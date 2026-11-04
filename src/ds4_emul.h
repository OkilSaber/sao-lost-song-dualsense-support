// Presente la DualSense au module PS4 natif du jeu (PadDevicePS4Manager) comme une
// DualShock 4 USB, en interceptant ses appels HID dans la table d'imports de l'exe.
// Les valeurs ci-dessous sont celles que ce module verifie.
#pragma once
#include <hidpi.h>

#define EMUL_MAX_HANDLES 16
#define DS4_INPUT_LEN 64
#define DS4_OUTPUT_LEN 32
#define DS4_FEATURE_LEN 64

static HANDLE volatile g_emulHandles[EMUL_MAX_HANDLES];
static BYTE g_fakePreparsed[8];
#define FAKE_PP ((PHIDP_PREPARSED_DATA)g_fakePreparsed)

static BOOL emul_is(HANDLE h) {
    if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
    for (int i = 0; i < EMUL_MAX_HANDLES; i++)
        if (g_emulHandles[i] == h) return TRUE;
    return FALSE;
}

static void emul_add(HANDLE h) {
    for (int i = 0; i < EMUL_MAX_HANDLES; i++)
        if (!InterlockedCompareExchangePointer((void *volatile *)&g_emulHandles[i], h, NULL)) return;
}

static void emul_remove(HANDLE h) {
    for (int i = 0; i < EMUL_MAX_HANDLES; i++)
        if (g_emulHandles[i] == h) g_emulHandles[i] = NULL;
}

static BOOL path_is_dualsense(LPCWSTR p) {
    return p && wcsstr(p, L"054c") && (wcsstr(p, L"0ce6") || wcsstr(p, L"0df2"));
}

