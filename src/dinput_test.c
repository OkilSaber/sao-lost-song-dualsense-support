// Compare ce que DirectInput enumere avec le dinput8.dll systeme et celui du mod.
#include <stdio.h>
#include <windows.h>
typedef HRESULT (WINAPI *Create_t)(HINSTANCE, DWORD, const GUID *, void **, IUnknown *);
typedef HRESULT (STDMETHODCALLTYPE *Enum_t)(void *, DWORD, BOOL (CALLBACK *)(const void *, void *), void *, DWORD);
static const GUID IID_A = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
static BOOL CALLBACK cb(const void *inst, void *ref) {
    (void)ref;
    const GUID *p = (const GUID *)((const BYTE *)inst + 20);
    printf("  VID %04lX PID %04lX %s\n", p->Data1 & 0xFFFF, p->Data1 >> 16, (const char *)inst + 56 + 260);
    return TRUE;
}
int main(int argc, char **argv) {
    (void)argc;
    HMODULE m = LoadLibraryA(argv[1]);
    if (!m) { printf("DLL non chargee\n"); return 1; }
    Create_t create = (Create_t)GetProcAddress(m, "DirectInput8Create");
    void *di = NULL;
    HRESULT hr = create(GetModuleHandleA(NULL), 0x0800, &IID_A, &di, NULL);
    printf("%s hr=%08lX\n", argv[1], hr);
    if (FAILED(hr)) return 1;
    Enum_t e = (Enum_t)(*(void ***)di)[4];
    e(di, 4 /*DI8DEVCLASS_GAMECTRL*/, cb, NULL, 1 /*DIEDFL_ATTACHEDONLY*/);
    return 0;
}
