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
    installed = true;
    constexpr uint32_t confirm = 1u << 2, camera = 1u << 18, navigation = 1u << 20;
    nativeNow = nativeEdge = confirm | camera | navigation;
    assert(StockNow(confirm | camera) == static_cast<int>(confirm | camera));
    assert(StockOn(camera) == static_cast<int>(camera)); // Stock ABI returns raw bits, not 1.
    assert(StockOn(confirm) == static_cast<int>(confirm));
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
        assert(!StockOn(confirm));
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
    // Diagnostics are observational: no original route calls or latch changes.
    const int beforeDiagnostics = reads;
    const auto heldBeforeDiagnostics = gate.held;
    const auto d = Diagnostics();
    assert(d.installed && d.queries && d.forwarded && d.uiBlocked && d.releaseBlocked);
    assert(d.confirmQueries && d.confirmForwarded && d.confirmUiBlocked && d.confirmReleaseBlocked && d.confirmHeldDown);
    assert(reads == beforeDiagnostics && gate.held == heldBeforeDiagnostics);
    // Native licence/name entry bypasses game switches through WM_CHAR.
    constexpr intptr_t keyScan = intptr_t{0x1e} << 16;
    constexpr intptr_t repeat = intptr_t{1} << 30;
    assert(!SuppressTextMessage(WM_KEYDOWN, 'A', keyScan));
    assert(!SuppressTextMessage(WM_CHAR, 'a', keyScan));
    Overlay::WheelSettingsVisible = true;
    Suspended(); // Capture an already held key even before another message.
    assert(SuppressTextMessage(WM_CHAR, 'a', keyScan));
    assert(!SuppressTextMessage(WM_CLOSE, 0, 0));
    assert(!SuppressTextMessage(WM_QUERYENDSESSION, 0, 0));
    assert(!SuppressTextMessage(WM_SYSKEYDOWN, VK_F4, intptr_t{0x3e} << 16));
    Overlay::WheelSettingsVisible = false;
    assert(!SuppressTextMessage(WM_KEYDOWN, 'A', keyScan | repeat));
    assert(SuppressTextMessage(WM_CHAR, 'a', keyScan | repeat));
    assert(!SuppressTextMessage(WM_KEYUP, 'A', keyScan | repeat));
    assert(SuppressTextMessage(WM_CHAR, 'a', keyScan)); // Queued old char cannot replay on close.
    assert(!SuppressTextMessage(WM_KEYDOWN, 'A', keyScan));
    assert(!SuppressTextMessage(WM_CHAR, 'a', keyScan));
    foreground = reinterpret_cast<HWND>(2);
    assert(SuppressTextMessage(WM_CHAR, 'a', keyScan));
    foreground = window;
    assert(SuppressTextMessage(WM_CHAR, 'a', keyScan));
    assert(!SuppressTextMessage(WM_KEYUP, 'A', keyScan | repeat));
    assert(!SuppressTextMessage(WM_KEYDOWN, 'A', keyScan));
    assert(!SuppressTextMessage(WM_CHAR, 'a', keyScan));
    assert(!SuppressTextMessage(WM_KEYDOWN, VK_RETURN, intptr_t{0x1c} << 16));
    assert(Diagnostics().returnKeyDown == 1 && Diagnostics().textBlocked >= 5);
    assert(reads == beforeDiagnostics); // Message guard never polls native input.
    std::cout << "PASS: actual production x86 input dispatch, aggregate native/SDL semantics, held release, observational diagnostics and native WM_CHAR suppression through UI/focus/held-repeat/queued-character recovery. No hardware.\n";
}
