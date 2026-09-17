// Exercises the production Update/selection code with fake ABI callbacks.
// No WheelFfb.dll is loaded, no DirectInput device is acquired, no game is run.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
static bool focused = true;
static HWND FixtureForeground() { return focused ? reinterpret_cast<HWND>(1) : nullptr; }
static BOOL FixtureIsWindow(HWND hwnd) { return hwnd == reinterpret_cast<HWND>(1); }
static DWORD FixtureWindowOwner(HWND, DWORD* owner) { *owner = GetCurrentProcessId(); return 1; }
#define GetForegroundWindow FixtureForeground
#define IsWindow FixtureIsWindow
#define GetWindowThreadProcessId FixtureWindowOwner
#include "../../src/hooks_dinputffb.cpp"
#include <cassert>
#include <iostream>

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
static GUID selected{};
static int __cdecl Output(int x, int y) { assert(x == 0 && y == 0); ++outputCalls; return 1; }
static int __cdecl Periodic(int, int magnitude, int) { assert(magnitude == 0); ++outputCalls; return 1; }
static void __cdecl Free() { ++freeCalls; }
static void __cdecl Preferred(const char*) {}
static void __cdecl PreferredIndex(int index) { assert(index == -1); }
static void __cdecl PreferredGuid(const void* value) { selected = *static_cast<const GUID*>(value); }
static void __cdecl Strict(int value) { assert(value == 1); ++strictCalls; }
static int __cdecl Enumerate() { return 0; }
static int __cdecl RefusedInit(int hwnd) { assert(hwnd == 1); ++initCalls; return 0; }
static int __cdecl LastError() { return static_cast<int>(0x80070005); }

int main()
{
    using namespace FFB;
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
    std::cout << "PASS: six production output gates zero constant/periodics before init; zero-before-switch; unbound follow-mode refusal; strict explicit GUID; driver refusal; zero-HWND refusal.\n";
}
