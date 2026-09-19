#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <atomic>
#include "hook_mgr.hpp"
#include "plugin.hpp"
#include "game_addrs.hpp"
#include "overlay/overlay.hpp"
#include "wheel_input_gate.hpp"

namespace WheelInputGuard
{
static SafetyHookInline onHook, nowHook, volumeHook, oldVolumeHook, volumeSwitchHook;
static WheelInput::DispatchGate gate;
static uint32_t blockedVolumes = 0;
static std::array<std::atomic<bool>, 512> capturedTextKeys{}, textKeysDown{};
static std::atomic<bool> textSuspended = false;
static std::atomic<bool> installed = false;
static std::atomic<uint32_t> diagnosticHeld = 0;
#define DIAGNOSTIC_COUNTERS(X) \
    X(queries) X(uiBlocked) X(releaseBlocked) X(forwarded) \
    X(confirmQueries) X(confirmUiBlocked) X(confirmReleaseBlocked) \
    X(confirmHeldDown) X(confirmForwarded) X(confirmPositive) X(returnKeyDown) X(textBlocked)
#define DECLARE_COUNTER(name) static std::atomic<uint64_t> name = 0;
DIAGNOSTIC_COUNTERS(DECLARE_COUNTER)
#undef DECLARE_COUNTER
static uint32_t BlockingReason()
{
    return ((!Game::hWnd_ptr || GetForegroundWindow() != Game::GameHwnd()) ? 1u : 0u) |
        (Overlay::WheelSettingsVisible ? 2u : 0u) | (Overlay::IsActive ? 4u : 0u) |
        (Overlay::IsBindingDialogActive ? 8u : 0u);
}
static void UpdateTextSuspension(bool blocked)
{
    if (blocked && !textSuspended.exchange(true))
        for (size_t scan = 0; scan < textKeysDown.size(); ++scan)
            if (textKeysDown[scan]) capturedTextKeys[scan] = true;
    if (!blocked) textSuspended = false;
}
bool Suspended()
{
    const bool blocked = BlockingReason() != 0;
    UpdateTextSuspension(blocked);
    if (blocked) { gate.Suspend(); diagnosticHeld = gate.held; blockedVolumes = UINT32_MAX; }
    return blocked;
}
DiagnosticSnapshot Diagnostics()
{
    DiagnosticSnapshot result;
    result.installed = installed.load(); result.reason = BlockingReason(); result.heldMask = diagnosticHeld.load();
#define READ_COUNTER(name) result.name = name.load();
    DIAGNOSTIC_COUNTERS(READ_COUNTER)
#undef READ_COUNTER
    return result;
}
#undef DIAGNOSTIC_COUNTERS

bool SuppressTextMessage(uint32_t message, uintptr_t key, intptr_t detail)
{
    // Stock WndProc handles WM_CHAR independently of SwitchNow/On (licence/name
    // entry). ImGui's Win32 handler returns 0 after consuming that message.
    const auto scan = (static_cast<uintptr_t>(detail) >> 16) & 0x1ff;
    const bool blocked = BlockingReason() != 0 || message == WM_KILLFOCUS;
    UpdateTextSuspension(blocked);
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
    {
        textKeysDown[scan] = true;
        if (key == VK_RETURN) ++returnKeyDown;
        if (blocked) capturedTextKeys[scan] = true;
        else if (!(static_cast<uintptr_t>(detail) & (uintptr_t{1} << 30)))
            capturedTextKeys[scan] = false; // A fresh press, never held-repeat.
    }
    if (message == WM_KEYUP || message == WM_SYSKEYUP) textKeysDown[scan] = false;
    if (message != WM_CHAR) return false; // Preserve lifecycle/system messages.
    if (blocked) capturedTextKeys[scan] = true;
    if (!capturedTextKeys[scan]) return false;
    ++textBlocked;
    return true;
}

// Install after every input adapter. These trampolines call whichever route
// the game was configured to use (stock, SDL or DirectInput), never change it.
static int Query(uint32_t requested, bool edge)
{
    ++queries;
    const bool confirm = edge && requested == (1u << static_cast<int>(SwitchId::A));
    if (confirm) ++confirmQueries;
    if (Suspended()) { ++uiBlocked; if (confirm) ++confirmUiBlocked; return 0; }
    if (gate.held & requested)
    {
        uint32_t now = 0;
        for (int bit = 0; bit < 32; ++bit)
        {
            const uint32_t mask = uint32_t{1} << bit;
            if ((requested & mask) && nowHook.ccall<int>(mask)) now |= mask;
        }
        gate.Filter(requested, now, 0, false);
        diagnosticHeld = gate.held;
        if (confirm && now) ++confirmHeldDown;
        if (gate.held & requested) { ++releaseBlocked; if (confirm) ++confirmReleaseBlocked; return 0; }
    }
    // Preserve the original route's complete-mask semantics: SDL requires all
    // bits/chord edges, while the stock/remap routes accept any requested bit.
    const int result = edge ? onHook.ccall<int>(requested) : nowHook.ccall<int>(requested);
    ++forwarded;
    if (confirm) { ++confirmForwarded; if (result) ++confirmPositive; }
    return result;
}
static int __cdecl On(uint32_t switches) { return Query(switches, true); }
static int __cdecl Now(uint32_t switches) { return Query(switches, false); }
static int __cdecl Volume(ADChannel channel) { return Suspended() ? 0 : volumeHook.ccall<int>(channel); }
static int __cdecl OldVolume(ADChannel channel) { return Suspended() ? 0 : oldVolumeHook.ccall<int>(channel); }
static int __cdecl VolumeSwitch(ADChannel channel)
{
    // VolumeSwitch produces menu navigation from held analog controls. Keep
    // each channel released after a panel/focus transition as well.
    const auto index = static_cast<unsigned>(channel);
    const uint32_t mask = index < 32 ? uint32_t{1} << index : 0;
    if (Suspended()) return 0;
    const int result = volumeSwitchHook.ccall<int>(channel);
    if (!result) blockedVolumes &= ~mask;
    return blockedVolumes & mask ? 0 : result;
}
bool Install()
{
    onHook = safetyhook::create_inline(Module::exe_ptr(0x536F0), On);
    nowHook = safetyhook::create_inline(Module::exe_ptr(0x536C0), Now);
    volumeHook = safetyhook::create_inline(Module::exe_ptr(0x53720), Volume);
    oldVolumeHook = safetyhook::create_inline(Module::exe_ptr(0x53750), OldVolume);
    volumeSwitchHook = safetyhook::create_inline(Module::exe_ptr(0x53780), VolumeSwitch);
    const bool ready = onHook && nowHook && volumeHook && oldVolumeHook && volumeSwitchHook;
    installed = ready;
    if (!ready)
    {
        onHook = {}; nowHook = {}; volumeHook = {}; oldVolumeHook = {}; volumeSwitchHook = {};
        spdlog::error("Wheel settings input isolation hooks failed; UI adoption is incomplete");
    }
    return ready;
}
}
