// Conversion des rapports HID DualSense <-> XInput (sans etat, testable seul).
#pragma once
#include <windows.h>
#include <xinput.h>

#define DS_VID 0x054C
#define DS_PID 0x0CE6
#define DS_EDGE_PID 0x0DF2
#define DS_USB_INPUT_LEN 64
#define DS_BT_INPUT_LEN 78
#define DS_USB_OUTPUT_LEN 48
#define DS_BT_OUTPUT_LEN 78
#define DS4_REPORT_LEN 64

static SHORT ds_axis(BYTE v, BOOL invert) {
    int s = ((int)v - 128) * 256 + (v > 128 ? (v - 128) * 255 / 127 : 0);
    if (invert) s = -s - 1;
    return (SHORT)s;
}

// r pointe sur LX : LX LY RX RY, puis b0 b1 b2 les boutons, l2 r2 les gachettes.
static void ds_fill(XINPUT_GAMEPAD *p, const BYTE *r, BYTE b0, BYTE b1, BYTE b2, BYTE l2, BYTE r2) {
    static const WORD hat[8] = {
        XINPUT_GAMEPAD_DPAD_UP,
        XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_RIGHT,
        XINPUT_GAMEPAD_DPAD_RIGHT,
        XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_RIGHT,
        XINPUT_GAMEPAD_DPAD_DOWN,
        XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT,
        XINPUT_GAMEPAD_DPAD_LEFT,
        XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT,
    };
    WORD w = 0;
    if ((b0 & 0x0F) < 8) w |= hat[b0 & 0x0F];
    if (b0 & 0x10) w |= XINPUT_GAMEPAD_X;              // carre
    if (b0 & 0x20) w |= XINPUT_GAMEPAD_A;              // croix
    if (b0 & 0x40) w |= XINPUT_GAMEPAD_B;              // rond
    if (b0 & 0x80) w |= XINPUT_GAMEPAD_Y;              // triangle
    if (b1 & 0x01) w |= XINPUT_GAMEPAD_LEFT_SHOULDER;  // L1
    if (b1 & 0x02) w |= XINPUT_GAMEPAD_RIGHT_SHOULDER; // R1
    if (b1 & 0x10) w |= XINPUT_GAMEPAD_BACK;           // Create
    if (b1 & 0x20) w |= XINPUT_GAMEPAD_START;          // Options
    if (b1 & 0x40) w |= XINPUT_GAMEPAD_LEFT_THUMB;     // L3
    if (b1 & 0x80) w |= XINPUT_GAMEPAD_RIGHT_THUMB;    // R3
    if (b2 & 0x02) w |= XINPUT_GAMEPAD_BACK;           // clic du pave tactile
    p->wButtons = w;
    p->bLeftTrigger = l2;
    p->bRightTrigger = r2;
    p->sThumbLX = ds_axis(r[0], FALSE);
    p->sThumbLY = ds_axis(r[1], TRUE);
    p->sThumbRX = ds_axis(r[2], FALSE);
    p->sThumbRY = ds_axis(r[3], TRUE);
}

// Retourne FALSE si le rapport n'est pas un rapport d'entree connu.
static BOOL ds_parse_input(const BYTE *r, DWORD len, XINPUT_GAMEPAD *p) {
    if (r[0] == 0x01 && len == DS_USB_INPUT_LEN) {         // USB
        ds_fill(p, r + 1, r[8], r[9], r[10], r[5], r[6]);
        return TRUE;
    }
    if (r[0] == 0x31 && len >= DS_BT_INPUT_LEN) {          // Bluetooth, mode complet
        ds_fill(p, r + 2, r[9], r[10], r[11], r[6], r[7]);
        return TRUE;
    }
    if (r[0] == 0x01 && len >= 10) {                       // Bluetooth, mode simple
        ds_fill(p, r + 1, r[5], r[6], r[7], r[8], r[9]);
        return TRUE;
    }
    return FALSE;
}

// Rapport d'entree d'une DualShock 4 USB, construit a partir d'un rapport DualSense valide.
static void ds_make_ds4(const BYTE *r, DWORD len, BYTE *o, BYTE counter) {
    const BYTE *s = r[0] == 0x31 ? r + 1 : r;   // meme disposition que l'USB, decalee d'un octet
    BOOL full = r[0] == 0x31 || len == DS_USB_INPUT_LEN;
    ZeroMemory(o, DS4_REPORT_LEN);
    o[0] = 0x01;
    CopyMemory(o + 1, s + 1, 4);                // sticks
    o[5] = full ? s[8] : s[5];                  // croix directionnelle et symboles
    o[6] = full ? s[9] : s[6];                  // L1 R1 L2 R2 Create Options L3 R3
    o[7] = (BYTE)(((full ? s[10] : s[7]) & 0x03) | (counter << 2)); // PS, pave tactile, compteur
    o[8] = full ? s[5] : s[8];                  // gachettes
    o[9] = full ? s[6] : s[9];
    o[10] = counter;                            // horodatage
    o[12] = 0xFF;                               // batterie
    if (full) CopyMemory(o + 13, s + 16, 12);   // gyroscope et accelerometre
    o[30] = 0x1B;                               // alimentee en USB
    o[35] = o[39] = 0x80;                       // aucun doigt sur le pave tactile
}

static DWORD ds_crc32(const BYTE *d, DWORD n, DWORD crc) {
    for (DWORD i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return crc;
}

// Rumble XInput : wLeftMotorSpeed = gros moteur, wRightMotorSpeed = petit moteur.
static DWORD ds_build_output(BYTE *o, BOOL bluetooth, BYTE seq, WORD large, WORD small_) {
    BYTE *c;
    DWORD len = bluetooth ? DS_BT_OUTPUT_LEN : DS_USB_OUTPUT_LEN;
    ZeroMemory(o, len);
    if (bluetooth) {
        o[0] = 0x31;
        o[1] = (BYTE)(seq << 4);
        o[2] = 0x10;
        c = o + 3;
    } else {
        o[0] = 0x02;
        c = o + 1;
    }
    c[0] = 0x03;                    // vibration compatible + selection des haptiques
    c[2] = (BYTE)(small_ >> 8);     // moteur droit
    c[3] = (BYTE)(large >> 8);      // moteur gauche
    c[39] = 0x04;                   // vibration compatible v2 (firmwares recents)
    if (bluetooth) {
        BYTE seed = 0xA2;
        DWORD crc = ~ds_crc32(o, len - 4, ds_crc32(&seed, 1, 0xFFFFFFFFu));
        CopyMemory(o + len - 4, &crc, 4);
    }
    return len;
}
