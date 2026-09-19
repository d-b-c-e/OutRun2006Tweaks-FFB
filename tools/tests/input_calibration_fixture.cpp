// Executes the production primary-axis readers against memory-only DI states.
// Never enumerates, acquires a device, installs hooks or starts the game.
#include "../../src/hooks_inputremap.cpp"
#include <cassert>
#include <iostream>
#include <limits>

IDirectInput8A* g_RealDirectInput8 = nullptr;
Hook::Hook() {}

int main()
{
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
    Settings::DIRemapSteeringAxis = Settings::DIRemapAccelAxis = Settings::DIRemapBrakeAxis = -1;
    assert(GetSteering() == 0 && GetAcceleration() == 0 && GetBrake() == 0);
    std::cout << "PASS: actual axis readers, legacy opt-out preservation (144 cases), calibrated endpoints/center/partial/clamp/invert/clear, invalid range/ambiguity, independent/shared USB pedal identity, missing/refused identity neutral, reconnect and unused-slot cleanup. No device calls.\n";
}
