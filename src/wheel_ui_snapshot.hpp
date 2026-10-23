#pragma once
#include <array>
#include <string>
#include <vector>
#include "pov_binding.hpp"

namespace DInputRemap
{
struct UiSnapshot
{
    bool connected = false;
    std::string name;
    std::string guid;
    std::array<long, 8> axes{};
    // Physical buttons 0..127, then POV directions in the existing binding encoding.
    std::array<bool, PovBinding::Last + 1> buttons{};
    float steering = 0, throttle = 0, brake = 0;
};
UiSnapshot ReadUiSnapshot();
UiSnapshot ReadAxisUiSnapshot(int role);
UiSnapshot ReadDeviceUiSnapshot(const std::string& guid);
struct InputDeviceChoice { std::string guid, name; };
const std::vector<InputDeviceChoice>& UiInputDevices();
void RefreshUiInputDevices();
void ReleaseUnusedUiDevices();
std::string PrimaryInputGuid();
bool CanAdoptPrimaryInput(const std::string& guid);
void AdoptPrimaryInput(const std::string& guid);
}
namespace FFB
{
void ZeroAllForces();
const char* UiStatus();
struct DeviceChoice { std::string guid, name; };
const std::vector<DeviceChoice>& UiDevices();
void RefreshUiDevices(bool retry = true);
void SelectionChanged();
std::string UiDeviceLabel();
}
namespace Telemetry
{
void SetEnabled(bool enabled);
const char* UiStatus();
}
