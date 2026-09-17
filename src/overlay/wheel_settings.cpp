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
};
static Capture capture;
bool IsCapturing() { return capture.destination != nullptr; }

static bool Choice(const char* label, bool& value)
{
    ImGui::PushID(label);
    ImGui::Text("%s:", label);
    ImGui::SameLine(180);
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
    ImGui::SameLine(180);
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

static void CapturePanel(const DInputRemap::UiSnapshot& input)
{
    if (!IsCapturing()) return;
    if (!input.connected || GetForegroundWindow() != Game::GameHwnd() || ImGui::GetTime() > capture.deadline ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { capture = {}; return; }
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
            capture = {};
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
        capture = {};
    }
}

static void AxisRow(const char* label, int& axis, bool& invert, const char* key, const char* invertKey,
    float value, const DInputRemap::UiSnapshot& input)
{
    ImGui::PushID(label);
    ImGui::Text("%s - Axis %d", label, axis + 1);
    ImGui::SameLine();
    ImGui::BeginDisabled(!input.connected || IsCapturing());
    if (ImGui::Button("Bind")) BeginCapture(axis, label, key, true, input);
    ImGui::EndDisabled();
    if (input.connected)
    {
        char text[40];
        sprintf_s(text, "%+.0f%%", value * 100.0f);
        ImGui::ProgressBar(std::clamp(value, 0.0f, 1.0f), ImVec2(-1, 0), text);
    }
    else ImGui::TextDisabled("Device input unavailable");
    if (Choice("Invert", invert)) Save("DirectInput", invertKey, invert);
    ImGui::PopID();
}

static void ButtonRow(const char* label, int& button, const char* key, const DInputRemap::UiSnapshot& input)
{
    ImGui::PushID(key);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(180);
    if (button >= 0) ImGui::Text("Button %d", button + 1); else ImGui::TextDisabled("Not bound");
    ImGui::SameLine(310);
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
    ImGui::SameLine(180);
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
            ImGui::Text("Wheel: %s", input.name.c_str());
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
        ImGui::Text("Wheel: %s", input.connected ? input.name.c_str() : "Unavailable");
        ImGui::BeginDisabled(IsCapturing());
        AxisRow("Steering", Settings::DIRemapSteeringAxis, Settings::DIRemapSteeringInvert, "SteeringAxis", "SteeringInvert", input.steering, input);
        AxisRow("Throttle", Settings::DIRemapAccelAxis, Settings::DIRemapAccelInvert, "AccelerationAxis", "AccelerationInvert", input.throttle, input);
        AxisRow("Brake", Settings::DIRemapBrakeAxis, Settings::DIRemapBrakeInvert, "BrakeAxis", "BrakeInvert", input.brake, input);
        Percent("Steering deadzone", Settings::SteeringDeadZone, "Controls", "SteeringDeadZone", 0.95f);
        ImGui::TextWrapped("Binding uses the device's full range. Guided endpoint calibration and separate pedal-device binding are not implemented yet.");
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
        ImGui::SetNextWindowSize(ImVec2(std::min(1100.0f, screen.x - 32), std::min(760.0f, screen.y - 32)), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(screen.x / 2, screen.y / 2), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
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
            ImGui::BeginChild("Navigation", ImVec2(136, 0), false);
            ImGui::BeginDisabled(IsCapturing());
            for (int i = 0; i < 6; ++i)
            {
                const bool selected = page == static_cast<Page>(i);
                if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(pages[i], ImVec2(-1, 36))) page = static_cast<Page>(i);
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
        ImGui::PopStyleVar(3);
    }
    static WheelSettingsWindow instance;
};
WheelSettingsWindow WheelSettingsWindow::instance;
