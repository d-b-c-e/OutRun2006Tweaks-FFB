/* wheelffb.h - the C ABI of WheelFfb.dll (dbce-wheel-mod-toolkit), with a
 * runtime loader for native mods.
 *
 * A dinput8 proxy or an injected DLL cannot afford a static import of
 * WheelFfb.dll: if the file is missing the loader would refuse to load the
 * proxy and the game would not start. Load it at runtime instead, beside your
 * own module, and disable force feedback when it is absent:
 *
 *     static WheelFfbApi ffb;
 *     if (WheelFfb_LoadBeside(&ffb, thisModule, L"WheelFfb.dll")) {
 *         ffb.SetLogPath("C:\\...\\ffb.log");        // optional; default %LOCALAPPDATA%\DbceWheel\ffb.log
 *         ffb.SetPreferredDeviceIndex(iniIndex);      // or SetPreferredDevice("MOZA")
 *         if (ffb.InitDirectInput((int)(INT_PTR)hwnd)) {
 *             ffb.InstallExitGuards();
 *             int road = ffb.CreatePeriodicEffect(25); // -1: synthesise below 15 Hz instead
 *         }
 *     } else log("WheelFfb.dll: %s missing", WheelFfb_MissingExport(&ffb));
 *
 * Every export is listed once, in WHEELFFB_API_LIST; typedefs, the struct and
 * the resolver are generated from it. The list must match WheelFfb.def - the
 * toolkit build checks that. The ABI only ever gains exports; check
 * GetWheelFfbVersion() (100 = 0.1.0, 200 = 0.2.0) before using a newer one,
 * or accept WheelFfb_Load's partial result via WheelFfb_MissingExport.
 *
 * All functions are __cdecl (the x86 .def exports them undecorated). Force
 * magnitudes are DirectInput's -10000..10000; frequencies for the periodic
 * effects are millihertz (25 Hz = 25000). Never call FreeLibrary on the module
 * from DllMain - unload from your own shutdown path, after PanicStop.
 */
#ifndef DBCE_WHEELFFB_H
#define DBCE_WHEELFFB_H

#include <windows.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* X(return type, name, parameter list) */
#define WHEELFFB_API_LIST(X) \
    X(int,  InitDirectInput,        (int hwnd)) \
    X(void, Aquire,                 (void)) \
    X(int,  SetDeviceForcesXY,      (int x, int y)) \
    X(int,  StartEffect,            (void)) \
    X(int,  StopEffect,             (void)) \
    X(int,  SetAutoCenter,          (int enable)) \
    X(void, FreeDirectInput,        (void)) \
    X(void, SetPreferredDevice,     (const char* name)) \
    X(void, SetPreferredDeviceGuid, (const void* guid16)) \
    X(void, SetPreferredDeviceIndex,(int index)) \
    X(int,  EnumerateDevices,       (void)) \
    X(int,  GetDeviceName,          (int index, char* buffer, int size)) \
    X(int,  EnumerateAllDevices,    (void)) \
    X(int,  GetAnyDeviceName,       (int index, char* buffer, int size)) \
    X(int,  OpenAuxDevice,          (int index)) \
    X(int,  ReadAuxButtons,         (unsigned char* buffer, int length)) \
    X(void, CloseAuxDevice,         (void)) \
    X(int,  OpenReadDevice,         (int index)) \
    X(int,  ReadDeviceState,        (int slot, int* axes, unsigned char* buttons, int buttonCount)) \
    X(void, CloseReadDevices,       (void)) \
    X(int,  GetDeviceInfo,          (int index, int* axes, int* buttons)) \
    X(int,  GetAnyDeviceInfo,       (int index, int* axes, int* buttons, int* ffb)) \
    X(void, PanicStop,              (void)) \
    X(void, ZeroForces,             (void)) \
    X(int,  InstallExitGuards,      (void)) \
    X(void, SetHoldTimeoutMs,       (int ms)) \
    X(void, SetLogPath,             (const char* path)) \
    X(int,  GetLastHResult,         (void)) \
    X(int,  GetWheelFfbVersion,     (void)) \
    X(int,  CreatePeriodicEffect,   (int freqHz)) \
    X(int,  UpdatePeriodicEffect,   (int slot, int magnitude, int freqMilliHz)) \
    X(void, ReleasePeriodicEffects, (void))

#define WHEELFFB_TYPEDEF(ret, name, args) typedef ret (__cdecl *WheelFfb_##name##_t) args;
WHEELFFB_API_LIST(WHEELFFB_TYPEDEF)
#undef WHEELFFB_TYPEDEF

typedef struct WheelFfbApi {
    HMODULE module;
#define WHEELFFB_MEMBER(ret, name, args) WheelFfb_##name##_t name;
    WHEELFFB_API_LIST(WHEELFFB_MEMBER)
#undef WHEELFFB_MEMBER
} WheelFfbApi;

/* Loads WheelFfb.dll from `path` (a full path is strongly preferred: a bare
 * name searches the process's DLL path, which for a game means its exe folder
 * and then the system). Returns 1 when the module loaded and every export
 * resolved; 0 otherwise, with whatever did resolve left usable. */
static int WheelFfb_Load(WheelFfbApi* api, const wchar_t* path)
{
    int ok = 1;
    memset(api, 0, sizeof(*api));
    api->module = LoadLibraryW(path ? path : L"WheelFfb.dll");
    if (!api->module) return 0;
#define WHEELFFB_RESOLVE(ret, name, args) \
    api->name = (WheelFfb_##name##_t)GetProcAddress(api->module, #name); if (!api->name) ok = 0;
    WHEELFFB_API_LIST(WHEELFFB_RESOLVE)
#undef WHEELFFB_RESOLVE
    return ok;
}

/* Loads `fileName` from the folder that contains `self` (your own DLL's
 * HMODULE from DllMain) - the game's working directory is not reliable. */
static int WheelFfb_LoadBeside(WheelFfbApi* api, HMODULE self, const wchar_t* fileName)
{
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    wchar_t* slash;
    if (n == 0 || n >= MAX_PATH) { memset(api, 0, sizeof(*api)); return 0; }
    slash = wcsrchr(path, L'\\');
    if (!slash) { memset(api, 0, sizeof(*api)); return 0; }
    slash[1] = 0;
    if (wcslen(path) + wcslen(fileName ? fileName : L"WheelFfb.dll") >= MAX_PATH) { memset(api, 0, sizeof(*api)); return 0; }
    wcscat_s(path, MAX_PATH, fileName ? fileName : L"WheelFfb.dll");
    return WheelFfb_Load(api, path);
}

/* Name of the first export that did not resolve, "module" if the DLL itself
 * did not load, or NULL when everything is present. For the log line. */
static const char* WheelFfb_MissingExport(const WheelFfbApi* api)
{
    if (!api->module) return "module";
#define WHEELFFB_CHECK(ret, name, args) if (!api->name) return #name;
    WHEELFFB_API_LIST(WHEELFFB_CHECK)
#undef WHEELFFB_CHECK
    return NULL;
}

/* Frees the module. Call from your own shutdown path after PanicStop, never
 * from DllMain(DLL_PROCESS_DETACH). */
static void WheelFfb_Unload(WheelFfbApi* api)
{
    if (api->module) FreeLibrary(api->module);
    memset(api, 0, sizeof(*api));
}

#ifdef __cplusplus
}
#endif
#endif /* DBCE_WHEELFFB_H */
