// Tests de la conversion, puis (si une manette est branchee) lecture reelle via la DLL.
#include <stdio.h>
#include <string.h>
#include "dualsense.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("ECHEC ligne %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(int argc, char **argv) {
    XINPUT_GAMEPAD p;
    BYTE usb[64] = {0x01, 255, 0, 128, 128, 200, 10, 7, 0x20 | 2, 0x01 | 0x20, 0x02};
    CHECK(ds_parse_input(usb, 64, &p));
    CHECK(p.sThumbLX == 32767 && p.sThumbLY == 32767);
    CHECK(p.sThumbRX == 0 && p.sThumbRY == -1);
    CHECK(p.bLeftTrigger == 200 && p.bRightTrigger == 10);
    CHECK(p.wButtons == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_LEFT_SHOULDER |
                         XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK));

    BYTE bt[78] = {0x31, 0x11, 0, 255, 128, 128, 200, 10, 7, 0x40 | 8, 0x02};
    CHECK(ds_parse_input(bt, 78, &p));
    CHECK(p.sThumbLX == -32768 && p.sThumbLY == -32768);
    CHECK(p.wButtons == (XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_RIGHT_SHOULDER)); // hat 8 = neutre

    BYTE simple[10] = {0x01, 128, 128, 128, 128, 0x80 | 8, 0x40, 0, 50, 60};
    CHECK(ds_parse_input(simple, 10, &p));
    CHECK(p.wButtons == (XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_LEFT_THUMB));
    CHECK(p.bLeftTrigger == 50 && p.bRightTrigger == 60);

    BYTE simple78[78] = {0x01, 128, 128, 128, 128, 0x80 | 8, 0x40, 0, 50, 60};
    CHECK(ds_parse_input(simple78, 78, &p));
    CHECK(p.wButtons == (XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_LEFT_THUMB));

    BYTE d[DS4_REPORT_LEN];
    ds_make_ds4(usb, 64, d, 5);
    CHECK(d[0] == 0x01 && d[1] == 255 && d[2] == 0 && d[3] == 128 && d[4] == 128);
    CHECK(d[5] == (0x20 | 2) && d[6] == (0x01 | 0x20) && d[7] == (0x02 | (5 << 2)));
    CHECK(d[8] == 200 && d[9] == 10 && d[35] == 0x80 && d[39] == 0x80);
    ds_make_ds4(bt, 78, d, 0);
    CHECK(d[1] == 0 && d[2] == 255 && d[5] == (0x40 | 8) && d[6] == 0x02 && d[8] == 200 && d[9] == 10);
    ds_make_ds4(simple78, 78, d, 0);
    CHECK(d[5] == (0x80 | 8) && d[6] == 0x40 && d[8] == 50 && d[9] == 60);

    BYTE unknown[64] = {0x07};
    CHECK(!ds_parse_input(unknown, 64, &p));

    BYTE o[78];
    CHECK(ds_build_output(o, FALSE, 0, 0xFF00, 0x8000) == 48);
    CHECK(o[0] == 0x02 && o[1] == 0x03 && o[3] == 0x80 && o[4] == 0xFF && o[40] == 0x04);
    CHECK(ds_build_output(o, TRUE, 3, 0xFF00, 0x8000) == 78);
    CHECK(o[0] == 0x31 && o[1] == 0x30 && o[2] == 0x10 && o[5] == 0x80 && o[6] == 0xFF);
    // Valeur de reference du CRC32 standard, "123456789" -> 0xCBF43926
    CHECK(~ds_crc32((const BYTE *)"123456789", 9, 0xFFFFFFFFu) == 0xCBF43926u);

    printf(fails ? "%d echec(s)\n" : "Conversion OK\n", fails);

    if (argc > 1) {
        HMODULE m = LoadLibraryA(argv[1]);
        if (!m) { printf("DLL non chargee (%lu)\n", GetLastError()); return 1; }
        DWORD (WINAPI *get)(DWORD, XINPUT_STATE *) = (void *)GetProcAddress(m, "XInputGetState");
        DWORD (WINAPI *set)(DWORD, XINPUT_VIBRATION *) = (void *)GetProcAddress(m, "XInputSetState");
        if (!get || !set) { printf("Exports manquants\n"); return 1; }
        XINPUT_STATE s;
        DWORD r = ERROR_DEVICE_NOT_CONNECTED;
        for (int i = 0; i < 30 && r != ERROR_SUCCESS; i++) { r = 1; get(0, &s); Sleep(100); r = get(0, &s); }
        printf("XInputGetState(0) = %lu\n", r);
        if (r == ERROR_SUCCESS)
            printf("boutons=%04X LX=%d LY=%d RX=%d RY=%d L2=%u R2=%u\n", s.Gamepad.wButtons, s.Gamepad.sThumbLX,
                   s.Gamepad.sThumbLY, s.Gamepad.sThumbRX, s.Gamepad.sThumbRY, s.Gamepad.bLeftTrigger, s.Gamepad.bRightTrigger);
    }
    return fails != 0;
}
