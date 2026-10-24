// Exercises the production Update/selection code with fake ABI callbacks.
// No WheelFfb.dll is loaded, no DirectInput device is acquired, no game is run.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
static bool focused = true;
static HWND FixtureForeground() { return focused ? reinterpret_cast<HWND>(1) : nullptr; }
static BOOL FixtureIsWindow(HWND hwnd) { return hwnd == reinterpret_cast<HWND>(1); }
static DWORD FixtureWindowOwner(HWND, DWORD* owner) { *owner = GetCurrentProcessId(); return 1; }
static ULONGLONG recoveryNow = 100;
static ULONGLONG FixtureTick64() { return recoveryNow; }
#define GetForegroundWindow FixtureForeground
#define IsWindow FixtureIsWindow
#define GetWindowThreadProcessId FixtureWindowOwner
#define GetTickCount64 FixtureTick64
#include "../../src/hooks_dinputffb.cpp"
namespace TickDiscovery { void Observe(EVWORK_CAR*, bool) {} void NoteHooks(bool, bool) {} } // inert without an armed window
namespace RemapInject { bool Observe(float, float, int, uint32_t, uint32_t, bool) { return false; } }
#include <cassert>
#include <iostream>
#include <limits>

float VibrationLeftMotor = 0, VibrationRightMotor = 0;
double __cdecl sub_1149C0(unsigned int, int, DWORD*) { return 0; }
Hook::Hook() {}
static GUID steeringGuid = { 0x11111111, 0x2222, 0x3333, {4,5,6,7,8,9,10,11} };
namespace DInputRemap
{
IDirectInputDevice8A* GetPrimaryDevice() { return nullptr; }
bool IsPrimaryInitialized() { return true; }
bool GetPrimaryDeviceGuid(GUID* out) { *out = steeringGuid; return true; }
int GetTelemetryAccel() { return -1; }
int GetTelemetryBrake() { return -1; }
UiSnapshot ReadUiSnapshot() { return {}; }
}
static int outputCalls = 0, initCalls = 0, freeCalls = 0, strictCalls = 0;
static int outputResult = 1, periodicResult = 1, retainedForce = 0, startCalls = 0, auxiliaryCreates = 0;
static int lastResult = static_cast<int>(E_ACCESSDENIED), zeroCalls = 0, nonzeroCalls = 0;
static GUID selected{};
static int __cdecl Output(int x, int y) { assert(y == 0); ++outputCalls; if(x)++nonzeroCalls; if(outputResult)retainedForce=x; return outputResult; }
static int __cdecl Periodic(int, int magnitude, int) { ++outputCalls; if(magnitude)++nonzeroCalls; return periodicResult; }
static void __cdecl Zero() { ++zeroCalls; retainedForce=0; } // Best effort is not an accepted setter.
static void __cdecl Free() { ++freeCalls; retainedForce=0; }
static BOOL __cdecl Start() { ++startCalls; return TRUE; } // Would replay retainedForce unchanged.
static int __cdecl AcceptedInit(int hwnd) { assert(hwnd == 1); ++initCalls; return 1; }
static int __cdecl CreateAuxiliary(int) { ++auxiliaryCreates; return 0; }
static void __cdecl Preferred(const char*) {}
static void __cdecl PreferredIndex(int index) { assert(index == -1); }
static void __cdecl PreferredGuid(const void* value) { selected = *static_cast<const GUID*>(value); }
static void __cdecl Strict(int value) { assert(value == 1); ++strictCalls; }
static int __cdecl Enumerate() { return 0; }
static int __cdecl RefusedInit(int hwnd) { assert(hwnd == 1); ++initCalls; return 0; }
static int __cdecl LastError() { return lastResult; }

