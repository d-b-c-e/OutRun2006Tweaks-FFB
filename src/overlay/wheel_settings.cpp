#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include "hook_mgr.hpp"
#include "plugin.hpp"
#include "game_addrs.hpp"
#include "resource.h"
#include "overlay.hpp"
#include "wheel_settings.hpp"
#include "wheel_settings_policy.hpp"
#include "wheel_ui_snapshot.hpp"
#include <imgui.h>

extern void ForceShowCursor(bool show);
extern bool overlay_visible;

namespace WheelSettingsUi
{
static float UiScale() { return ImGui::GetIO().FontGlobalScale / 1.5f; }
enum class Page { Setup, Controls, Ffb, Cameras, Telemetry, Help };
static Page page = Page::Setup;
static bool saveFailed = false;
static std::map<std::pair<std::string, std::string>, std::string> pending;
static std::string saveError;

// Merge only edited keys into the existing user override. Do not serialize
// every setting or rewrite the shipped INI, and never truncate a working file.
static bool SavePending()
{
    const auto& path = Module::UserIniPath;
    auto temporary = path;
    temporary += L".wheel-settings.tmp";
    try
    {
        std::string contents;
        if (std::filesystem::exists(path))
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("Cannot read the settings file");
            contents.assign(std::istreambuf_iterator<char>(input), {});
            if (input.bad()) throw std::runtime_error("Cannot read the settings file");
        }
        // The game's reader accepts UTF-8/ANSI, not UTF-16. Refuse an unsupported
        // file instead of silently replacing an owner's data in another encoding.
        if (contents.find('\0') != std::string::npos)
            throw std::runtime_error("Settings file encoding is unsupported; keep the original and check Help");
        for (const auto& [key, value] : pending)
            contents = WheelSettingsPolicy::SetIniValue(contents, key.first, key.second, value);
        auto backup = path;
        backup += L".before-wheel-settings.bak";
        if (std::filesystem::exists(path) && !std::filesystem::exists(backup))
            std::filesystem::copy_file(path, backup);
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.exceptions(std::ios::failbit | std::ios::badbit);
            output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            output.close();
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace the settings file (Windows error " + std::to_string(GetLastError()) + ")");
        pending.clear();
        saveFailed = false;
        saveError.clear();
        return true;
    }
    catch (const std::exception& error)
    {
        saveFailed = true;
        saveError = error.what();
        spdlog::error("Wheel settings: {}", saveError);
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
}

template<class T> static void Queue(const char* section, const char* key, T value)
{
    std::ostringstream text;
    text << std::boolalpha << value;
    pending[{section, key}] = text.str();
}
template<class T> static bool Save(const char* section, const char* key, T value)
{
    Queue(section, key, value);
    return SavePending();
}

void StopFfb()
{
    Settings::DirectInputFFB = false;
    FFB::ZeroAllForces();
    Save("FFB", "DirectInputFFB", false);
}

void FlushChanges() { if (!pending.empty()) SavePending(); }

struct Capture
{
    int* destination = nullptr;
    const char* key = nullptr;
    const char* label = nullptr;
    bool axis = false;
    int candidate = -1;
    double deadline = 0;
    DInputRemap::UiSnapshot initial;
    int role = -1, fixedAxis = -1, stage = 0;
    WheelInput::Travel travel;
    WheelInput::Calibration calibration;
    bool invert = false;
    float deadzone = 0;
};
static Capture capture;
bool IsCapturing() { return capture.destination != nullptr; }
static void EndCapture() { capture = {}; DInputRemap::ReleaseUnusedUiDevices(); }

static bool Choice(const char* label, bool& value)
{
    ImGui::PushID(label);
    ImGui::Text("%s:", label);
    ImGui::SameLine(180 * UiScale());
    bool changed = false;
    if (ImGui::RadioButton("Off", !value) && value) { value = false; changed = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("On", value) && !value) { value = true; changed = true; }
    ImGui::PopID();
    return changed;
}

static void Percent(const char* label, float& value, const char* section, const char* key, float maximum = 2.0f)
{
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(180 * UiScale());
    ImGui::SetNextItemWidth(-1);
    float percent = value * 100.0f;
    if (ImGui::SliderFloat("##value", &percent, 0, maximum * 100.0f, "%.0f%%"))
    {
        value = percent / 100.0f;
        Queue(section, key, value);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) Save(section, key, value);
    ImGui::PopID();
}

static void BeginCapture(int& value, const char* label, const char* key, bool axis, const DInputRemap::UiSnapshot& input)
{
    capture = { &value, key, label, axis, -1, ImGui::GetTime() + 20.0, input };
}

static bool& AxisInvert(int role)
{
    return role == 0 ? Settings::DIRemapSteeringInvert : role == 1 ? Settings::DIRemapAccelInvert : Settings::DIRemapBrakeInvert;
}
static float& AxisDeadzone(int role)
{
    return role == 0 ? Settings::SteeringDeadZone : role == 1 ? Settings::DIRemapAccelDeadzone : Settings::DIRemapBrakeDeadzone;
}
static const char* AxisPrefix(int role) { return role == 0 ? "Steering" : role == 1 ? "Throttle" : "Brake"; }

static void BeginAxisCalibration(int& value, const char* key, int role, bool existing, const DInputRemap::UiSnapshot& input)
{
    BeginCapture(value, AxisPrefix(role), key, true, input);
    capture.role = role;
    capture.fixedAxis = existing ? value : -1;
    capture.deadline = ImGui::GetTime() + 90;
    capture.deadzone = AxisDeadzone(role);
    capture.invert = AxisInvert(role);
    DInputRemap::RefreshUiInputDevices();
}

static bool SaveAxisCalibration(const DInputRemap::UiSnapshot& input)
{
    if (capture.role < 0 || capture.stage != 2 || capture.candidate < 0 || capture.candidate > 7 ||
        !input.connected || input.guid.empty() || input.guid != capture.initial.guid ||
        !capture.calibration.enabled || !WheelInput::Valid(capture.calibration, capture.role == 0) || !std::isfinite(capture.deadzone) ||
        capture.deadzone < 0 || capture.deadzone >= 1) return false;
    if (capture.role == 0 && !DInputRemap::CanAdoptPrimaryInput(input.guid)) return false;
    const auto oldPrimary = DInputRemap::PrimaryInputGuid();
    const bool replacingPrimary = capture.role == 0 && !oldPrimary.empty() && oldPrimary != input.guid;
    const bool pinThrottle = replacingPrimary && Settings::DIRemapAccelDeviceGuid.empty();
    const bool pinBrake = replacingPrimary && Settings::DIRemapBrakeDeviceGuid.empty();
    const auto previousPending = pending;
    const auto prefix = std::string(AxisPrefix(capture.role));
    Queue("DirectInput", capture.key, capture.candidate);
    Queue("DirectInput.Calibration", (prefix + "Enabled").c_str(), true);
    Queue("DirectInput.Calibration", (prefix + "Minimum").c_str(), capture.calibration.minimum);
    Queue("DirectInput.Calibration", (prefix + "Center").c_str(), capture.calibration.center);
    Queue("DirectInput.Calibration", (prefix + "Maximum").c_str(), capture.calibration.maximum);
    const char* invertKey = capture.role == 0 ? "SteeringInvert" : capture.role == 1 ? "AccelerationInvert" : "BrakeInvert";
    const char* deadzoneKey = capture.role == 0 ? "SteeringDeadZone" : capture.role == 1 ? "AccelerationDeadzone" : "BrakeDeadzone";
    Queue("DirectInput", invertKey, capture.invert);
    Queue(capture.role == 0 ? "Controls" : "DirectInput", deadzoneKey, capture.deadzone);
    if (capture.role == 0) Queue("DirectInput", "DeviceGuid", input.guid);
    else
    {
        Queue("DirectInput", capture.role == 1 ? "ThrottleDeviceGuid" : "BrakeDeviceGuid", input.guid);
        Queue("DirectInput", capture.role == 1 ? "ThrottleDeviceName" : "BrakeDeviceName", input.name);
    }
    if (pinThrottle) Queue("DirectInput", "ThrottleDeviceGuid", oldPrimary);
    if (pinBrake) Queue("DirectInput", "BrakeDeviceGuid", oldPrimary);
    if (!SavePending())
    {
        // Never flush a provisional calibration later through Stop/view/close.
        pending = previousPending;
        return false;
    }
    *capture.destination = capture.candidate;
    Settings::DIRemapCalibration[capture.role] = capture.calibration;
    AxisInvert(capture.role) = capture.invert;
    AxisDeadzone(capture.role) = capture.deadzone;
    if (capture.role == 0)
    {
        const bool identityChanged = Settings::DIRemapDeviceGuid != input.guid;
        Settings::DIRemapDeviceGuid = input.guid;
        if (identityChanged) FFB::SelectionChanged(); // Zero before the input-handle swap too.
        if (pinThrottle) Settings::DIRemapAccelDeviceGuid = oldPrimary;
        if (pinBrake) Settings::DIRemapBrakeDeviceGuid = oldPrimary;
        DInputRemap::AdoptPrimaryInput(input.guid);
    }
    else
    {
        (capture.role == 1 ? Settings::DIRemapAccelDeviceGuid : Settings::DIRemapBrakeDeviceGuid) = input.guid;
        (capture.role == 1 ? Settings::DIRemapAccelDeviceName : Settings::DIRemapBrakeDeviceName) = input.name;
    }
    return true;
}

static void AxisCalibrationPanel(const DInputRemap::UiSnapshot& input)
{
    const bool steering = capture.role == 0;
    ImGui::Text("Calibrate %s", capture.label);
    ImGui::TextWrapped("Device: %s", input.connected ? input.name.c_str() : "Missing or unavailable");
    ImGui::TextWrapped("Your current binding remains unchanged until Save calibration.");
    if (capture.stage == 0)
    {
        {
            ImGui::TextUnformatted("Input device");
            ImGui::SameLine(180 * UiScale());
            ImGui::SetNextItemWidth(-1);
            ImGui::BeginDisabled(capture.fixedAxis >= 0);
            if (ImGui::BeginCombo("##input-device", input.connected ? input.name.c_str() : "Choose a connected device"))
            {
                const auto& devices = DInputRemap::UiInputDevices();
                for (const auto& device : devices)
                {
                    auto name = device.name;
                    if (std::count_if(devices.begin(), devices.end(), [&](const auto& other) { return other.name == name; }) > 1)
                        name += " [" + device.guid.substr(1, 8) + "]";
                    ImGui::PushID(device.guid.c_str());
                    if (ImGui::Selectable(name.c_str(), device.guid == capture.initial.guid))
                    {
                        capture.initial = DInputRemap::ReadDeviceUiSnapshot(device.guid);
                        capture.deadline = ImGui::GetTime() + 90;
                        // Release the previous provisional device on cancellation/save.
                    }
                    ImGui::PopID();
                }
                if (devices.empty()) ImGui::TextDisabled("No input devices found");
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            if (ImGui::Button("Refresh devices")) DInputRemap::RefreshUiInputDevices();
            if (capture.fixedAxis >= 0) ImGui::TextWrapped("Use Cancel, then Bind to choose a different device or axis.");
        }
        ImGui::TextWrapped(steering ? "Center the wheel and leave other controls still." : "Release this pedal fully and leave other controls still.");
        ImGui::BeginDisabled(!input.connected || input.guid != capture.initial.guid);
        if (ImGui::Button(steering ? "Capture center" : "Capture rest"))
        {
            capture.travel.Begin(input.axes);
            capture.stage = 1;
        }
        ImGui::EndDisabled();
    }
    else if (capture.stage == 1)
    {
        ImGui::TextWrapped(steering ? "Turn fully left, then fully right, then return to center." : "Press this pedal fully, then release it.");
        capture.travel.Observe(input.axes);
        const int candidate = capture.travel.Candidate();
        bool valid = candidate >= 0 && (capture.fixedAxis < 0 || candidate == capture.fixedAxis);
        if (valid)
        {
            capture.calibration = capture.travel.Endpoints(candidate, steering);
            valid = WheelInput::Valid(capture.calibration, steering);
            if (!steering)
            {
                const float rest = capture.travel.rest[candidate];
                const float span = capture.calibration.maximum - capture.calibration.minimum;
                valid &= std::min(std::abs(rest - capture.calibration.minimum), std::abs(rest - capture.calibration.maximum)) <= span * .05f;
                capture.invert = rest > capture.calibration.center;
            }
        }
        if (candidate == -2) ImGui::TextWrapped("More than one axis moved. Choose Start again and move only the requested control.");
        else if (candidate < 0) ImGui::TextUnformatted("Waiting for enough travel...");
        else if (capture.fixedAxis >= 0 && candidate != capture.fixedAxis)
            ImGui::TextWrapped("A different axis moved. Use the assigned control, or Cancel and choose Bind to replace it.");
        else if (!valid) ImGui::TextWrapped("Capture both steering limits around center, or a released pedal and its full travel.");
        else ImGui::Text("Detected: Axis %d", candidate + 1);
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Preview calibration")) { capture.candidate = candidate; capture.stage = 2; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Start again")) capture.stage = 0;
    }
    else
    {
        const float value = WheelInput::Normalize(static_cast<float>(input.axes[capture.candidate]), capture.calibration,
            steering, capture.invert, capture.deadzone);
        ImGui::Text("Device input: %+.0f%%", value * 100);
        ImGui::ProgressBar(steering ? (value + 1) / 2 : value, ImVec2(-1, 0), "");
        Choice("Invert", capture.invert);
        float percent = capture.deadzone * 100;
        ImGui::TextUnformatted("Deadzone");
        ImGui::SameLine(180 * UiScale());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##calibration-deadzone", &percent, 0, 95, "%.0f%%")) capture.deadzone = percent / 100;
        ImGui::TextWrapped("%s", steering ? "Check center is 0%, left is -100% and right is +100%." : "Check released is 0%, partial travel is smooth and fully pressed is 100%.");
        if (ImGui::Button("Save calibration") && SaveAxisCalibration(input)) { EndCapture(); return; }
        ImGui::SameLine();
        if (ImGui::Button("Start again")) capture.stage = 0;
    }
    if (ImGui::Button("Cancel")) EndCapture();
}

