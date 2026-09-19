// Production outermost input hooks against ordinary local functions. No game,
// device, native toolkit or input injection; exercises real x86 trampolines.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
static HWND foreground = reinterpret_cast<HWND>(1);
static HWND FixtureForeground() { return foreground; }
#define GetForegroundWindow FixtureForeground
#include "../../src/wheel_input_gate.cpp"
#include <cassert>
#include <iostream>

static volatile uint32_t nativeNow = 0, nativeEdge = 0;
static volatile uint32_t nativePrevious = 0;
static bool chordRoute = false;
static volatile int rawVolume = 193, rawOldVolume = 171, rawNavigation = 1;
static int reads = 0;
__declspec(noinline) static int __cdecl StockNow(uint32_t mask) { ++reads; return chordRoute ? (nativeNow & mask) == mask : static_cast<int>(nativeNow & mask); }
__declspec(noinline) static int __cdecl StockOn(uint32_t mask) { ++reads; return chordRoute ? (nativeNow & mask) == mask && (nativePrevious & mask) != mask : static_cast<int>(nativeEdge & mask); }
__declspec(noinline) static int __cdecl StockVolume(ADChannel) { ++reads; return rawVolume; }
__declspec(noinline) static int __cdecl StockOld(ADChannel) { ++reads; return rawOldVolume; }
__declspec(noinline) static int __cdecl StockNavigation(ADChannel) { ++reads; return rawNavigation; }

int main()
{
    using namespace WheelInputGuard;
    HWND window = foreground; Game::hWnd_ptr = &window;
    onHook = safetyhook::create_inline(StockOn, On);
    nowHook = safetyhook::create_inline(StockNow, Now);
    volumeHook = safetyhook::create_inline(StockVolume, Volume);
    oldVolumeHook = safetyhook::create_inline(StockOld, OldVolume);
    volumeSwitchHook = safetyhook::create_inline(StockNavigation, VolumeSwitch);
    assert(onHook && nowHook && volumeHook && oldVolumeHook && volumeSwitchHook);
    constexpr uint32_t confirm = 1u << 2, camera = 1u << 18, navigation = 1u << 20;
    nativeNow = nativeEdge = confirm | camera | navigation;
    assert(StockNow(confirm | camera) == static_cast<int>(confirm | camera));
    assert(StockOn(camera) == static_cast<int>(camera)); // Stock ABI returns raw bits, not 1.
    assert(StockVolume(ADChannel::Acceleration) == 193 && StockOld(ADChannel::Brake) == 171);
    chordRoute = true; nativeNow = confirm; nativePrevious = 0;
    assert(!StockNow(confirm | camera) && !StockOn(confirm | camera));
    nativeNow = confirm | camera; nativePrevious = confirm;
    assert(StockNow(confirm | camera) && StockOn(confirm | camera));
    nativePrevious = nativeNow; assert(!StockOn(confirm | camera));
    chordRoute = false; nativeNow = nativeEdge = confirm | camera | navigation;
    for (int reason = 0; reason != 4; ++reason)
    {
        Overlay::WheelSettingsVisible = reason == 0;
        Overlay::IsBindingDialogActive = reason == 1;
        Overlay::IsActive = reason == 2;
        foreground = reinterpret_cast<HWND>(reason == 3 ? 2 : 1);
        const int before = reads;
        assert(!StockOn(UINT32_MAX) && !StockNow(UINT32_MAX));
        assert(!StockVolume(ADChannel::Acceleration) && !StockOld(ADChannel::Brake));
        assert(!StockNavigation(ADChannel::Steering));
        assert(reads == before); // Original dispatch is not run while isolated.
        Overlay::WheelSettingsVisible = Overlay::IsBindingDialogActive = Overlay::IsActive = false;
        foreground = window;
        assert(!StockOn(confirm) && !StockNow(camera | navigation)); // Held through close/refocus.
        nativeNow = nativeEdge = 0;
        assert(!StockNow(confirm)); // Does not accidentally clear unqueried camera/nav latch.
        nativeNow = nativeEdge = camera | navigation;
        assert(!StockNow(camera | navigation));
        nativeNow = nativeEdge = 0;
        assert(!StockNow(UINT32_MAX));
        nativeNow = nativeEdge = confirm | camera | navigation;
        assert(StockOn(confirm | camera | navigation));
        assert(!StockNavigation(ADChannel::Steering));
        rawNavigation = 0; assert(!StockNavigation(ADChannel::Steering));
        rawNavigation = 1; assert(StockNavigation(ADChannel::Steering));
    }
    std::cout << "PASS: actual production x86 outer input trampolines suppress stock/adapter dispatch and current/previous axes for panel, capture, tools and focus; held digital/analog navigation requires release before recovery. No hardware.\n";
}