int main()
{
    using namespace FFB;
    ConsumerLifecycle::hostVerified = true;
    ConsumerLifecycle::actuatorReadiness = [](void* window) { return window == reinterpret_cast<void*>(1); };
    ConsumerLifecycle::Runtime().SetIdleCallback(FFB::LifecycleIdle);
    HWND hwnd = reinterpret_cast<HWND>(1);
    Game::hWnd_ptr = &hwnd;
    GameState mode = STATE_GAME;
    Game::current_mode = &mode;
    Settings::TelemetryEnabled = false;
    ffbLoaded = true;
    ffb.SetDeviceForcesXY = Output;
    ffb.UpdatePeriodicEffect = Periodic;
    ffb.FreeDirectInput = Free;
    ffb.SetPreferredDevice = Preferred;
    ffb.SetPreferredDeviceIndex = PreferredIndex;
    ffb.SetPreferredDeviceGuid = PreferredGuid;
    ffb.SetStrictDeviceSelection = Strict;
    ffb.EnumerateDevices = Enumerate;
    ffb.InitDirectInput = RefusedInit;
    ffb.GetLastHResult = LastError;
    ffb.ZeroForces = Zero;
    ffb.StartEffect = Start;
    ffb.CreatePeriodicEffect = CreateAuxiliary;
    EVWORK_CAR car{};
    for (int gate = 0; gate < 6; ++gate)
    {
        Settings::DirectInputFFB = gate != 0;
        Overlay::IsActive = gate == 1;
        Overlay::WheelSettingsVisible = gate == 2;
        Overlay::IsBindingDialogActive = gate == 3;
        focused = gate != 4;
        mode = gate == 5 ? STATE_SMPAUSEMENU : STATE_GAME;
        initialized = true;
        prevConstantLevel = 1234;
        periodicsActive = true;
        slotRoadTexture = 0; slotTireSlip = 1;
        const int before = outputCalls;
        Update(&car);
        assert(outputCalls >= before + 3);
        assert(prevConstantLevel == 0 && warmupFrames == 0);
        assert(initCalls == 0);
    }
    // Target switching zeroes first, releases once, and leaves no initialized
    // actuator. No next device is opened merely by choosing a dropdown row.
    SelectionChanged();
    assert(freeCalls == 1 && !initialized && initCalls == 0);
    focused = true;
    mode = STATE_GAME;
    Overlay::IsActive = Overlay::WheelSettingsVisible = Overlay::IsBindingDialogActive = false;
    Settings::UseDirectInputRemap = true;
    Settings::FFBDeviceGuid = "steering";
    Settings::DIRemapDeviceGuid = "auto";
    assert(!DeferredInit() && initCalls == 0);
    assert(deviceError.find("Bind Steering") != std::string::npos);
    initAttempted = false;
    Settings::FFBDeviceGuid = "{AAAAAAAA-BBBB-CCCC-0102-030405060708}";
    assert(!DeferredInit()); // fake driver refusal: exactly one requested device
    assert(initCalls == 1 && strictCalls == 1);
    assert(selected.Data1 == 0xAAAAAAAA); // override wins over the steering wheel
    assert(!initialized && !deviceError.empty());
    initAttempted = false;
    hwnd = nullptr;
    assert(!DeferredInit() && initCalls == 1); // never hand native a zero HWND
    // A retained nonzero effect must be replaced with an accepted zero before
    // startup completes. StartEffect would replay the previous parameters.
    hwnd = reinterpret_cast<HWND>(1);
    ffb.InitDirectInput = AcceptedInit;
    Settings::FFBProfile = "legacy";
    Settings::FFBUsePeriodicEffects = false;
    initAttempted = initialized = false;
    retainedForce = 5000;
    int beforeOutput = outputCalls;
    assert(DeferredInit() && initialized);
    assert(outputCalls == beforeOutput + 1 && retainedForce == 0 && startCalls == 0);
    assert(DeferredInit() && outputCalls == beforeOutput + 1); // no repeat startup
    SelectionChanged();
    Settings::FFBUsePeriodicEffects = true;
    outputResult = 0; retainedForce = 5000;
    int beforeFree = freeCalls;
    assert(!DeferredInit() && !initialized && initAttempted);
    assert(freeCalls == beforeFree + 1 && retainedForce == 0 && auxiliaryCreates == 0);
    assert(slotRoadTexture == -1 && slotTireSlip == -1 && !periodicsActive && startCalls == 0);
    assert(deviceError.find("neutral startup") != std::string::npos);
    beforeOutput = outputCalls;
    assert(!DeferredInit() && outputCalls == beforeOutput); // latched until explicit Refresh
    SelectionChanged(); outputResult = 1; Settings::FFBUsePeriodicEffects = false;
    assert(DeferredInit() && initialized && startCalls == 0); // explicit retry succeeds
    Settings::FFBGlobalStrength = 1;
    SetConstantForce(4321); assert(retainedForce == 4321 && prevConstantLevel == 4321);
    outputResult = 0; beforeFree = freeCalls; beforeOutput = outputCalls;
    SetConstantForce(-1234);
    assert(freeCalls == beforeFree + 1 && !initialized && initAttempted && retainedForce == 0);
    assert(prevConstantLevel == 4321); // Refused command never becomes the deadband cache.
    assert(!initialized); // Output blocked; calculation state is retained through the sample.
    assert(deviceError.find("refused output") != std::string::npos);
    SetConstantForce(2222); UpdatePeriodic(0, 0.5f, 25);
    assert(!DeferredInit() && outputCalls == beforeOutput + 1); // No automatic acquisition/retry.
    SelectionChanged(); outputResult = 1; assert(DeferredInit());
    SetConstantForce(4321); periodicResult = 0; beforeFree = freeCalls;
    UpdatePeriodic(0, 0.5f, 25);
    assert(!initialized && initAttempted && freeCalls == beforeFree + 1 && retainedForce == 0);
    periodicResult = 1; SelectionChanged(); assert(DeferredInit());
    SetConstantForce(4321);
    ConsumerLifecycle::actuatorReadiness = [](void*) { return false; };
    beforeOutput = outputCalls;
    SetConstantForce(2000); UpdatePeriodic(0, 0.5f, 25);
    assert(outputCalls == beforeOutput && retainedForce == 4321);
    periodicsActive = true; slotRoadTexture = 0; slotTireSlip = 1;
    ZeroAllForces();
    assert(outputCalls == beforeOutput + 3 && retainedForce == 0 && prevConstantLevel == 0);
    // A refused zero still releases the held output; it cannot pretend to stop.
    retainedForce = prevConstantLevel = 4321; outputResult = 0; beforeFree = freeCalls;
    ZeroAllForces();
    assert(freeCalls == beforeFree + 1 && retainedForce == 0 && !initialized);
    ConsumerLifecycle::actuatorReadiness = [](void*) { return true; };
    outputResult = 1; SelectionChanged(); assert(DeferredInit());
    beforeOutput = outputCalls; beforeFree = freeCalls;
    Settings::FFBGlobalStrength = std::numeric_limits<float>::quiet_NaN();
    SetConstantForce(1234);
    assert(outputCalls == beforeOutput && freeCalls == beforeFree + 1 && !initialized);
    Settings::FFBGlobalStrength = 1; SelectionChanged(); assert(DeferredInit());
    beforeOutput = outputCalls; beforeFree = freeCalls;
    UpdatePeriodic(0, 0.5f, std::numeric_limits<float>::infinity());
    assert(outputCalls == beforeOutput && freeCalls == beforeFree + 1 && !initialized);
    sharedModel = new dbce::force::Model(sharedProfile.model);
    sharedShaper = new dbce::force::Shaper(sharedProfile.shaper);
    useSharedModel = true;
    auto* retainedModel = sharedModel;
    beforeOutput = outputCalls;
    RefreshUiDevices(false); // Initial FFB-page population must not silently retry.
    assert(initAttempted && sharedModel == retainedModel && !deviceError.empty());
    RefreshUiDevices(); // Explicit action reconciles the failed model before opening again.
    assert(!initAttempted && !initialized && !sharedModel && !sharedShaper && !useSharedModel);
    assert(!periodicsActive && slotRoadTexture == -1 && slotTireSlip == -1 && warmupFrames == 0);
    assert(outputCalls == beforeOutput && deviceError.empty());
    assert(DeferredInit() && initialized && startCalls == 0);
    for (HRESULT transient : std::array<HRESULT, 5>{static_cast<HRESULT>(DIERR_NOTEXCLUSIVEACQUIRED),
            static_cast<HRESULT>(DIERR_INPUTLOST), static_cast<HRESULT>(DIERR_NOTACQUIRED),
            static_cast<HRESULT>(E_HANDLE), static_cast<HRESULT>(DIERR_NOTDOWNLOADED)}) {
        SelectionChanged(); outputResult = periodicResult = 1; assert(DeferredInit());
        Settings::DirectInputFFB = true;
        SetConstantForce(1234);
        periodicsActive = true; slotRoadTexture = 0; slotTireSlip = 1;
        lastResult = static_cast<int>(transient);
        outputResult = periodicResult = 0;
        focused = false;
        ConsumerLifecycle::actuatorReadiness = [](void*) { return false; };
        beforeFree = freeCalls;
        Update(&car); // Native unacquired at focus loss; failed zero is not cached.
        assert(initialized && deliveryRecoveryPending && prevConstantLevel == 1234 && freeCalls == beforeFree);
        recoveryNow += 10000; Update(&car);
        assert(initialized && !deliveryRecoveryClockRunning); // Background time never expires foreground recovery.
        focused = true;
        ConsumerLifecycle::actuatorReadiness = [](void*) { return true; };
        int nonzeroBefore = nonzeroCalls;
        Update(&car); // Acknowledge neutral first, no force or model advance.
        assert(initialized && deliveryRecoveryClockRunning && nonzeroCalls == nonzeroBefore);
        recoveryNow += 1999;
        Update(&car);
        assert(initialized && nonzeroCalls == nonzeroBefore);
        // Focus notification can arrive while the game stops ticking entirely.
        // Exercise both immediate silence and the deferred producer-drain path.
        if (transient == DIERR_NOTACQUIRED) {
            assert(ConsumerLifecycle::Runtime().Pause());
            SilenceForLifecycle();
            ConsumerLifecycle::Runtime().Resume();
            assert(deliveryRecoveryPending && !deliveryRecoveryClockRunning);
            recoveryNow += 10000;
            Update(&car);
            assert(initialized && deliveryRecoveryClockRunning && nonzeroCalls == nonzeroBefore);
            {
                ConsumerLifecycle::Gate::Lease held(ConsumerLifecycle::Runtime());
                assert(!ConsumerLifecycle::Runtime().Pause());
                ConsumerLifecycle::Runtime().Resume();
            }
            assert(deliveryRecoveryPending && !deliveryRecoveryClockRunning);
            recoveryNow += 10000;
            Update(&car);
            assert(initialized && nonzeroCalls == nonzeroBefore);
        }
        outputResult = periodicResult = 1;
        Update(&car);
        assert(initialized && !deliveryRecoveryPending && !deliveryRecoveryClockRunning && deviceError.empty());
        assert(warmupFrames <= 1 && startCalls == 0); // Existing ramp restarts after neutral acknowledgment.
    }
    // An accepted constant zero must not conceal one auxiliary that still fails.
    SetConstantForce(1234); periodicResult = 0; lastResult = DIERR_NOTACQUIRED;
    beforeFree = freeCalls; UpdatePeriodic(0, 0.5f, 25);
    int nonzeroBefore = nonzeroCalls;
    Update(&car);
    const auto began = deliveryRecoveryStart;
    recoveryNow += 1999; Update(&car);
    assert(initialized && deliveryRecoveryPending && deliveryRecoveryStart == began && nonzeroCalls == nonzeroBefore);
    recoveryNow += 1; Update(&car);
    assert(!initialized && initAttempted && freeCalls == beforeFree + 1 && nonzeroCalls == nonzeroBefore);
    assert(!DeferredInit()); // Bounded failure still needs explicit Refresh.
    periodicResult = outputResult = 1; lastResult = static_cast<int>(E_ACCESSDENIED);
    SelectionChanged(); assert(DeferredInit());
    std::cout << "PASS: six production output gates zero constant/periodics before init; zero-before-switch; unbound follow-mode refusal; strict explicit GUID; driver refusal; zero-HWND refusal.\n";
    std::cout << "PASS: accepted-zero startup replaces retained force; refused zero releases and latches without auxiliaries; explicit retry only.\n";
    std::cout << "PASS: refused constant/periodic/zero release retained output without caching or retry; readiness loss still permits zero; nonfinite output settings refused.\n";
    std::cout << "PASS: automatic device listing retains failure latch; explicit Refresh resets retained model without output before neutral reacquisition.\n";
    std::cout << "PASS: transient access/effect loss recovers only after every neutral acknowledgment; no nonzero during recovery; background time excluded; persistent auxiliary failure latches at 2 s.\n";
}