static void CapturePanel(const DInputRemap::UiSnapshot& primaryInput)
{
    if (!IsCapturing()) return;
    const auto input = capture.axis && capture.role >= 0 ? DInputRemap::ReadDeviceUiSnapshot(capture.initial.guid) : primaryInput;
    const bool choosingDevice = capture.axis && capture.role >= 0 && capture.stage == 0;
    if ((!choosingDevice && (!input.connected || input.guid != capture.initial.guid)) || GetForegroundWindow() != Game::GameHwnd() || ImGui::GetTime() > capture.deadline ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { EndCapture(); return; }
    if (capture.axis && capture.role >= 0) { AxisCalibrationPanel(input); return; }
    ImGui::Separator();
    ImGui::Text("Bind %s", capture.label);
    ImGui::TextWrapped(capture.axis ? "Move only this axis through its travel, then choose Save binding. Your previous binding stays active until saved."
        : "Press and release the desired wheel button, then choose Save binding.");
    if (capture.axis)
    {
        long greatest = 8192;
        for (int i = 0; i < 8; ++i)
        {
            const auto movement = std::abs(input.axes[i] - capture.initial.axes[i]);
            if (movement > greatest) { greatest = movement; capture.candidate = i; }
        }
    }
    else
        for (int i = 0; i < 128; ++i)
        {
            if (!input.buttons[i]) capture.initial.buttons[i] = false;
            if (input.buttons[i] && !capture.initial.buttons[i]) { capture.candidate = i; break; }
        }
    if (capture.candidate >= 0)
        ImGui::Text("Detected: %s %d", capture.axis ? "Axis" : "Button", capture.candidate + 1);
    else ImGui::TextUnformatted("Waiting for input...");
    ImGui::BeginDisabled(capture.candidate < 0 || (!capture.axis && input.buttons[capture.candidate]));
    if (ImGui::Button("Save binding"))
    {
        const bool saveSteering = capture.axis && std::string(capture.key) == "SteeringAxis";
        if (saveSteering) Queue("DirectInput", "DeviceGuid", input.guid);
        if (Save("DirectInput", capture.key, capture.candidate))
        {
            *capture.destination = capture.candidate;
            if (saveSteering)
            {
                Settings::DIRemapDeviceGuid = input.guid;
                if (Settings::FFBDeviceGuid == "steering") FFB::SelectionChanged();
            }
            EndCapture();
        }
        else
        {
            pending.erase({"DirectInput", capture.key});
            if (saveSteering) pending.erase({"DirectInput", "DeviceGuid"});
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        // A failed provisional save must not be retried after cancellation.
        pending.erase({"DirectInput", capture.key});
        EndCapture();
    }
}

static void AxisRow(int role, const char* label, int& axis, bool& invert, const char* key, const char* invertKey,
    float value, const DInputRemap::UiSnapshot& primaryInput)
{
    const auto input = role == 0 ? primaryInput : DInputRemap::ReadAxisUiSnapshot(role);
    ImGui::PushID(label);
    if (axis >= 0) ImGui::Text("%s - Axis %d", label, axis + 1);
    else ImGui::Text("%s - Not bound", label);
    ImGui::SameLine();
    ImGui::BeginDisabled(IsCapturing());
    ImGui::BeginDisabled(!Settings::UseDirectInputRemap || Settings::UseNewInput);
    if (ImGui::Button("Bind")) BeginAxisCalibration(axis, key, role, false, input);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(axis < 0 || !input.connected);
    if (ImGui::Button("Calibrate")) BeginAxisCalibration(axis, key, role, true, input);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(axis < 0);
    if (ImGui::Button("Clear"))
    {
        if (Save("DirectInput", key, -1)) axis = -1;
        else pending.erase({"DirectInput", key});
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (role > 0)
    {
        const auto& savedName = role == 1 ? Settings::DIRemapAccelDeviceName : Settings::DIRemapBrakeDeviceName;
        ImGui::TextWrapped("Device: %s%s", input.connected ? input.name.c_str() : savedName.empty() ? "Unavailable" : savedName.c_str(), input.connected ? "" : " (missing)");
    }
    if (input.connected)
    {
        char text[40];
        sprintf_s(text, "%+.0f%%", value * 100.0f);
        ImGui::ProgressBar(role == 0 ? (value + 1) / 2 : std::clamp(value, 0.0f, 1.0f), ImVec2(-1, 0), text);
    }
    else ImGui::TextDisabled("Device input unavailable");
    if (!Settings::DIRemapCalibration[role].enabled) ImGui::TextDisabled("Using the existing full device range - Calibrate to set endpoints");
    else if (!WheelInput::Valid(Settings::DIRemapCalibration[role], role == 0)) ImGui::TextWrapped("Saved calibration is invalid. Calibrate this axis again.");
    if (Choice("Invert", invert)) Save("DirectInput", invertKey, invert);
    ImGui::PopID();
}

static void ButtonRow(const char* label, int& button, const char* key, const DInputRemap::UiSnapshot& input)
{
    ImGui::PushID(key);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(180 * UiScale());
    if (button >= 0) ImGui::Text("Button %d", button + 1); else ImGui::TextDisabled("Not bound");
    ImGui::SameLine(310 * UiScale());
    ImGui::BeginDisabled(!input.connected || IsCapturing());
    if (ImGui::Button("Bind")) BeginCapture(button, label, key, false, input);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(IsCapturing());
    if (ImGui::Button("Clear"))
    {
        if (Save("DirectInput", key, -1)) button = -1;
        else pending.erase({"DirectInput", key});
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}

static bool CustomTuning()
{
    return Settings::FFBProfile != "legacy" || Settings::FFBSpringStrength != 0.45f ||
        Settings::FFBDamperStrength != 0.10f || Settings::FFBSteeringWeight != 0.55f ||
        Settings::FFBGripLoss != 0.6f || Settings::FFBWeightTransfer != 0.8f ||
        Settings::FFBLateralDeadzone != 1.5f || Settings::FFBWallImpact != 1.0f ||
        Settings::FFBGearShift != 0.3f || Settings::FFBRoadTexture != 0.6f ||
        Settings::FFBTireSlip != 0.35f || Settings::FFBEngineIdle != 0.08f ||
        !Settings::FFBUsePeriodicEffects || Settings::FFBWheelTorqueNm != 0 ||
        Settings::FFBInvertForce || Settings::FFBDiagnosticLog || Settings::DIRemapSteeringSensitivity != 1.0f;
}

static void SelectDevice(const std::string& guid, const std::string& name)
{
    Queue("FFB", "FFBDeviceGuid", guid);
    Queue("FFB", "FFBDeviceName", name);
    if (SavePending())
    {
        FFB::SelectionChanged(); // Zero/release the previous actuator first.
        Settings::FFBDeviceGuid = guid;
        Settings::FFBDeviceName = name;
    }
    else
    {
        pending.erase({"FFB", "FFBDeviceGuid"});
        pending.erase({"FFB", "FFBDeviceName"});
    }
}

static void DevicePicker()
{
    static bool listed = false;
    if (!listed) { FFB::RefreshUiDevices(); listed = true; }
    const auto label = FFB::UiDeviceLabel();
    ImGui::TextUnformatted("FFB device");
    ImGui::SameLine(180 * UiScale());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##ffb-device", label.c_str()))
    {
        if (ImGui::Selectable("Use steering wheel", Settings::FFBDeviceGuid == "steering")) SelectDevice("steering", "");
        const auto& devices = FFB::UiDevices();
        for (const auto& device : devices)
        {
            auto name = device.name;
            if (std::count_if(devices.begin(), devices.end(), [&](const auto& other) { return other.name == device.name; }) > 1)
                name += " [" + device.guid.substr(1, 8) + "]";
            ImGui::PushID(device.guid.c_str());
            if (ImGui::Selectable(name.c_str(), Settings::FFBDeviceGuid == device.guid)) SelectDevice(device.guid, device.name);
            ImGui::PopID();
        }
        if (devices.empty()) ImGui::TextDisabled("No force-feedback devices found");
        ImGui::EndCombo();
    }
    if (ImGui::Button("Refresh devices")) FFB::RefreshUiDevices();
    ImGui::TextWrapped("A missing saved wheel stays selected. Choose a replacement here; the mod never picks another actuator automatically.");
}

static void Contents(const DInputRemap::UiSnapshot& input, bool advanced)
{
    if (IsCapturing()) { CapturePanel(input); return; }
    switch (page)
    {
    case Page::Setup:
        ImGui::TextUnformatted("Connect your wheel, check the axes, then drive.");
        if (!Settings::UseDirectInputRemap || Settings::UseNewInput)
        {
            ImGui::TextWrapped("Wheel remapping is not active. Enabling it takes effect after restarting the game.");
            if (ImGui::Button("Enable wheel controls for next launch"))
            {
                pending[{"DirectInput", "UseDirectInputRemap"}] = "true";
                pending[{"Controls", "UseNewInput"}] = "false";
                if (SavePending()) Save("WheelSettings", "View", Settings::WheelSettingsView);
            }
            ImGui::TextWrapped("After saving, exit normally and restart. Device selection still uses the existing saved wheel configuration.");
        }
        else if (!input.connected)
            ImGui::TextWrapped("Wheel input unavailable. Check its connection, then restart the game. Automatic device replacement is not supported by this version.");
        else
        {
            ImGui::TextWrapped("Wheel: %s", input.name.c_str());
            ImGui::Text("Steering: %+.0f%%   Throttle: %.0f%%   Brake: %.0f%%", input.steering * 100, input.throttle * 100, input.brake * 100);
            ImGui::TextWrapped("Next: check each axis and bind your driving buttons in Controls.");
        }
        if (ImGui::Button("Open Controls")) page = Page::Controls;
        ImGui::SameLine();
        if (ImGui::Button("Check FFB")) page = Page::Ffb;
        ImGui::TextWrapped("Telemetry and advanced tuning are optional. FFB remains Off unless you enable it.");
        break;
    case Page::Controls:
        if (Settings::UseNewInput)
        {
            ImGui::TextWrapped("The current controller input route uses the existing bindings dialog.");
            if (ImGui::Button("Open controller bindings")) Overlay::RequestBindingDialog = true;
        }
        ImGui::TextWrapped("Wheel: %s", input.connected ? input.name.c_str() : "Unavailable");
        ImGui::BeginDisabled(IsCapturing());
        AxisRow(0, "Steering", Settings::DIRemapSteeringAxis, Settings::DIRemapSteeringInvert, "SteeringAxis", "SteeringInvert", input.steering, input);
        AxisRow(1, "Throttle", Settings::DIRemapAccelAxis, Settings::DIRemapAccelInvert, "AccelerationAxis", "AccelerationInvert", input.throttle, input);
        AxisRow(2, "Brake", Settings::DIRemapBrakeAxis, Settings::DIRemapBrakeInvert, "BrakeAxis", "BrakeInvert", input.brake, input);
        Percent("Steering deadzone", Settings::SteeringDeadZone, "Controls", "SteeringDeadZone", 0.95f);
        ImGui::TextWrapped("Values here are game input. Bind lets each pedal use its own device; a missing saved device outputs zero until it returns or you bind a replacement.");
        ImGui::TextDisabled("Handbrake: no handbrake action is exposed by this game adapter.");
        if (ImGui::CollapsingHeader("Driving buttons"))
        {
            ButtonRow("Shift up", Settings::DIRemapButtonGearUp, "ButtonGearUp", input);
            ButtonRow("Shift down", Settings::DIRemapButtonGearDown, "ButtonGearDown", input);
            ButtonRow("Change camera", Settings::DIRemapButtonChangeView, "ButtonChangeView", input);
        }
        if (ImGui::CollapsingHeader("Menu buttons"))
        {
            ButtonRow("Confirm", Settings::DIRemapButtonA, "ButtonA", input);
            ButtonRow("Back", Settings::DIRemapButtonB, "ButtonB", input);
            ButtonRow("Start / pause", Settings::DIRemapButtonStart, "ButtonStart", input);
            ButtonRow("Select", Settings::DIRemapButtonBack, "ButtonBack", input);
            ButtonRow("X action", Settings::DIRemapButtonX, "ButtonX", input);
            ButtonRow("Y action", Settings::DIRemapButtonY, "ButtonY", input);
            ButtonRow("Menu up", Settings::DIRemapButtonSelUp, "ButtonSelUp", input);
            ButtonRow("Menu down", Settings::DIRemapButtonSelDown, "ButtonSelDown", input);
            ButtonRow("Menu left", Settings::DIRemapButtonSelLeft, "ButtonSelLeft", input);
            ButtonRow("Menu right", Settings::DIRemapButtonSelRight, "ButtonSelRight", input);
        }
        if (advanced)
        {
            if (ImGui::SliderFloat("Steering sensitivity", &Settings::DIRemapSteeringSensitivity, 0.1f, 10.0f, "%.2fx"))
                Queue("DirectInput", "SteeringSensitivity", Settings::DIRemapSteeringSensitivity);
            if (ImGui::IsItemDeactivatedAfterEdit()) Save("DirectInput", "SteeringSensitivity", Settings::DIRemapSteeringSensitivity);
            ImGui::TextWrapped("Separate shifter and button-box assignments remain startup INI settings. See Help for the settings folder.");
            if (input.connected)
                for (int i = 0; i < 8; ++i) ImGui::Text("Raw axis %d: %ld", i + 1, input.axes[i]);
        }
        ImGui::EndDisabled();
        CapturePanel(input);
        break;
    case Page::Ffb:
        if (Choice("FFB", Settings::DirectInputFFB))
        {
            if (!Settings::DirectInputFFB) FFB::ZeroAllForces();
            Save("FFB", "DirectInputFFB", Settings::DirectInputFFB);
        }
        DevicePicker();
        Percent("Strength", Settings::FFBGlobalStrength, "FFB", "FFBGlobalStrength");
        ImGui::TextWrapped("Status: %s", FFB::UiStatus());
        ImGui::TextWrapped("This build's steering force signal still needs a driven validation. Start with low strength.");
        if (advanced)
        {
            ImGui::Separator();
            ImGui::Text("Force profile: %s (startup setting)", Settings::FFBProfile.c_str());
            ImGui::TextWrapped("The gains below belong to the built-in legacy model. Named profiles keep their tuning in force-profiles.user.ini.");
            ImGui::BeginDisabled(Settings::FFBProfile != "legacy");
            Percent("Centering", Settings::FFBSpringStrength, "FFB", "FFBSpringStrength");
            Percent("Damping", Settings::FFBDamperStrength, "FFB", "FFBDamperStrength");
            Percent("Cornering load", Settings::FFBSteeringWeight, "FFB", "FFBSteeringWeight");
            Percent("Grip loss", Settings::FFBGripLoss, "FFB", "FFBGripLoss", 1);
            Percent("Weight transfer", Settings::FFBWeightTransfer, "FFB", "FFBWeightTransfer");
            Percent("Wall impact", Settings::FFBWallImpact, "FFB", "FFBWallImpact");
            Percent("Gear shift", Settings::FFBGearShift, "FFB", "FFBGearShift");
            Percent("Road texture", Settings::FFBRoadTexture, "FFB", "FFBRoadTexture");
            Percent("Tire slip", Settings::FFBTireSlip, "FFB", "FFBTireSlip");
            Percent("Engine idle", Settings::FFBEngineIdle, "FFB", "FFBEngineIdle", 1);
            if (ImGui::SliderFloat("Lateral noise floor", &Settings::FFBLateralDeadzone, 0, 10, "%.2f"))
                Queue("FFB", "FFBLateralDeadzone", Settings::FFBLateralDeadzone);
            if (ImGui::IsItemDeactivatedAfterEdit()) Save("FFB", "FFBLateralDeadzone", Settings::FFBLateralDeadzone);
            ImGui::EndDisabled();
            if (Choice("Invert force", Settings::FFBInvertForce)) Save("FFB", "FFBInvertForce", Settings::FFBInvertForce);
            if (Choice("Diagnostic log", Settings::FFBDiagnosticLog)) Save("FFB", "FFBDiagnosticLog", Settings::FFBDiagnosticLog);
            ImGui::TextUnformatted("Wheel torque calibration: not implemented by this adapter");
            ImGui::Text("Periodic effects: %s (startup configuration)", Settings::FFBUsePeriodicEffects ? "On" : "Off");
        }
        break;
    case Page::Cameras:
        ButtonRow("Change camera", Settings::DIRemapButtonChangeView, "ButtonChangeView", input);
        ImGui::TextWrapped("Change camera uses the game's normal camera rotation.");
        ImGui::TextWrapped("Bonnet / Bumper mounts and live adjustment shortcuts have not been implemented in this adapter yet. No camera shortcut is advertised as active.");
        CapturePanel(input);
        break;
    case Page::Telemetry:
    {
        bool enabled = Settings::TelemetryEnabled;
        if (Choice("Telemetry", enabled))
        {
            Telemetry::SetEnabled(enabled);
            Save("Telemetry", "Enable", enabled);
        }
        ImGui::TextUnformatted("Receiver: Forza Motorsport 7 / SimHub");
        ImGui::TextUnformatted("Destination: 127.0.0.1:8000");
        ImGui::TextWrapped("Status: %s", Telemetry::UiStatus());
        if (ImGui::CollapsingHeader("Receiver setup"))
            ImGui::TextWrapped("Configure your receiver for Forza Motorsport 7 Data Out, UDP port 8000 on this PC. Sending does not confirm that a receiver is connected.");
        if (advanced)
        {
            ImGui::Text("Shared memory: %s", Settings::TelemetrySharedMemName.c_str());
            ImGui::TextWrapped("Packet: FM7 Dash, 311 bytes. Destination/protocol are fixed by this adapter. RPM is synthesized; speed scale and steering signal remain unverified. Unavailable packet fields contain zero, not measured zero.");
        }
        break;
    }
    case Page::Help:
        ImGui::TextUnformatted("OutRun2006Tweaks " MODULE_VERSION_STR);
        ImGui::TextUnformatted("F6: settings | F8: Stop FFB | Esc: cancel or close");
        ImGui::TextWrapped("No input: reconnect the wheel, exit normally, then restart. Check Controls before driving. No FFB: check both DLLs are beside the game and choose On in FFB.");
        if (ImGui::SliderFloat("UI scale", &Overlay::GlobalFontScale, 1.0f, 2.5f, "%.2fx"))
            ImGui::GetIO().FontGlobalScale = Overlay::GlobalFontScale;
        if (ImGui::IsItemDeactivatedAfterEdit() && !Overlay::settings_write())
        {
            saveFailed = true;
            saveError = "UI scale could not be saved; retry with the scale control";
        }
        if (ImGui::Button("Open settings folder"))
            ShellExecuteW(nullptr, L"open", Module::DllPath.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        ImGui::TextWrapped("Support-file creation is not implemented. Logs are local beside the game; nothing is uploaded.");
        if (advanced)
        {
            ImGui::TextWrapped("Log: %s", Module::LogPath.string().c_str());
            ImGui::TextWrapped("User settings: %s", Module::UserIniPath.string().c_str());
            ImGui::TextWrapped("The first settings edit keeps a .before-wheel-settings.bak backup.");
            ImGui::TextWrapped("F11 retains the upstream developer tools, including graphics, chat and course editing. Those windows have not been reconciled to the wheel UX standard.");
        }
        break;
    }
}
}

class WheelSettingsWindow : public OverlayWindow
{
public:
    void init() override {}
    void render(bool) override
    {
        using namespace WheelSettingsUi;
        if (!Overlay::WheelSettingsVisible) return;
        Overlay::IsActive = true;
        FFB::ZeroAllForces();
        const auto input = DInputRemap::ReadUiSnapshot();
        const bool wasCapturing = IsCapturing();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !wasCapturing && !Overlay::IsBindingDialogActive)
        {
            if (!pending.empty()) SavePending();
            Overlay::WheelSettingsVisible = false;
            ForceShowCursor(overlay_visible);
            return;
        }
        const auto screen = ImGui::GetIO().DisplaySize;
        const float scale = UiScale();
        static float lastScale = 0;
        ImGui::SetNextWindowSize(ImVec2(std::min(1100.0f * scale, screen.x - 32), std::min(760.0f * scale, screen.y - 32)),
            scale == lastScale ? ImGuiCond_FirstUseEver : ImGuiCond_Always);
        lastScale = scale;
        ImGui::SetNextWindowPos(ImVec2(screen.x / 2, screen.y / 2), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        if (Overlay::WheelSettingsFont) ImGui::PushFont(Overlay::WheelSettingsFont);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(.106f, .125f, .149f, 1));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(.14f, .169f, .20f, 1));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(.14f, .169f, .20f, 1));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.14f, .169f, .20f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.20f, .25f, .29f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.16f, .24f, .29f, 1));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(.54f, .847f, .933f, 1));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(.54f, .847f, .933f, 1));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(.54f, .847f, .933f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.94f, .95f, .97f, 1));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(.714f, .757f, .807f, 1));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(.27f, .314f, .369f, 1));
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(.54f, .847f, .933f, 1));
        if (ImGui::Begin("Wheel settings", nullptr, ImGuiWindowFlags_NoCollapse))
        {
            bool advanced = WheelSettingsPolicy::IsAdvanced(Settings::WheelSettingsView);
            ImGui::TextUnformatted("View:");
            ImGui::SameLine();
            ImGui::BeginDisabled(IsCapturing() || Overlay::IsBindingDialogActive);
            if (ImGui::RadioButton("Simple", !advanced))
            {
                Settings::WheelSettingsView = "Simple";
                Save("WheelSettings", "View", Settings::WheelSettingsView);
                advanced = false;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Advanced", advanced))
            {
                Settings::WheelSettingsView = "Advanced";
                Save("WheelSettings", "View", Settings::WheelSettingsView);
                advanced = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Stop FFB")) StopFfb();
            ImGui::SameLine();
            ImGui::BeginDisabled(IsCapturing());
            if (ImGui::Button("Close"))
            {
                if (!pending.empty()) SavePending();
                Overlay::WheelSettingsVisible = false;
                ForceShowCursor(overlay_visible);
            }
            ImGui::EndDisabled();
            if (saveFailed)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.3f, 1));
                ImGui::TextWrapped("Not saved - %s", saveError.c_str());
                ImGui::PopStyleColor();
                if (!pending.empty() && ImGui::Button("Retry save")) SavePending();
            }
            else ImGui::TextDisabled("Changes save automatically");
            ImGui::TextWrapped("FFB: %s", FFB::UiStatus());
            if (!advanced && CustomTuning())
            {
                ImGui::TextUnformatted("Custom tuning active");
                ImGui::SameLine();
                ImGui::BeginDisabled(IsCapturing());
                if (ImGui::Button("Review in Advanced"))
                {
                    Settings::WheelSettingsView = "Advanced";
                    Save("WheelSettings", "View", Settings::WheelSettingsView);
                    advanced = true;
                }
                ImGui::EndDisabled();
            }
            static const char* pages[] = { "Setup", "Controls", "FFB", "Cameras", "Telemetry", "Help" };
            ImGui::Separator();
            ImGui::BeginChild("Navigation", ImVec2(136 * scale, 0), false);
            ImGui::BeginDisabled(IsCapturing());
            for (int i = 0; i < 6; ++i)
            {
                const bool selected = page == static_cast<Page>(i);
                if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(pages[i], ImVec2(-1, 36 * scale))) page = static_cast<Page>(i);
                if (selected) ImGui::PopStyleColor();
            }
            ImGui::EndDisabled();
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("Page contents", ImVec2(0, 0), false);
            Contents(input, advanced);
            ImGui::EndChild();
            if (!ImGui::IsAnyItemActive() && !saveFailed && !pending.empty()) SavePending();
        }
        ImGui::End();
        ImGui::PopStyleColor(13);
        if (Overlay::WheelSettingsFont) ImGui::PopFont();
        ImGui::PopStyleVar(3);
    }
    static WheelSettingsWindow instance;
};
WheelSettingsWindow WheelSettingsWindow::instance;
