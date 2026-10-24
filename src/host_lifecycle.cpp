#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <wincrypt.h>
#include <commctrl.h>
#include <array>
#include <cstdio>
#include "hook_mgr.hpp"
#include "plugin.hpp"
#include "game_addrs.hpp"
#include "Proxy.hpp"
#include "consumer_lifecycle.hpp"
#include "host_lifecycle_policy.hpp"
#include "upnp.hpp"
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Comctl32.lib")

namespace FFB { void FinalizeForExit(); void SilenceForLifecycle(); void LifecycleIdle(); void FinalizeSignalCapture() noexcept; }
namespace DInputRemap { void FinalizeForExit(); }
namespace TickDiscovery { void FinalizeForExit(); }
void InputManager_FinalizeForExit();
namespace OutRunLifecycle {
static unsigned char* image = nullptr;
static std::atomic<bool> loopEntered = false, hooksInstalled = false;
static bool closeRequested = false, sessionRequested = false;
static HWND guardedWindow = nullptr;
static DWORD ownerThread = 0;
static constexpr UINT_PTR SubclassId = 0x0FFB;
// Heap-owned hook objects intentionally survive process detach, like the pinned
// modules. Their destructors must not patch/unhook under the loader lock.
static SafetyHookMid *bootstrapHook = nullptr, *loopHook = nullptr, *cleanupHook = nullptr, *continuationHook = nullptr;
static std::vector<unsigned char> bootstrapPatch, loopPatch, cleanupPatch, continuationPatch;

static bool Readable(const void* pointer, std::size_t count) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(pointer, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return reinterpret_cast<std::uintptr_t>(pointer)+count <= reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
}
static bool BytesMatch(std::uint32_t rva, const std::vector<unsigned char>& expected) {
    return image && Readable(image+rva, expected.size()) && std::memcmp(image+rva, expected.data(), expected.size()) == 0;
}
static bool LoadedIdentityMatches() {
    if (!image) return false;
    for (const auto& signature : CriticalSignatures()) {
        const auto* installed = signature.rva == BootstrapRva ? &bootstrapPatch :
            signature.rva == LoopCallRva ? &loopPatch : signature.rva == CleanupCallRva ? &cleanupPatch :
            signature.rva == 0x17E10 ? &continuationPatch : nullptr;
        const auto expected = installed && !installed->empty() ? *installed :
            RelocatedBytes(signature, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image)));
        if (!BytesMatch(signature.rva, expected)) return false;
    }
    return true;
}
static bool ExactDiskBuild() {
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr, path, _countof(path))) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    HCRYPTPROV provider = 0; HCRYPTHASH hash = 0;
    bool ok = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash);
    std::array<BYTE, 65536> buffer{}; DWORD read = 0;
    while (ok) {
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) { ok = false; break; }
        if (!read) break;
        ok = CryptHashData(hash, buffer.data(), read, 0) != FALSE;
    }
    BYTE digest[32]{}; DWORD length = sizeof(digest);
    if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0) && length == 32;
    char text[65]{};
    if (ok) for (int i=0; i<32; ++i) std::snprintf(text+i*2, 3, "%02x", digest[i]);
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    CloseHandle(file);
    return ok && DiskIdentityMatches(text);
}
static bool Pin(void* address) {
    HMODULE retained = nullptr;
    return address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(address), &retained);
}
static void FinalizeAtOuterBoundary() {
    auto& gate = ConsumerLifecycle::Runtime();
    if (!gate.ClaimFinalization()) return;
    FFB::FinalizeForExit();
    DInputRemap::FinalizeForExit();
    InputManager_FinalizeForExit();
    gate.CompleteFinalization();
    FFB::FinalizeSignalCapture(); // physical cleanup and lease drain always precede capture disk work
    try { UPnP::shutdown(); } catch (...) {} // Router I/O must finish outside loader lock, without blocking game cleanup on failure.
    // Read-only discovery last: outputs are silenced and the gate is complete before any disk work, and nothing from
    // it may escape into the game's exit path.
    try { TickDiscovery::FinalizeForExit(); } catch (...) {}
}
static bool QuitPending() {
    MSG message{};
    return PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_NOREMOVE) ||
        (image && *reinterpret_cast<const DWORD*>(image+0x4A8CAC) != 0);
}
static void PauseAndSilence() {
    if (ConsumerLifecycle::Runtime().Pause()) FFB::SilenceForLifecycle();
}
static LRESULT CALLBACK Guard(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (message == WM_CLOSE) { closeRequested = true; PauseAndSilence(); }
    if (message == WM_QUERYENDSESSION) { sessionRequested = true; PauseAndSilence(); }
    if (message == WM_ACTIVATEAPP && !wp &&
        ConsumerLifecycle::Runtime().Current() == ConsumerLifecycle::Gate::Phase::Running) {
        // Keep the existing immediate focus-loss silence. A reentrant request
        // defers both zero and resume until its producer lease completes.
        PauseAndSilence();
        ConsumerLifecycle::Runtime().Resume();
    }
    if (message == WM_ENDSESSION && wp) ConsumerLifecycle::Runtime().RequestStop();
    if (message == WM_DESTROY) ConsumerLifecycle::Runtime().RequestStop();
    const LRESULT result = DefSubclassProc(hwnd, message, wp, lp);
    if (message == WM_ENDSESSION && !wp) {
        sessionRequested = false;
        if (!closeRequested) ConsumerLifecycle::Runtime().Resume();
    }
    if (message == WM_NCDESTROY) { RemoveWindowSubclass(hwnd, Guard, SubclassId); guardedWindow = nullptr; }
    return result;
}
static bool ActuatorReady(void* window) {
    const auto hwnd = static_cast<HWND>(window);
    DWORD process = 0;
    if (!hooksInstalled || !loopEntered || !LoadedIdentityMatches() || !hwnd || !IsWindow(hwnd) ||
        GetWindowThreadProcessId(hwnd, &process) != GetCurrentThreadId() || process != GetCurrentProcessId()) return false;
    if (!guardedWindow) {
        if (!SetWindowSubclass(hwnd, Guard, SubclassId, 0)) return false;
        guardedWindow = hwnd; ownerThread = GetCurrentThreadId();
    }
    return guardedWindow == hwnd && ownerThread == GetCurrentThreadId();
}
static void CleanupBoundary(SafetyHookContext&) {
    // Exact callsite is reached only after the synchronous outer loop returns;
    // no lease is held here. SafetyHook preserves context and relocates the
    // displaced CALL, so game cleanup still executes exactly once afterward.
    FinalizeAtOuterBoundary();
}
static void LoopBoundary(SafetyHookContext&) { loopEntered = ConsumerLifecycle::hostVerified.load() && LoadedIdentityMatches(); }
static void ContinuingBoundary(SafetyHookContext&) {
    // The inspected loop reaches this after dispatch returns. A close consumed
    // by another handler can recover here, before the displaced exit-flag test.
    if (closeRequested && !sessionRequested && hooksInstalled && loopEntered &&
        GetCurrentThreadId() == ownerThread && LoadedIdentityMatches() && !QuitPending()) {
        closeRequested = false;
        ConsumerLifecycle::Runtime().Resume();
    }
}
static void Bootstrap(SafetyHookContext&) {
    // Called from exact WinMain-shaped runtime entry, not Plugin_Init/DllMain.
    ConsumerLifecycle::hostVerified = false;
    if (!hooksInstalled || !ExactDiskBuild() || !LoadedIdentityMatches() || !Pin(Module::DllHandle) || !Pin(proxy::origModule)) return;
    ConsumerLifecycle::Runtime().SetIdleCallback(FFB::LifecycleIdle);
    ConsumerLifecycle::actuatorReadiness = ActuatorReady;
    ConsumerLifecycle::hostVerified = true;
}
class HostLifecycleHook final : public Hook {
public:
    std::string_view description() override { return "ExactBuildHostLifecycle"; }
    bool validate() override { return true; }
    bool apply() override {
        hooksInstalled = false;
        ConsumerLifecycle::hostVerified = false;
        image = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
        if (!LoadedIdentityMatches()) return false;
        // Disk verification and module pinning are deferred to Bootstrap;
        // neither actuator loading nor cleanup occurs during hook registration.
        bootstrapHook = new SafetyHookMid(safetyhook::create_mid(image+BootstrapRva, Bootstrap));
        if (!*bootstrapHook) return false;
        bootstrapPatch.assign(image+BootstrapRva, image+BootstrapRva+10);
        loopHook = new SafetyHookMid(safetyhook::create_mid(image+LoopCallRva, LoopBoundary));
        if (!*loopHook) return false;
        loopPatch.assign(image+LoopCallRva, image+LoopCallRva+5);
        cleanupHook = new SafetyHookMid(safetyhook::create_mid(image+CleanupCallRva, CleanupBoundary));
        if (!*cleanupHook) return false;
        cleanupPatch.assign(image+CleanupCallRva, image+CleanupCallRva+5);
        continuationHook = new SafetyHookMid(safetyhook::create_mid(image+0x17E10, ContinuingBoundary));
        if (!*continuationHook) return false;
        continuationPatch.assign(image+0x17E10, image+0x17E10+12);
        hooksInstalled = LoadedIdentityMatches();
        return hooksInstalled;
    }
};
static HostLifecycleHook instance;
}
