#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
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
bool Suspended()
{
    const bool blocked = Overlay::IsActive || Overlay::WheelSettingsVisible || Overlay::IsBindingDialogActive ||
        !Game::hWnd_ptr || GetForegroundWindow() != Game::GameHwnd();
    if (blocked) { gate.Suspend(); blockedVolumes = UINT32_MAX; }
    return blocked;
}

// Install after every input adapter. These trampolines call whichever route
// the game was configured to use (stock, SDL or DirectInput), never change it.
static int Query(uint32_t requested, bool edge)
{
    if (Suspended()) { gate.Suspend(); return 0; }
    if (gate.held & requested)
    {
        uint32_t now = 0;
        for (int bit = 0; bit < 32; ++bit)
        {
            const uint32_t mask = uint32_t{1} << bit;
            if ((requested & mask) && nowHook.ccall<int>(mask)) now |= mask;
        }
        gate.Filter(requested, now, 0, false);
        if (gate.held & requested) return 0;
    }
    // Preserve the original route's complete-mask semantics: SDL requires all
    // bits/chord edges, while the stock/remap routes accept any requested bit.
    return edge ? onHook.ccall<int>(requested) : nowHook.ccall<int>(requested);
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
    if (!ready)
    {
        onHook = {}; nowHook = {}; volumeHook = {}; oldVolumeHook = {}; volumeSwitchHook = {};
        spdlog::error("Wheel settings input isolation hooks failed; UI adoption is incomplete");
    }
    return ready;
}
}