static HANDLE (WINAPI *o_CreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static HANDLE WINAPI h_CreateFileW(LPCWSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD f, HANDLE t) {
    HANDLE h = o_CreateFileW(p, a, s, sa, c, f, t);
    if (h != INVALID_HANDLE_VALUE && path_is_dualsense(p)) emul_add(h);
    return h;
}

static BOOL (WINAPI *o_CloseHandle)(HANDLE);
static BOOL WINAPI h_CloseHandle(HANDLE h) {
    emul_remove(h);
    return o_CloseHandle(h);
}

static BOOLEAN (WINAPI *o_HidD_GetAttributes)(HANDLE, PHIDD_ATTRIBUTES);
static BOOLEAN WINAPI h_HidD_GetAttributes(HANDLE h, PHIDD_ATTRIBUTES a) {
    BOOLEAN ok = o_HidD_GetAttributes(h, a);
    if (ok && emul_is(h)) {
        a->ProductID = 0x09CC;      // DualShock 4 v2
        a->VersionNumber = 0x0100;  // le jeu exige au moins 23 en decimal
    }
    return ok;
}

static BOOLEAN (WINAPI *o_HidD_GetPreparsedData)(HANDLE, PHIDP_PREPARSED_DATA *);
static BOOLEAN WINAPI h_HidD_GetPreparsedData(HANDLE h, PHIDP_PREPARSED_DATA *p) {
    if (emul_is(h)) {
        *p = FAKE_PP;
        return TRUE;
    }
    return o_HidD_GetPreparsedData(h, p);
}

static BOOLEAN (WINAPI *o_HidD_FreePreparsedData)(PHIDP_PREPARSED_DATA);
static BOOLEAN WINAPI h_HidD_FreePreparsedData(PHIDP_PREPARSED_DATA p) {
    return p == FAKE_PP ? TRUE : o_HidD_FreePreparsedData(p);
}

static NTSTATUS (WINAPI *o_HidP_GetCaps)(PHIDP_PREPARSED_DATA, PHIDP_CAPS);
static NTSTATUS WINAPI h_HidP_GetCaps(PHIDP_PREPARSED_DATA p, PHIDP_CAPS c) {
    if (p != FAKE_PP) return o_HidP_GetCaps(p, c);
    ZeroMemory(c, sizeof(*c));
    c->UsagePage = 1;
    c->Usage = 5;
    c->InputReportByteLength = DS4_INPUT_LEN;
    c->OutputReportByteLength = DS4_OUTPUT_LEN;
    c->FeatureReportByteLength = DS4_FEATURE_LEN;
    c->NumberFeatureValueCaps = 1;
    return HIDP_STATUS_SUCCESS;
}

static NTSTATUS (WINAPI *o_HidP_GetValueCaps)(HIDP_REPORT_TYPE, PHIDP_VALUE_CAPS, PUSHORT, PHIDP_PREPARSED_DATA);
static NTSTATUS WINAPI h_HidP_GetValueCaps(HIDP_REPORT_TYPE t, PHIDP_VALUE_CAPS v, PUSHORT n, PHIDP_PREPARSED_DATA p) {
    if (p != FAKE_PP) return o_HidP_GetValueCaps(t, v, n, p);
    if (t != HidP_Feature || *n < 1) {
        *n = 0;
        return HIDP_STATUS_SUCCESS;
    }
    // La signature qui fait choisir au jeu le mode USB.
    ZeroMemory(v, sizeof(*v));
    v->UsagePage = 0xFF80;
    v->ReportID = 0xA3;
    v->ReportCount = 0x30;
    v->BitSize = 8;
    v->NotRange.Usage = 0x43;
    *n = 1;
    return HIDP_STATUS_SUCCESS;
}

static void put16(BYTE *b, int off, WORD v) {
    b[off] = (BYTE)v;
    b[off + 1] = (BYTE)(v >> 8);
}

static BOOLEAN (WINAPI *o_HidD_GetFeature)(HANDLE, PVOID, ULONG);
static BOOLEAN WINAPI h_HidD_GetFeature(HANDLE h, PVOID buf, ULONG n) {
    if (!emul_is(h)) return o_HidD_GetFeature(h, buf, n);
    BYTE *b = (BYTE *)buf;
    BYTE id = b[0];
    ZeroMemory(b, n);
    b[0] = id;
    switch (id) {
    case 0x12: {                    // adresse de la manette, ne doit pas etre nulle
        static const BYTE mac[6] = {0x5E, 0x05, 0xD5, 0xCE, 0x4C, 0x05};
        if (n >= 7) CopyMemory(b + 1, mac, 6);
        return TRUE;
    }
    case 0xA3:                      // versions : le jeu exige firmware >= 0x3100
        if (n >= 0x31) {
            CopyMemory(b + 1, "Nov  4 2026", 11);
            CopyMemory(b + 0x11, "12:00:00", 8);
            put16(b, 0x21, 0x0100);
            put16(b, 0x23, 0x7070);
        }
        return TRUE;
    case 0x02: {                    // calibration du gyroscope et de l'accelerometre
        static const WORD gain[14] = {8192, 0xE000, 8192, 0xE000, 8192, 0xE000, 540, 540,
                                      8192, 0xE000, 8192, 0xE000, 8192, 0xE000};
        for (int i = 0; i < 14 && 7 + i * 2 + 1 < (int)n; i++) put16(b, 7 + i * 2, gain[i]);
        return TRUE;
    }
    }
    log_line("Rapport de fonction 0x%02X demande par le jeu, non emule", id);
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

static BOOLEAN (WINAPI *o_HidD_SetFeature)(HANDLE, PVOID, ULONG);
static BOOLEAN WINAPI h_HidD_SetFeature(HANDLE h, PVOID b, ULONG n) {
    return emul_is(h) ? TRUE : o_HidD_SetFeature(h, b, n);
}

static void emul_string(PVOID b, ULONG n, const wchar_t *s) {
    if (n >= sizeof(wchar_t)) lstrcpynW((wchar_t *)b, s, (int)(n / sizeof(wchar_t)));
}

static BOOLEAN (WINAPI *o_HidD_GetManufacturerString)(HANDLE, PVOID, ULONG);
static BOOLEAN WINAPI h_HidD_GetManufacturerString(HANDLE h, PVOID b, ULONG n) {
    if (!emul_is(h)) return o_HidD_GetManufacturerString(h, b, n);
    emul_string(b, n, L"Sony Computer Entertainment");
    return TRUE;
}

static BOOLEAN (WINAPI *o_HidD_GetProductString)(HANDLE, PVOID, ULONG);
static BOOLEAN WINAPI h_HidD_GetProductString(HANDLE h, PVOID b, ULONG n) {
    if (!emul_is(h)) return o_HidD_GetProductString(h, b, n);
    emul_string(b, n, L"Wireless Controller");
    return TRUE;
}

static BOOL (WINAPI *o_ReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL WINAPI h_ReadFile(HANDLE h, LPVOID b, DWORD n, LPDWORD got, LPOVERLAPPED ov) {
    if (!emul_is(h) || n < DS4_INPUT_LEN) return o_ReadFile(h, b, n, got, ov);
    init();
    if (!g_ds4Active && InterlockedExchange(&g_ds4Active, 1) == 0)
        log_line("Le jeu utilise la manette en mode PS4");
    // Au demarrage, laisser au thread de lecture le temps de recevoir un premier rapport.
    for (int i = 0; i < 40 && !g_connected; i++) Sleep(50);
    if (!g_connected) {
        SetLastError(ERROR_DEVICE_NOT_CONNECTED);
        return FALSE;
    }
    WaitForSingleObject(g_reportEvent, 100);
    AcquireSRWLockShared(&g_lock);
    CopyMemory(b, g_ds4Report, DS4_INPUT_LEN);
    ReleaseSRWLockShared(&g_lock);
    if (got) *got = DS4_INPUT_LEN;
    return TRUE;
}

static BOOL (WINAPI *o_WriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL WINAPI h_WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD w, LPOVERLAPPED ov) {
    if (!emul_is(h)) return o_WriteFile(h, buf, n, w, ov);
    const BYTE *b = (const BYTE *)buf;
    if (n >= 6 && b[0] == 0x05) {   // [4] petit moteur, [5] gros moteur
        XINPUT_VIBRATION v;
        v.wLeftMotorSpeed = (WORD)(b[5] << 8);
        v.wRightMotorSpeed = (WORD)(b[4] << 8);
        set_rumble(&v);
    }
    if (w) *w = n;
    return TRUE;
}

static void emul_install(void) {
    static const struct { const char *name; void *hook; void **orig; } T[] = {
        {"CreateFileW", h_CreateFileW, (void **)&o_CreateFileW},
        {"CloseHandle", h_CloseHandle, (void **)&o_CloseHandle},
        {"ReadFile", h_ReadFile, (void **)&o_ReadFile},
        {"WriteFile", h_WriteFile, (void **)&o_WriteFile},
        {"HidD_GetAttributes", h_HidD_GetAttributes, (void **)&o_HidD_GetAttributes},
        {"HidD_GetPreparsedData", h_HidD_GetPreparsedData, (void **)&o_HidD_GetPreparsedData},
        {"HidD_FreePreparsedData", h_HidD_FreePreparsedData, (void **)&o_HidD_FreePreparsedData},
        {"HidP_GetCaps", h_HidP_GetCaps, (void **)&o_HidP_GetCaps},
        {"HidP_GetValueCaps", h_HidP_GetValueCaps, (void **)&o_HidP_GetValueCaps},
        {"HidD_GetFeature", h_HidD_GetFeature, (void **)&o_HidD_GetFeature},
        {"HidD_SetFeature", h_HidD_SetFeature, (void **)&o_HidD_SetFeature},
        {"HidD_GetManufacturerString", h_HidD_GetManufacturerString, (void **)&o_HidD_GetManufacturerString},
        {"HidD_GetProductString", h_HidD_GetProductString, (void **)&o_HidD_GetProductString},
    };
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    int patched = 0;
    for (; imp->Name; imp++) {
        if (!imp->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const char *fn = ((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name;
            for (int i = 0; i < (int)(sizeof(T) / sizeof(T[0])); i++) {
                DWORD old;
                if (strcmp(fn, T[i].name) || !VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) continue;
                *T[i].orig = (void *)iat->u1.Function;
                iat->u1.Function = (ULONG_PTR)T[i].hook;
                VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
                patched++;
            }
        }
    }
    log_line("Emulation PS4 : %d fonctions interceptees", patched);
}
