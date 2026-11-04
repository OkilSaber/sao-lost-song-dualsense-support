// xinput9_1_0.dll de remplacement : la DualSense est vue comme une manette PS4 par le
// module natif du jeu, ou a defaut comme la manette XInput 0.
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <stdio.h>
#include <stdarg.h>
#include "dualsense.h"

typedef DWORD (WINAPI *GetState_t)(DWORD, XINPUT_STATE *);
typedef DWORD (WINAPI *SetState_t)(DWORD, XINPUT_VIBRATION *);
typedef DWORD (WINAPI *GetCaps_t)(DWORD, DWORD, XINPUT_CAPABILITIES *);
typedef DWORD (WINAPI *GetGuids_t)(DWORD, GUID *, GUID *);

static HMODULE g_self, g_real;
static GetState_t g_realGetState;
static SetState_t g_realSetState;
static GetCaps_t g_realGetCaps;
static GetGuids_t g_realGetGuids;

static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static SRWLOCK g_lock = SRWLOCK_INIT;
static volatile LONG g_connected;
static XINPUT_GAMEPAD g_pad;
static DWORD g_packet;
static HANDLE g_write = INVALID_HANDLE_VALUE;
static BOOL g_bluetooth;
static XINPUT_VIBRATION g_vibration;
static HANDLE g_rumbleEvent;
static HANDLE g_reportEvent;
static volatile LONG g_ds4Active;
static BYTE g_ds4Report[DS4_REPORT_LEN];

static void log_line(const char *fmt, ...) {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(g_self, path, MAX_PATH);
    while (n && path[n - 1] != '\\') n--;
    lstrcpynA(path + n, "sao_ps5.log", MAX_PATH - n);
    FILE *f;
    if (fopen_s(&f, path, "a") == 0) {
        SYSTEMTIME t;
        va_list a;
        GetLocalTime(&t);
        fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
        va_start(a, fmt);
        vfprintf(f, fmt, a);
        va_end(a);
        fputc('\n', f);
        fclose(f);
    }
}

