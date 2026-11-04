// dinput8.dll de remplacement : cache la DualSense a DirectInput, sinon le jeu la lirait
// une seconde fois avec des boutons melanges. Les autres peripheriques passent normalement.
#include <windows.h>

typedef HRESULT (WINAPI *Create_t)(HINSTANCE, DWORD, REFIID, void **, IUnknown *);
typedef BOOL (CALLBACK *EnumCb_t)(const void *, void *);
typedef HRESULT (STDMETHODCALLTYPE *EnumDevices_t)(void *, DWORD, EnumCb_t, void *, DWORD);

#define VTBL_ENUMDEVICES 4
#define OFFSET_GUID_PRODUCT 20 // dwSize + guidInstance, identique en A et en W

static const GUID IID_A = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
static const GUID IID_W = {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};

static EnumDevices_t g_origA, g_origW;

typedef struct { EnumCb_t cb; void *ref; } Filter;

static BOOL CALLBACK filter_cb(const void *inst, void *ref) {
    const Filter *f = (const Filter *)ref;
    const GUID *product = (const GUID *)((const BYTE *)inst + OFFSET_GUID_PRODUCT);
    // guidProduct.Data1 vaut PID << 16 | VID
    DWORD id = product->Data1;
    if ((id & 0xFFFF) == 0x054C && ((id >> 16) == 0x0CE6 || (id >> 16) == 0x0DF2))
        return TRUE; // DIENUM_CONTINUE
    return f->cb(inst, f->ref);
}

static HRESULT STDMETHODCALLTYPE hookA(void *self, DWORD type, EnumCb_t cb, void *ref, DWORD flags) {
    Filter f = {cb, ref};
    return g_origA(self, type, filter_cb, &f, flags);
}

static HRESULT STDMETHODCALLTYPE hookW(void *self, DWORD type, EnumCb_t cb, void *ref, DWORD flags) {
    Filter f = {cb, ref};
    return g_origW(self, type, filter_cb, &f, flags);
}

static void patch_vtable(void *obj, BOOL wide) {
    void **slot = *(void ***)obj + VTBL_ENUMDEVICES;
    void *hook = wide ? (void *)hookW : (void *)hookA;
    DWORD old;
    if (*slot == hook) return;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return;
    if (wide) g_origW = (EnumDevices_t)*slot;
    else g_origA = (EnumDevices_t)*slot;
    *slot = hook;
    VirtualProtect(slot, sizeof(void *), old, &old);
}

HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, void **out, IUnknown *outer) {
    static Create_t real;
    if (!real) {
        wchar_t path[MAX_PATH];
        GetSystemDirectoryW(path, MAX_PATH);
        lstrcatW(path, L"\\dinput8.dll");
        HMODULE m = LoadLibraryW(path);
        if (m) real = (Create_t)GetProcAddress(m, "DirectInput8Create");
        if (!real) return E_FAIL;
    }
    HRESULT hr = real(inst, version, riid, out, outer);
    if (SUCCEEDED(hr) && out && *out) {
        if (IsEqualGUID(riid, &IID_A)) patch_vtable(*out, FALSE);
        else if (IsEqualGUID(riid, &IID_W)) patch_vtable(*out, TRUE);
    }
    return hr;
}
