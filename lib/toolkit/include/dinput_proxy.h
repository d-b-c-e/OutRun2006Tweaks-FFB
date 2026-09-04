// dinput_proxy.h - the reusable half of a dinput8.dll wrapper proxy (C++11).
//
// A proxy is a DLL named dinput8.dll placed beside a game exe. Windows loads it
// instead of the system one, it forwards DirectInput8Create to the real DLL,
// and in between it patches COM vtables so it can watch what the game reads and
// writes - or change one field, which is often the entire fix.
//
// What is here is the part that is identical in every proxy and expensive to
// get wrong: loading the real DLL, patching a vtable slot safely, the slot
// numbers themselves, the SIXDOF -> DRIVING re-type, and the blank-twin filter.
// What is NOT here is your hooks: what you do with a device state or an effect
// is per game, and the hook signatures depend on which interfaces you touch.
//
//   #include "dinput_proxy.h"
//   using namespace dbce::dproxy;
//
//   extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD v, REFIID riid,
//                                                LPVOID* out, LPUNKNOWN unk) {
//       if (!load_real_dinput8()) return E_FAIL;
//       HRESULT hr = real_create()(h, v, riid, out, unk);
//       if (SUCCEEDED(hr) && *out) {
//           patch_vtable(*out, DI8_CREATE_DEVICE, (void*)MyCreateDevice, &g_origCreateDevice);
//           patch_vtable(*out, DI8_ENUM_DEVICES,  (void*)MyEnumDevices,  &g_origEnumDevices);
//       }
//       return hr;
//   }
//
// Lineage: milestone-wheel-tools src/proxy.cpp, verified on Gravel at a MOZA
// R12 (2026-08-30); OutRun2006Tweaks-FFB src/Proxy.cpp.
//
// THREE RULES, each of which cost a debugging session somewhere:
//
//   * A vtable is shared by every instance of a class. Your hook fires for
//     devices you have never heard of, so check `self` first and forward
//     untouched when it is not yours.
//   * Do nothing heavy in DllMain. LoadLibrary, threads and DirectInput under
//     the loader lock deadlock; defer to the first forwarded call.
//   * Only one dinput8.dll can live in a folder. If the user already has one
//     from another tool, you must chain to it rather than replace it, or say
//     so loudly - silently overwriting someone else's proxy breaks their setup.
#pragma once

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
#include <string.h>

namespace dbce { namespace dproxy {

// ---------------------------------------------------------------- vtable slots
// Counted from the start of the COM vtable, IUnknown's three included. These
// are stable across the DirectInput 8 ABI and are the numbers every proxy in
// this family uses; naming them stops the next person recounting.
enum Di8Slot {                 // IDirectInput8
    DI8_QUERY_INTERFACE  = 0,
    DI8_ADD_REF          = 1,
    DI8_RELEASE          = 2,
    DI8_CREATE_DEVICE    = 3,
    DI8_ENUM_DEVICES     = 4,
};

enum Di8DeviceSlot {           // IDirectInputDevice8
    DID8_GET_CAPABILITIES = 3,
    DID8_GET_PROPERTY     = 5,
    DID8_SET_PROPERTY     = 6,
    DID8_ACQUIRE          = 7,
    DID8_UNACQUIRE        = 8,
    DID8_GET_DEVICE_STATE = 9,
    DID8_SET_DATA_FORMAT  = 11,
    DID8_GET_DEVICE_INFO  = 15,
    DID8_CREATE_EFFECT    = 18,
};

enum Di8EffectSlot {           // IDirectInputEffect
    DIE_SET_PARAMETERS    = 6,
};

// ---------------------------------------------------------------- the real DLL
// Loaded from system32 by full path. Loading "dinput8.dll" by name from inside
// a proxy named dinput8.dll finds itself.
typedef HRESULT (WINAPI *PfnDirectInput8Create)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

inline HMODULE& real_module() { static HMODULE m = NULL; return m; }

inline bool load_real_dinput8()
{
    if (real_module()) return true;
    char path[MAX_PATH];
    UINT n = GetSystemDirectoryA(path, MAX_PATH);
    if (!n || n >= MAX_PATH - 13) return false;
    strcat_s(path, MAX_PATH, "\\dinput8.dll");
    real_module() = LoadLibraryA(path);
    return real_module() != NULL;
}

inline PfnDirectInput8Create real_create()
{
    if (!load_real_dinput8()) return NULL;
    static PfnDirectInput8Create fn = NULL;
    if (!fn) fn = (PfnDirectInput8Create)GetProcAddress(real_module(), "DirectInput8Create");
    return fn;
}

// ---------------------------------------------------------------- vtable patch
// Replaces one slot, remembering the original. Idempotent: a second call for a
// slot already patched does nothing, which matters because the game creates
// many device objects that share one vtable and you will be called repeatedly.
// Returns false only if the page could not be made writable.
inline bool patch_vtable(void* obj, int slot, void* hook, void** original)
{
    if (!obj || !original) return false;
    if (*original) return true;                   // already patched
    void** vt = *(void***)obj;
    DWORD old;
    if (!VirtualProtect(&vt[slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
    *original = vt[slot];
    vt[slot] = hook;
    VirtualProtect(&vt[slot], sizeof(void*), old, &old);
    return true;
}

// ---------------------------------------------------------------- the re-type
// DirectInput classifies an HID joystick with six or more axes as
// DI8DEVTYPE_1STPERSON / SIXDOF (0x18). A direct-drive base is therefore never
// DI8DEVTYPE_DRIVING, and a game that decides "is this a wheel" from dwDevType
// offers it as neither wheel nor gamepad - Gravel shows no wheel option at all
// and flickers between keyboard and controller.
//
// Rewriting dwDevType in the two places a game reads it (the EnumDevices
// callback and GetCapabilities/GetDeviceInfo) fixes that, and nothing else is
// altered, so force feedback passes through intact. Miss either read site and
// the game sees a contradiction, which is worse than the original problem.
//
// `productKey` is guidProduct.Data1, i.e. (PID << 16) | VID - the same value
// games use as a config key. 0 means "any device".
inline bool retype_as_wheel(DWORD productKey, DWORD deviceProductData1, DWORD& devType,
                            bool threePedals = true)
{
    if (productKey && deviceProductData1 != productKey) return false;
    if ((devType & 0xFF) == DI8DEVTYPE_DRIVING) return false;      // already a wheel
    devType = (devType & 0xFFFF0000) | DI8DEVTYPE_DRIVING |
              ((threePedals ? DI8DEVTYPEDRIVING_THREEPEDALS : DI8DEVTYPEDRIVING_LIMITED) << 8);
    return true;
}

// ---------------------------------------------------------------- blank twins
// A game creates several device objects for the same physical wheel. Some are
// only ever enumerated, or held unacquired, and read back all zeros - which
// would overwrite the real reads at poll rate and make the wheel look dead.
//
// The rule that works: once ANY object has produced a non-blank state, only
// objects that have done so may update your state. Feed every GetDeviceState
// through this and ignore it when it returns false.
class BlankTwinFilter {
public:
    BlankTwinFilter() : live_(0) {}

    // `blank` is your own judgement of "this state carries nothing", e.g. every
    // axis at 0 or centre and no buttons down.
    bool should_accept(void* self, bool blank)
    {
        if (!blank) { live_ = self; return true; }   // proved itself
        return live_ == self || live_ == 0;          // trusted, or nobody has yet
    }
    void reset() { live_ = 0; }

private:
    void* live_;
};

}} // namespace dbce::dproxy