// Ouvre la premiere DualSense trouvee. Retourne le handle de lecture.
static HANDLE open_dualsense(HANDLE *write, DWORD *inputLen) {
    GUID guid;
    HANDLE found = INVALID_HANDLE_VALUE;
    HidD_GetHidGuid(&guid);
    HDEVINFO set = SetupDiGetClassDevsW(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return found;
    SP_DEVICE_INTERFACE_DATA itf;
    itf.cbSize = sizeof(itf);
    for (DWORD i = 0; found == INVALID_HANDLE_VALUE && SetupDiEnumDeviceInterfaces(set, NULL, &guid, i, &itf); i++) {
        BYTE buf[1024];
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *det = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *)buf;
        det->cbSize = sizeof(*det);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &itf, det, sizeof(buf), NULL, NULL)) continue;
        HANDLE h = CreateFileW(det->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES at;
        at.Size = sizeof(at);
        PHIDP_PREPARSED_DATA pp = NULL;
        HIDP_CAPS caps = {0};
        BOOL ok = HidD_GetAttributes(h, &at) && at.VendorID == DS_VID &&
                  (at.ProductID == DS_PID || at.ProductID == DS_EDGE_PID) && HidD_GetPreparsedData(h, &pp);
        if (ok) {
            ok = HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage == 1 && caps.Usage == 5;
            HidD_FreePreparsedData(pp);
        }
        if (!ok) {
            CloseHandle(h);
            continue;
        }
        // Un handle dedie a l'ecriture : ReadFile bloquant monopoliserait le premier.
        *write = CreateFileW(det->DevicePath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, 0, NULL);
        *inputLen = caps.InputReportByteLength;
        found = h;
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

static DWORD WINAPI reader_thread(void *unused) {
    (void)unused;
    for (;;) {
        HANDLE write = INVALID_HANDLE_VALUE;
        DWORD inputLen = 0;
        HANDLE h = open_dualsense(&write, &inputLen);
        if (h == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }
        BOOL bluetooth = inputLen >= DS_BT_INPUT_LEN;
        if (bluetooth) {
            // Lire ce rapport de calibration fait passer la manette au rapport complet 0x31.
            BYTE feat[41] = {0x05};
            HidD_GetFeature(h, feat, sizeof(feat));
        }
        log_line("DualSense ouverte (%s, rapport %lu octets)", bluetooth ? "Bluetooth" : "USB", inputLen);
        AcquireSRWLockExclusive(&g_lock);
        g_write = write;
        g_bluetooth = bluetooth;
        ReleaseSRWLockExclusive(&g_lock);

        BYTE r[128];
        DWORD got;
        BOOL first = TRUE;
        BYTE counter = 0;
        while (ReadFile(h, r, sizeof(r), &got, NULL)) {
            XINPUT_GAMEPAD p;
            if (!ds_parse_input(r, got, &p)) continue;
            AcquireSRWLockExclusive(&g_lock);
            if (memcmp(&p, &g_pad, sizeof(p)) != 0) g_packet++;
            g_pad = p;
            ds_make_ds4(r, got, g_ds4Report, counter++);
            ReleaseSRWLockExclusive(&g_lock);
            InterlockedExchange(&g_connected, 1);
            SetEvent(g_reportEvent);
            if (first) {
                log_line("Premier rapport recu : id 0x%02X, %lu octets", r[0], got);
                first = FALSE;
            }
        }
        log_line("DualSense perdue (erreur %lu)", GetLastError());
        InterlockedExchange(&g_connected, 0);
        AcquireSRWLockExclusive(&g_lock);
        g_write = INVALID_HANDLE_VALUE;
        ReleaseSRWLockExclusive(&g_lock);
        CloseHandle(h);
        if (write != INVALID_HANDLE_VALUE) CloseHandle(write);
    }
}

// L'envoi peut bloquer quelques ms : il ne doit jamais se faire dans le thread du jeu.
static DWORD WINAPI rumble_thread(void *unused) {
    BYTE seq = 0;
    (void)unused;
    for (;;) {
        WaitForSingleObject(g_rumbleEvent, INFINITE);
        BYTE o[DS_BT_OUTPUT_LEN];
        DWORD written;
        AcquireSRWLockShared(&g_lock);
        HANDLE h = g_write;
        DWORD len = ds_build_output(o, g_bluetooth, seq, g_vibration.wLeftMotorSpeed, g_vibration.wRightMotorSpeed);
        ReleaseSRWLockShared(&g_lock);
        seq = (seq + 1) & 0x0F;
        if (h != INVALID_HANDLE_VALUE && !WriteFile(h, o, len, &written, NULL))
            log_line("Vibration refusee (erreur %lu)", GetLastError());
    }
}

static BOOL CALLBACK init_once(PINIT_ONCE once, void *param, void **ctx) {
    wchar_t path[MAX_PATH];
    (void)once; (void)param; (void)ctx;
    GetSystemDirectoryW(path, MAX_PATH);
    lstrcatW(path, L"\\xinput9_1_0.dll");
    g_real = LoadLibraryW(path);
    if (g_real) {
        g_realGetState = (GetState_t)GetProcAddress(g_real, "XInputGetState");
        g_realSetState = (SetState_t)GetProcAddress(g_real, "XInputSetState");
        g_realGetCaps = (GetCaps_t)GetProcAddress(g_real, "XInputGetCapabilities");
        g_realGetGuids = (GetGuids_t)GetProcAddress(g_real, "XInputGetDSoundAudioDeviceGuids");
    }
    log_line("Mod charge, XInput systeme %s", g_real ? "trouve" : "introuvable");
    g_rumbleEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_reportEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    CloseHandle(CreateThread(NULL, 0, reader_thread, NULL, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, rumble_thread, NULL, 0, NULL));
    return TRUE;
}

static void init(void) {
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
}

// Les vraies manettes XInput restent utilisables, decalees d'un rang si la DualSense est la.
// En mode PS4, le jeu lit la DualSense par HID : elle ne doit plus apparaitre ici aussi.
static BOOL forward_index(DWORD *index) {
    if (!g_connected || g_ds4Active) return TRUE;
    if (*index == 0) return FALSE;
    (*index)--;
    return TRUE;
}

static void set_rumble(const XINPUT_VIBRATION *vib) {
    AcquireSRWLockExclusive(&g_lock);
    BOOL changed = vib->wLeftMotorSpeed != g_vibration.wLeftMotorSpeed ||
                   vib->wRightMotorSpeed != g_vibration.wRightMotorSpeed;
    g_vibration = *vib;
    ReleaseSRWLockExclusive(&g_lock);
    if (changed) SetEvent(g_rumbleEvent);
}

#include "ds4_emul.h"

DWORD WINAPI XInputGetState(DWORD index, XINPUT_STATE *state) {
    init();
    if (!state) return ERROR_BAD_ARGUMENTS;
    if (forward_index(&index))
        return g_realGetState ? g_realGetState(index, state) : ERROR_DEVICE_NOT_CONNECTED;
    AcquireSRWLockShared(&g_lock);
    state->dwPacketNumber = g_packet;
    state->Gamepad = g_pad;
    ReleaseSRWLockShared(&g_lock);
    return ERROR_SUCCESS;
}

DWORD WINAPI XInputSetState(DWORD index, XINPUT_VIBRATION *vib) {
    init();
    if (!vib) return ERROR_BAD_ARGUMENTS;
    if (forward_index(&index))
        return g_realSetState ? g_realSetState(index, vib) : ERROR_DEVICE_NOT_CONNECTED;
    set_rumble(vib);
    return ERROR_SUCCESS;
}

DWORD WINAPI XInputGetCapabilities(DWORD index, DWORD flags, XINPUT_CAPABILITIES *caps) {
    init();
    if (!caps) return ERROR_BAD_ARGUMENTS;
    if (forward_index(&index))
        return g_realGetCaps ? g_realGetCaps(index, flags, caps) : ERROR_DEVICE_NOT_CONNECTED;
    ZeroMemory(caps, sizeof(*caps));
    caps->Type = XINPUT_DEVTYPE_GAMEPAD;
    caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
    caps->Gamepad.wButtons = 0xF3FF;
    caps->Gamepad.bLeftTrigger = caps->Gamepad.bRightTrigger = 255;
    caps->Gamepad.sThumbLX = caps->Gamepad.sThumbLY = -64;
    caps->Gamepad.sThumbRX = caps->Gamepad.sThumbRY = -64;
    caps->Vibration.wLeftMotorSpeed = caps->Vibration.wRightMotorSpeed = 65535;
    return ERROR_SUCCESS;
}

DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD index, GUID *render, GUID *capture) {
    init();
    if (forward_index(&index) && g_realGetGuids) return g_realGetGuids(index, render, capture);
    if (render) ZeroMemory(render, sizeof(*render));
    if (capture) ZeroMemory(capture, sizeof(*capture));
    return ERROR_SUCCESS;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        emul_install();
    }
    return TRUE;
}
