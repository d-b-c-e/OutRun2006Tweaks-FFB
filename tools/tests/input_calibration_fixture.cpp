// Executes production switch merging and axis readers against memory-only states.
// Never enumerates, acquires a device, installs hooks or starts the game.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
static DWORD fixtureTick = 1000;
static bool fixtureReturn = false;
static DWORD FixtureTick() { return fixtureTick; }
static SHORT FixtureKey(int key) { return key == VK_RETURN && fixtureReturn ? SHORT(0x8000) : 0; }
#define GetTickCount FixtureTick
#define GetAsyncKeyState FixtureKey
#include "../../src/hooks_inputremap.cpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace Settings
{
Setting<bool> UseNewInput{ "Controls", "UseNewInput", false };
Setting<float> SteeringDeadZone{ "Controls", "SteeringDeadZone", 0.2f };
bool read_legacy(const std::filesystem::path&) { return true; }
}
void CDSwitcher_ReadIni(const std::filesystem::path&) {}

IDirectInput8A* g_RealDirectInput8 = nullptr;
Hook::Hook() {}
namespace RemapInject {
bool Armed() { return false; }
void Poll() { std::abort(); }
int Deliver(const GUID&, IDirectInputDevice8A*, DIJOYSTATE2&) { std::abort(); }
void DeviceGone(const GUID&) {}
int NoteVolume(Volume, int value) { return value; }
}
static int consumerReleases = 0, consumerUnacquires = 0, borrowedCalls = 0;
static ULONG __stdcall FakeRelease(void*) { ++consumerReleases; return 0; }
static HRESULT __stdcall FakeUnacquire(void*) { ++consumerUnacquires; return S_OK; }
static ULONG __stdcall BorrowedRelease(void*) { ++borrowedCalls; return 0; }
static DIJOYSTATE2 polledState{};
static int stateReads = 0;
static HRESULT __stdcall FakePoll(void*) { return S_OK; }
static HRESULT __stdcall FakeState(void*, DWORD size, void* state) {
    assert(size == sizeof(polledState)); ++stateReads;
    std::memcpy(state, &polledState, size); return S_OK;
}
static void RequireInput(bool pass, const char* message) {
    if (!pass) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
// Real selection/InitSlot paths, with local COM stubs only. These device types
// were observed together on the rig; product names alone did not identify them.
struct SelectionDevice {
    void** table;
    DIDEVICEINSTANCEA info{};
    DIDEVCAPS caps{};
    bool capsOk = true;
};
static std::vector<SelectionDevice*> selectionDevices;
static int selectionEnums = 0, selectionCreates = 0;
static ULONG __stdcall SelectionRelease(void*) { return 0; }
static HRESULT __stdcall SelectionCaps(SelectionDevice* self, DIDEVCAPS* caps) {
    if (!self->capsOk) return E_FAIL;
    *caps = self->caps; return S_OK;
}
static HRESULT __stdcall SelectionInfo(SelectionDevice* self, DIDEVICEINSTANCEA* info) {
    *info = self->info; return S_OK;
}
static HRESULT __stdcall SelectionFormat(void*, const DIDATAFORMAT*) { return S_OK; }
static HRESULT __stdcall SelectionCoop(void*, HWND, DWORD) { return S_OK; }
static HRESULT __stdcall SelectionObjects(void*, LPDIENUMDEVICEOBJECTSCALLBACKA, void*, DWORD) { return S_OK; }
static HRESULT __stdcall SelectionAcquire(void*) { return S_OK; }
static HRESULT __stdcall SelectionCreate(void*, REFGUID guid, IDirectInputDevice8A** device, IUnknown*) {
    ++selectionCreates;
    for (auto* entry : selectionDevices) if (IsEqualGUID(guid, entry->info.guidInstance)) {
        *device = reinterpret_cast<IDirectInputDevice8A*>(entry); return S_OK;
    }
    *device = nullptr; return DIERR_DEVICENOTREG;
}
static HRESULT __stdcall SelectionEnum(void*, DWORD, LPDIENUMDEVICESCALLBACKA callback, void* context, DWORD) {
    ++selectionEnums;
    for (auto* entry : selectionDevices) if (!callback(&entry->info, context)) break;
    return S_OK;
}
static void TestAutomaticPrimarySelection()
{
    using namespace DInputRemap;
    void* deviceTable[32]{};
    deviceTable[2] = reinterpret_cast<void*>(SelectionRelease);
    deviceTable[3] = reinterpret_cast<void*>(SelectionCaps);
    deviceTable[4] = reinterpret_cast<void*>(SelectionObjects);
    deviceTable[7] = reinterpret_cast<void*>(SelectionAcquire);
    deviceTable[11] = reinterpret_cast<void*>(SelectionFormat);
    deviceTable[13] = reinterpret_cast<void*>(SelectionCoop);
    deviceTable[15] = reinterpret_cast<void*>(SelectionInfo);
    auto make = [&](DWORD id, const char* name, DWORD type, DWORD axes, bool ffb) {
        SelectionDevice d{}; d.table = deviceTable;
        d.info.dwSize = sizeof(d.info); d.info.guidInstance.Data1 = id; d.info.dwDevType = type;
        strcpy_s(d.info.tszInstanceName, name);
        d.caps.dwSize = sizeof(d.caps); d.caps.dwAxes = axes;
        d.caps.dwFlags = ffb ? DIDC_FORCEFEEDBACK : 0;
        return d;
    };
    auto pad = make(1, "Controller (TS-UFB01B-X)", 0x00010215, 8, true);
    auto wheel = make(2, "MOZA R12 Base", 0x00010318, 3, true);
    auto shifter = make(3, "DS-8X Shifter", 0x0001021c, 8, true);
    auto conventional = make(4, "Wheel", DI8DEVTYPE_DRIVING, 2, false);
    auto noAxes = make(5, "Button-only controller", DI8DEVTYPE_JOYSTICK, 0, true);
    auto unreadable = make(6, "Unavailable", DI8DEVTYPE_1STPERSON, 8, true); unreadable.capsOk = false;
    auto virtualJoystick = make(7, "vJoy", DI8DEVTYPE_JOYSTICK, 8, true);
    void* diTable[11]{};
    diTable[3] = reinterpret_cast<void*>(SelectionCreate);
    diTable[4] = reinterpret_cast<void*>(SelectionEnum);
    void** diObject = diTable;
    auto* di = reinterpret_cast<IDirectInput8A*>(&diObject);
    HWND window = reinterpret_cast<HWND>(1);
    const auto oldWindow = Game::hWnd_ptr; Game::hWnd_ptr = &window;
    auto choose = [&](const std::string& identity, bool isPrimary = true) {
        DeviceSlot slot{}; openedGuids.clear(); primaryGuidValid = false;
        const bool ok = InitSlot(slot, identity, "Fixture", di, isPrimary);
        return ok ? slot.guid.Data1 : DWORD(0);
    };
    selectionDevices = {&pad, &shifter, &wheel, &noAxes, &unreadable, &virtualJoystick};
    RequireInput(choose("auto") == 2, "primary auto must skip gamepad/supplemental and retain the first-person R12");
    std::reverse(selectionDevices.begin(), selectionDevices.end());
    RequireInput(choose("auto") == 2, "wheel selection must not depend on pad enumeration order");
    selectionDevices = {&pad, &shifter, &noAxes, &unreadable, &virtualJoystick};
    RequireInput(choose("auto") == 0, "wheel absent must not substitute gamepad, supplemental, no-axis or failed caps");
    selectionDevices.push_back(&conventional);
    RequireInput(choose("") == 4, "non-FFB driving wheels remain eligible");
    selectionDevices = {&pad, &shifter, &virtualJoystick};
    for (auto* explicitDevice : selectionDevices) {
        const auto enumerations = selectionEnums;
        RequireInput(choose(GuidText(explicitDevice->info.guidInstance)) == explicitDevice->info.guidInstance.Data1,
            "explicit primary GUID must bypass auto filters, including pad/supplemental/virtual names");
        RequireInput(selectionEnums == enumerations, "explicit primary must not enumerate replacements");
    }
    const auto creates = selectionCreates, enumerations = selectionEnums;
    RequireInput(choose("invalid") == 0 && selectionCreates == creates && selectionEnums == enumerations,
        "malformed saved identity must not fall back to auto");
    selectionDevices = {&shifter};
    RequireInput(choose("auto", false) == 3, "optional auto slots must retain supplemental controls");
    selectionDevices.clear(); openedGuids.clear(); primaryGuidValid = false; Game::hWnd_ptr = oldWindow;
    std::cout << "PASS: production primary auto selection rejects pads/supplemental without rejecting the first-person R12; no-wheel/caps failure/order, non-FFB wheel, explicit GUID and optional-slot cases. Fake COM only.\n";
}
static void TestPollingEdges()
{
    using namespace DInputRemap;
    constexpr uint32_t a = 1u << static_cast<int>(SwitchId::A);
    constexpr uint32_t right = 1u << static_cast<int>(SwitchId::SelectionRight);
    void* table[32]{}; table[9] = reinterpret_cast<void*>(FakeState); table[25] = reinterpret_cast<void*>(FakePoll);
    void** object = table;
    primary = {};
    primary.device = reinterpret_cast<IDirectInputDevice8A*>(&object);
    primary.initialized = primary.connected = true;
    for (auto& pov : polledState.rgdwPOV) pov = 0xffffffff;
    Settings::DIShifterDeviceGuid.clear(); Settings::DIAuxDeviceGuid.clear();
    Settings::DIRemapAccelDeviceGuid.clear(); Settings::DIRemapBrakeDeviceGuid.clear();
    Settings::DIShifterGearMode = "hpattern";
    Settings::DIShifterDeviceGuid = "{11111111-2222-3333-0405-060708090A0B}";
    ParseGuid(Settings::DIShifterDeviceGuid, primary.guid);
    primaryGuid = primary.guid; primaryGuidValid = true;
    GameState mode = STATE_TITLE;
    const auto oldMode = Game::current_mode; Game::current_mode = &mode;
    Settings::UseDirectInputRemap = true; Settings::UseNewInput = false;
    hpattern.cooldownFrames = 6;
    polledState.rgbButtons[0] = polledState.rgbButtons[7] = 0x80;
    Settings::DIRemapButtonA = 31; Settings::DIRemapButtonStart = 34;
    Poll();
    RequireInput(!IsButtonPressedAny(SwitchId::A) && !IsButtonPressedAny(SwitchId::Start),
        "owner wheel bindings must not silently reinterpret another controller's buttons 0/7");
    Settings::DIRemapButtonA = 0; Settings::DIRemapButtonStart = 7;
    RequireInput(IsButtonPressedAny(SwitchId::A) && IsButtonPressedAny(SwitchId::Start),
        "explicit diagnostic bindings must recognize observed buttons 0/7");
    RequireInput(BuildSwitchOnMask() & a, "first button edge must be present");
    const auto ui = ReadUiSnapshot();
    RequireInput(ui.connected && ui.buttons[0] && (BuildSwitchOnMask() & a),
        "reading the settings snapshot must not consume gameplay button edges");
    ++fixtureTick;
    Poll();
    RequireInput(BuildSwitchOnMask() & a, "elapsed milliseconds inside one update must not erase a button edge");
    RequireInput(stateReads == 1, "input queries in one update must share one hardware snapshot");
    RequireInput(hpattern.cooldownFrames == 5, "shifter cooldown must advance once per update");
    BeginInputTick(); Poll();
    RequireInput(stateReads == 2 && !(BuildSwitchOnMask() & a) && (BuildSwitchMask() & a),
        "next update at the same wall time must read held state without repeating its edge");
    RequireInput(hpattern.cooldownFrames == 4, "catch-up update must advance shifter cooldown");
    polledState.rgbButtons[0] = polledState.rgbButtons[7] = 0;
    BeginInputTick(); Poll();
    RequireInput(!(BuildSwitchOnMask() & a) && !(BuildSwitchMask() & a), "release must clear held and edge state");
    polledState.rgdwPOV[0] = 9000;
    BeginInputTick(); Poll();
    RequireInput(BuildSwitchOnMask() & right, "POV edge must be available");
    ++fixtureTick; ReadUiSnapshot(); Poll();
    RequireInput(BuildSwitchOnMask() & right, "UI/axis reads must not consume a POV edge");
    BeginInputTick(); Poll();
    RequireInput(!(BuildSwitchOnMask() & right) && (BuildSwitchMask() & right), "held POV must not repeat its edge");
    fixtureReturn = true;
    BeginInputTick(); Poll();
    RequireInput((BuildSwitchOnMask() & a) && (BuildSwitchMask() & a), "keyboard edge and held state must share snapshot");
    ++fixtureTick;
    RequireInput(BuildSwitchOnMask() & a, "wall time must not consume a keyboard edge");
    fixtureReturn = false; Poll();
    RequireInput(BuildSwitchMask() & a, "keyboard must remain stable until next input update");
    BeginInputTick(); Poll();
    RequireInput(!(BuildSwitchOnMask() & a) && !(BuildSwitchMask() & a), "next update must observe key release");
    const auto token = inputTick;
    RequireInput(ConsumerLifecycle::Runtime().Pause(), "idle input fixture can pause");
    BeginInputTick(); Poll();
    RequireInput(inputTick == token, "paused lifecycle must not advance input");
    ConsumerLifecycle::Runtime().Resume();
    const auto readsBeforeResume = stateReads;
    BeginInputTick(); Poll();
    RequireInput(stateReads == readsBeforeResume + 1, "resumed input must refresh on next update");
    polledState.rgbButtons[127] = 0x80;
    for (int hat = 0; hat < 4; ++hat) {
        for (int direction = 0; direction < 4; ++direction) {
            for (auto& pov : polledState.rgdwPOV) pov = 0xffffffff;
            polledState.rgdwPOV[hat] = direction * 9000;
            BeginInputTick();
            const auto snapshot = ReadUiSnapshot();
            const int binding = PovBinding::Encode(hat, direction * 9000);
            RequireInput(binding < int(snapshot.buttons.size()) && snapshot.buttons[binding],
                "POV direction must reach the UI capture snapshot");
            RequireInput(snapshot.buttons[127], "POV snapshot must retain physical button 128");
            for (int i = PovBinding::Base; i <= PovBinding::Last; ++i)
                RequireInput(snapshot.buttons[i] == (i == binding), "POV snapshot retained another direction");
        }
        polledState.rgdwPOV[hat] = 4500;
        BeginInputTick();
        const auto diagonal = ReadUiSnapshot();
        RequireInput(diagonal.buttons[PovBinding::Encode(hat, 0)] && diagonal.buttons[PovBinding::Encode(hat, 9000)],
            "diagonal must expose both directions so capture can refuse ambiguity");
        polledState.rgdwPOV[hat] = 0xffff;
        BeginInputTick();
        const auto centered = ReadUiSnapshot();
        for (int i = PovBinding::Base; i <= PovBinding::Last; ++i)
            RequireInput(!centered.buttons[i], "centered POV must release capture directions");
    }
    primary = {};
    primaryGuidValid = false; Settings::DIShifterDeviceGuid.clear();
    Settings::DIShifterGearMode = "sequential"; Game::current_mode = oldMode;
    std::cout << "PASS: observed 0/7 versus saved 31/34 bindings; production button/POV/keyboard edges stable across queries and UI reads; held/release, same-millisecond catch-up, shifter cooldown and pause/resume. Fake devices and keyboard only.\n";
}

static void TestSwitchQueries()
{
    constexpr uint32_t a = 1u << static_cast<int>(SwitchId::A);
    constexpr uint32_t start = 1u << static_cast<int>(SwitchId::Start);
    constexpr uint32_t camera = 1u << static_cast<int>(SwitchId::ChangeView);
    constexpr uint32_t up = 1u << static_cast<int>(SwitchId::SelectionUp);
    constexpr uint32_t down = 1u << static_cast<int>(SwitchId::SelectionDown);
    constexpr uint32_t left = 1u << static_cast<int>(SwitchId::SelectionLeft);
    constexpr uint32_t right = 1u << static_cast<int>(SwitchId::SelectionRight);
    struct Case {
        const char* name;
        uint32_t requested, remapped, native, forwarded;
        int reads, result;
    };
    const Case cases[] = {
        {"native A survives mixed query", a | up, 0, a, a, 1, a},
        {"native Start survives mixed query", start | right, 0, start, start, 1, start},
        {"native camera survives mixed query", camera | down, 0, camera, camera, 1, camera},
        {"native A unchanged", a, 0, a, a, 1, a},
        {"native aggregate keeps raw bits", a | camera, 0, a | camera, a | camera, 1, a | camera},
        {"native any-bit semantics", a | start | left, 0, a, a | start, 1, a},
        {"parked native Up suppressed", up, 0, up, 0, 0, 0},
        {"parked native Down suppressed", down, 0, down, 0, 0, 0},
        {"parked native Left suppressed", left, 0, left, 0, 0, 0},
        {"parked native Right suppressed", right, 0, right, 0, 0, 0},
        {"native directions cannot confirm", up | down | left | right | a, 0, up | down | left | right, a, 1, 0},
        {"remapped navigation retained", left, left, 0, 0, 0, 1},
        {"remapped A retained", a | right, a, 0, a, 1, 1},
        {"native and remapped results merge", a | left, left, a, a, 1, a | 1},
        {"unrequested buttons ignored", a | up, camera, camera, a, 1, 0},
        {"neutral stays neutral", a | start | up, 0, 0, a | start, 1, 0},
        {"empty query does not call native", 0, a | up, a | up, 0, 0, 0},
    };
    for (bool booleanNative : {false, true})
        for (const auto& c : cases)
        {
            int reads = 0;
            uint32_t forwarded = 0;
            const int result = DInputRemap::MergeSwitchQuery(c.requested, c.remapped,
                [&](uint32_t mask) {
                    ++reads; forwarded = mask;
                    const int raw = static_cast<int>(c.native & mask);
                    return booleanNative ? int(raw != 0) : raw;
                });
            const int expected = booleanNative ? int(c.result != 0) : c.result;
            if (result != expected || reads != c.reads || forwarded != c.forwarded)
            {
                std::cerr << "FAIL: " << c.name << "; booleanNative=" << booleanNative
                    << "; result=" << result << " expected=" << expected
                    << "; native reads=" << reads << " expected=" << c.reads
                    << "; native mask=" << forwarded << " expected=" << c.forwarded << '\n';
                std::exit(1);
            }
        }
    std::cout << "PASS: 34 production remapper queries preserve native A/Start/camera, raw/boolean ABI and remapped navigation while suppressing native pedal navigation.\n";
}

int main()
{
    TestAutomaticPrimarySelection();
    TestSwitchQueries();
    TestPollingEdges();
    using namespace WheelInput;
    using namespace DInputRemap;
    Calibration wheel{true, 1000, 26000, 61000};
    Calibration pedal{true, 5000, 30000, 55000};
    assert(Valid(wheel, true) && Valid(pedal, false));
    assert(Normalize(26000, wheel, true, false, .02f) == 0);
    assert(Normalize(1000, wheel, true, false, 0) == -1);
    assert(Normalize(61000, wheel, true, false, 0) == 1);
    assert(Normalize(65535, wheel, true, true, 0) == -1);
    assert(Normalize(0, wheel, true, true, 0) == 1);
    assert(Normalize(5000, pedal, false, false, .05f) == 0);
    assert(Normalize(55000, pedal, false, false, .05f) == 1);
    assert(Normalize(55000, pedal, false, true, .05f) == 0);
    assert(Normalize(5000, pedal, false, true, .05f) == 1);
    assert(Normalize(30000, pedal, false, false, 0) == .5f);
    for (auto bad : {Calibration{true, 40000, 30000, 20000}, Calibration{true, 10000, 10500, 12000},
        Calibration{true, 1000, 62000, 61000}, Calibration{true, -1, 30000, 65535},
        Calibration{true, 0, 30000, std::numeric_limits<float>::infinity()},
        Calibration{true, 0, std::numeric_limits<float>::quiet_NaN(), 65535}})
    {
        assert(!Valid(bad, true));
        assert(Normalize(30000, bad, true, false, 0) == 0);
    }
    assert(Normalize(30000, wheel, true, false, 1) == 0);
    assert(Normalize(std::numeric_limits<float>::quiet_NaN(), wheel, true, false, 0) == 0);
    Travel travel;
    std::array<long, 8> axes{}; axes.fill(32768);
    travel.Begin(axes); assert(travel.Candidate() == -1);
    axes[3] = 1000; travel.Observe(axes);
    axes[3] = 61000; travel.Observe(axes);
    assert(travel.Candidate() == 3 && travel.Endpoints(3, true).center == 32768);
    axes[4] = 0; travel.Observe(axes);
    assert(travel.Candidate() == -2); // Never choose largest/latest of two controls.
    travel.Begin(axes); assert(travel.Candidate() == -1);

    Settings::DIRemapSteeringAxis = 0; Settings::DIRemapAccelAxis = 1; Settings::DIRemapBrakeAxis = 2;
    // Compare actual legacy outputs before/after changing disabled metadata.
    // This also confirms new pedal deadzones cannot affect uncalibrated saves.
    for (bool invert : {false, true})
        for (float deadzone : {0.0f, .04f, .2f})
            for (float sensitivity : {.5f, 1.0f, 1.5f})
                for (long raw : {0, 5000, 16384, 26000, 32768, 49152, 61000, 65535})
                {
                    Settings::DIRemapSteeringInvert = Settings::DIRemapAccelInvert = Settings::DIRemapBrakeInvert = invert;
                    Settings::SteeringDeadZone = deadzone; Settings::DIRemapSteeringSensitivity = sensitivity;
                    for (auto& c : Settings::DIRemapCalibration) c = {};
                    primary.currentState.lX = primary.currentState.lY = primary.currentState.lZ = raw;
                    const auto old = std::array{GetSteering(), GetAcceleration(), GetBrake()};
                    for (auto& c : Settings::DIRemapCalibration) c = {false, 10000, 20000, 45000};
                    Settings::DIRemapAccelDeadzone = Settings::DIRemapBrakeDeadzone = .8f;
                    assert((old == std::array{GetSteering(), GetAcceleration(), GetBrake()}));
                }
    Settings::DIRemapCalibration[0] = wheel;
    primary.connected = true;
    Settings::DIRemapCalibration[1] = Settings::DIRemapCalibration[2] = pedal;
    Settings::SteeringDeadZone = Settings::DIRemapAccelDeadzone = Settings::DIRemapBrakeDeadzone = 0;
    Settings::DIRemapSteeringSensitivity = 1;
    Settings::DIRemapSteeringInvert = Settings::DIRemapAccelInvert = Settings::DIRemapBrakeInvert = false;
    primary.currentState.lX = 26000; primary.currentState.lY = primary.currentState.lZ = 5000;
    assert(GetSteering() == 0 && GetAcceleration() == 0 && GetBrake() == 0);
    primary.currentState.lX = 61000; primary.currentState.lY = primary.currentState.lZ = 55000;
    assert(GetSteering() == 127 && GetAcceleration() == 255 && GetBrake() == 255);
    primary.currentState.lX = 1000; primary.currentState.lY = primary.currentState.lZ = 30000;
    assert(GetSteering() == -127 && GetAcceleration() == 127 && GetBrake() == 127);
    Settings::DIRemapSteeringInvert = Settings::DIRemapAccelInvert = Settings::DIRemapBrakeInvert = true;
    primary.currentState.lY = primary.currentState.lZ = 55000;
    assert(GetSteering() == 127 && GetAcceleration() == 0 && GetBrake() == 0);
    primary.currentState.lX = 65535; primary.currentState.lY = primary.currentState.lZ = 0;
    assert(GetSteering() == -127 && GetAcceleration() == 255 && GetBrake() == 255);
    const std::string pedalGuidA = "{11111111-2222-3333-0405-060708090A0B}";
    const std::string pedalGuidB = "{22222222-2222-3333-0405-060708090A0B}";
    Settings::DIRemapAccelDeviceGuid = pedalGuidA;
    Settings::DIRemapBrakeDeviceGuid = pedalGuidB;
    extraInputs[pedalGuidA] = std::make_unique<DeviceSlot>();
    extraInputs[pedalGuidB] = std::make_unique<DeviceSlot>();
    auto& throttle = *extraInputs[pedalGuidA];
    auto& brake = *extraInputs[pedalGuidB];
    throttle.connected = brake.connected = throttle.initialized = brake.initialized = true;
    throttle.currentState.lY = 5000;
    brake.currentState.lZ = 55000;
    // Two independent saved devices use their own state, not the primary wheel.
    assert(GetAcceleration() == 255 && GetBrake() == 0);
    throttle.connected = false;
    assert(GetAcceleration() == 0 && GetTelemetryAccel() == -1 && GetBrake() == 0);
    throttle.connected = true;
    assert(GetAcceleration() == 255 && GetTelemetryAccel() == 255);
    Settings::DIRemapBrakeDeviceGuid = pedalGuidA;
    throttle.currentState.lZ = 5000;
    assert(PedalSlot(1) == PedalSlot(2) && GetBrake() == 255); // Shared USB pedal set.
    Settings::DIRemapAccelDeviceGuid = "{33333333-2222-3333-0405-060708090A0B}";
    assert(GetAcceleration() == 0); // Missing saved identity never falls back.
    Settings::DIRemapAccelDeviceGuid = "invalid-guid";
    assert(GetAcceleration() == 0);
    ReleaseUnusedUiDevices();
    assert(extraInputs.size() == 1 && extraInputs.contains(pedalGuidA));
    Settings::DIRemapAccelDeviceGuid.clear(); Settings::DIRemapBrakeDeviceGuid.clear();
    ReleaseUnusedUiDevices(); assert(extraInputs.empty());
    const std::string oldPrimaryGuid = "{33333333-2222-3333-0405-060708090A0B}";
    ParseGuid(oldPrimaryGuid, primary.guid); primaryGuid = primary.guid; primaryGuidValid = true;
    primary.initialized = primary.connected = true;
    primary.currentState.lX = 61000;
    extraInputs[pedalGuidA] = std::make_unique<DeviceSlot>();
    auto& replacement = *extraInputs[pedalGuidA];
    replacement.initialized = replacement.connected = true;
    ParseGuid(pedalGuidA, replacement.guid);
    replacement.currentState.lX = 1000;
    Settings::DIRemapAccelDeviceGuid = Settings::DIRemapBrakeDeviceGuid = oldPrimaryGuid;
    assert(CanAdoptPrimaryInput(pedalGuidA) && !CanAdoptPrimaryInput(pedalGuidB));
    AdoptPrimaryInput(pedalGuidA);
    assert(PrimaryInputGuid() == pedalGuidA && primary.currentState.lX == 1000);
    assert(primary.previousState.lX == primary.currentState.lX);
    assert(PedalSlot(1) == PedalSlot(2) && PedalSlot(1) != &primary);
    assert(PedalSlot(1)->currentState.lX == 61000);
    ReleaseUnusedUiDevices();
    assert(extraInputs.size() == 1 && extraInputs.contains(oldPrimaryGuid));
    Settings::DIRemapAccelDeviceGuid.clear(); Settings::DIRemapBrakeDeviceGuid.clear();
    ReleaseUnusedUiDevices();
    primary.connected = false;
    assert(GetSteering() == 0); // Explicitly calibrated steering fails neutral too.
    // A pedal can resolve to primary through an empty or explicit saved GUID,
    // including after primary adoption. Stale current AND previous reads must
    // fail neutral; telemetry reports unavailable rather than stale throttle.
    for (const auto& identity : {std::string{}, PrimaryInputGuid()})
    {
        Settings::DIRemapAccelDeviceGuid = Settings::DIRemapBrakeDeviceGuid = identity;
        primary.currentState.lY = primary.currentState.lZ = 5000;
        primary.previousState.lY = primary.previousState.lZ = 5000;
        assert(GetAcceleration() == 0 && GetBrake() == 0);
        assert(GetPedal(1, true) == 0 && GetPedal(2, true) == 0);
        assert(GetTelemetryAccel() == -1 && GetTelemetryBrake() == -1);
        primary.connected = true;
        assert(GetAcceleration() == 255 && GetBrake() == 255);
        assert(GetPedal(1, true) == 255 && GetPedal(2, true) == 255);
        assert(GetTelemetryAccel() == 255 && GetTelemetryBrake() == 255);
        primary.connected = false;
    }
    Settings::DIRemapSteeringAxis = Settings::DIRemapAccelAxis = Settings::DIRemapBrakeAxis = -1;
    assert(GetSteering() == 0 && GetAcceleration() == 0 && GetBrake() == 0);
    // Optional roles share exact saved devices with pedals and the primary,
    // and stale buttons/POVs/H-pattern targets disappear on disconnect.
    extraInputs[pedalGuidB] = std::make_unique<DeviceSlot>();
    auto& buttonBox = *extraInputs[pedalGuidB];
    buttonBox.connected = buttonBox.initialized = true;
    ParseGuid(pedalGuidB, buttonBox.guid);
    buttonBox.currentState.rgbButtons[89] = 0x80;
    buttonBox.previousState.rgbButtons[89] = 0x80;
    buttonBox.currentState.rgdwPOV[0] = 9000;
    Settings::DIAuxDeviceGuid = Settings::DIShifterDeviceGuid = pedalGuidB;
    Settings::DIRemapBrakeDeviceGuid = pedalGuidB;
    Settings::DIAuxButtonChangeView = Settings::DIShifterButtonGear1 = 89;
    Settings::DIShifterGearMode = "hpattern";
    assert(&OptionalSlot(false) == &OptionalSlot(true) && &OptionalSlot(false) == PedalSlot(2));
    assert(IsButtonPressedAny(SwitchId::ChangeView) && WasButtonPressedAny(SwitchId::ChangeView));
    UpdateHPattern(); assert(hpattern.targetGear == 1);
    uint32_t povMask = 0; ApplyPovToMask(OptionalSlot(false), povMask); assert(povMask);
    buttonBox.connected = false;
    assert(!IsButtonPressedAny(SwitchId::ChangeView) && !WasButtonPressedAny(SwitchId::ChangeView));
    UpdateHPattern(); assert(hpattern.targetGear == 0 && hpattern.cachedMask == 0);
    povMask = 0; ApplyPovToMask(OptionalSlot(false), povMask); assert(!povMask);
    buttonBox.connected = true;
    Settings::DIShifterDeviceGuid = "invalid";
    assert(!OptionalSlot(true).connected && OptionalSlot(false).connected);
    Settings::DIRemapBrakeDeviceGuid.clear(); ReleaseUnusedUiDevices();
    assert(extraInputs.contains(pedalGuidB)); // Optional binding retains its shared handle.
    Settings::DIAuxDeviceGuid.clear(); Settings::DIShifterDeviceGuid.clear();
    ReleaseUnusedUiDevices(); assert(extraInputs.empty());
    // Minimal memory-only COM vtables exercise the production release path.
    void* ownedTable[32]{}; ownedTable[2]=reinterpret_cast<void*>(FakeRelease); ownedTable[8]=reinterpret_cast<void*>(FakeUnacquire);
    void* borrowedTable[32]{}; borrowedTable[2]=reinterpret_cast<void*>(BorrowedRelease);
    void** ownedObject=ownedTable; void** borrowedObject=borrowedTable;
    auto* owned=reinterpret_cast<IDirectInputDevice8A*>(&ownedObject);
    primary.device=shifter.device=aux.device=owned; // alias must not release twice
    extraInputs[oldPrimaryGuid]=std::make_unique<DeviceSlot>(); extraInputs[oldPrimaryGuid]->device=owned;
    g_RealDirectInput8=reinterpret_cast<IDirectInput8A*>(&borrowedObject);
    IDirectInput8A* borrowed=g_RealDirectInput8; Game::DirectInput8_ptr=&borrowed;
    assert(ConsumerLifecycle::Runtime().ClaimFinalization()); FinalizeForExit(); ConsumerLifecycle::Runtime().CompleteFinalization();
    assert(consumerReleases==1 && consumerUnacquires==1 && borrowedCalls==0);
    assert(!primary.device && !shifter.device && !aux.device && extraInputs.empty());
    assert(!ReadUiSnapshot().connected && GetTelemetryAccel()==-1 && !GetPrimaryDevice());
    RefreshUiInputDevices(); ReleaseUnusedUiDevices(); AdoptPrimaryInput(oldPrimaryGuid);
    assert(consumerReleases==1 && borrowedCalls==0);
    std::cout << "PASS: actual axis readers and 144 legacy cases, calibration/pedal identities/reconnect; production finalization releases aliased consumer COM handles once, preserves borrowed game/proxy handles, and rejects stopped input/UI/telemetry. Memory-only fake COM; no device calls.\n";
}
