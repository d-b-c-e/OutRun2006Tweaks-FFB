// Offline: actual player UI + mock input/output. Never loads the game or WheelFfb.dll.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
static HWND FixtureForeground() { return reinterpret_cast<HWND>(1); }
#define GetForegroundWindow FixtureForeground
#include "../../src/overlay/wheel_settings.cpp"
#include <imgui_internal.h>
#include <cassert>
#include <iostream>
#include "../../external/ini-cpp/ini/ini.h"

static int zeros = 0;
static int selectionZero = -1, primaryAdoptions = 0;
static DInputRemap::UiSnapshot fixtureInput;
bool overlay_visible = false;
bool InputManager_FunctionKeyBound(int key) { return key == VK_F9; } // Saved SDL binding conflict.
void ForceShowCursor(bool) {}
OverlayWindow::OverlayWindow() {}
bool Overlay::settings_write() { return true; }
static DInputRemap::UiSnapshot fixturePedal;
namespace DInputRemap
{
UiSnapshot ReadUiSnapshot() { return fixtureInput; }
UiSnapshot ReadAxisUiSnapshot(int role) { return role > 0 && !fixturePedal.guid.empty() ? fixturePedal : fixtureInput; }
UiSnapshot ReadDeviceUiSnapshot(const std::string& guid) { return guid == fixturePedal.guid ? fixturePedal : fixtureInput; }
static std::vector<InputDeviceChoice> inputChoices;
const std::vector<InputDeviceChoice>& UiInputDevices() { return inputChoices; }
void RefreshUiInputDevices() {}
void ReleaseUnusedUiDevices() {}
std::string PrimaryInputGuid() { return fixtureInput.guid; }
bool CanAdoptPrimaryInput(const std::string&) { return true; }
void AdoptPrimaryInput(const std::string& guid) { if (guid != fixtureInput.guid) assert(selectionZero == zeros); ++primaryAdoptions; }
}
namespace FFB
{
void ZeroAllForces() { ++zeros; }
const char* UiStatus() { return Settings::DirectInputFFB ? "Paused while settings are open" : "Off"; }
static std::vector<DeviceChoice> choices;
const std::vector<DeviceChoice>& UiDevices() { return choices; }
void RefreshUiDevices() {}
void SelectionChanged() { selectionZero = ++zeros; }
std::string UiDeviceLabel() { return "Use steering wheel - Fixture wheel"; }
}
namespace Telemetry
{
void SetEnabled(bool enabled) { Settings::TelemetryEnabled = enabled; }
const char* UiStatus() { return Settings::TelemetryEnabled ? "Waiting for a live car update" : "Off"; }
}

static std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
static void ExportDrawData(const std::filesystem::path& path)
{
    const auto* data = ImGui::GetDrawData();
    std::ofstream output(path);
    output << "{\"size\":[" << data->DisplaySize.x << ',' << data->DisplaySize.y << "],\"scale\":" << 1.5f * WheelSettingsUi::UiScale() << ",\"lists\":[";
    for (int li = 0; li < data->CmdListsCount; ++li)
    {
        if (li) output << ',';
        const auto* list = data->CmdLists[li];
        output << "{\"vertices\":[";
        for (int i = 0; i < list->VtxBuffer.Size; ++i)
        {
            if (i) output << ',';
            const auto& v = list->VtxBuffer[i];
            output << '[' << v.pos.x << ',' << v.pos.y << ',' << v.uv.x << ',' << v.uv.y << ',' << v.col << ']';
        }
        output << "],\"indices\":[";
        for (int i = 0; i < list->IdxBuffer.Size; ++i) { if (i) output << ','; output << list->IdxBuffer[i]; }
        output << "],\"commands\":[";
        for (int i = 0; i < list->CmdBuffer.Size; ++i)
        {
            if (i) output << ',';
            const auto& c = list->CmdBuffer[i];
            output << '[' << c.IdxOffset << ',' << c.VtxOffset << ',' << c.ElemCount << ',' << c.ClipRect.x << ',' << c.ClipRect.y << ',' << c.ClipRect.z << ',' << c.ClipRect.w << ']';
        }
        output << "]}";
    }
    output << "],\"windows\":[";
    bool firstWindow = true;
    for (const auto* window : ImGui::GetCurrentContext()->Windows)
    {
        if (!window->Active || window->Hidden) continue;
        if (!firstWindow) output << ',';
        firstWindow = false;
        output << "{\"bounds\":[" << window->Pos.x << ',' << window->Pos.y << ',' << window->Size.x << ',' << window->Size.y
            << "],\"scrollMax\":[" << window->ScrollMax.x << ',' << window->ScrollMax.y << "],\"content\":["
            << window->ContentSize.x << ',' << window->ContentSize.y << "]}";
        assert(window->Pos.x >= 0 && window->Pos.y >= 0);
        assert(window->Pos.x + window->Size.x <= data->DisplaySize.x + 1);
        assert(window->Pos.y + window->Size.y <= data->DisplaySize.y + 1);
    }
    output << "]}";
}
static std::string Frame(WheelSettingsUi::Page page, bool advanced, int width, int height)
{
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DeltaTime = 1.0f / 60;
    WheelSettingsUi::page = page;
    Settings::WheelSettingsView = advanced ? "Advanced" : "Simple";
    Overlay::WheelSettingsVisible = true;
    // First frame computes auto-fitting/window state; log the following frame.
    std::string text;
    for (int i = 0; i < 2; ++i)
    {
        ImGui::NewFrame();
        if (i) ImGui::LogToBuffer();
        WheelSettingsWindow::instance.render(false);
        if (i)
        {
            text = ImGui::GetCurrentContext()->LogBuffer.c_str();
            ImGui::LogFinish();
        }
        ImGui::Render();
    }
    // ImGui's internal log buffer is text, independent of a GPU or native window.
    return text;
}

int main(int argc, char** argv)
{
    using namespace WheelSettingsUi;
    using namespace WheelSettingsPolicy;
    if (argc != 2) return 2;
    const auto directory = std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(directory);
    Module::UserIniPath = directory / "fixture.user.ini";
    Module::DllPath = directory / "dinput8.dll";
    HWND hwnd = reinterpret_cast<HWND>(1);
    Game::hWnd_ptr = &hwnd;
    const std::string old = "; Owner comment\r\n[FFB]\r\nDirectInputFFB = false\r\nFFBGlobalStrength = 0.37\r\nFFBProfile = my-tune@4\r\n\r\n[Unknown]\r\nOpaque = retain me\r\n";
    std::ofstream(Module::UserIniPath, std::ios::binary) << old;
    assert(!IsAdvanced(""));
    assert(!IsAdvanced("invalid"));
    assert(IsAdvanced("Advanced"));
    assert(LayoutScale(1280, 720, 1) == 1 && LayoutScale(3840, 2160, 1) == 2);
    assert(LayoutScale(3840, 2160, 1.5f) == 3);
    assert(!WheelSettingsPolicy::ValidShortcut(VK_ESCAPE) && !WheelSettingsPolicy::ValidShortcut(VK_F11));
    const auto upstreamIni = directory / "old-upstream.ini";
    std::ofstream(upstreamIni) << "[Controls]\nUseNewInput = false\n";
    auto parsed = inih::INIReader(upstreamIni);
    assert(!RequiresLegacyFfbSelection(parsed, -1));
    assert(!RequiresLegacyFfbSelection(parsed, 2)); // No section must never throw.
    std::ofstream(upstreamIni) << "[FFB]\nDirectInputFFB = false\n";
    parsed = inih::INIReader(upstreamIni);
    assert(!RequiresLegacyFfbSelection(parsed, 2)); // Off-only override retains identity.
    std::ofstream(upstreamIni) << "[FFB]\nFFBDevice = 2\n";
    parsed = inih::INIReader(upstreamIni);
    assert(RequiresLegacyFfbSelection(parsed, 2));
    std::ofstream(upstreamIni) << "[FFB]\nFFBDevice = 2\nFFBDeviceGuid = steering\n";
    parsed = inih::INIReader(upstreamIni);
    assert(!RequiresLegacyFfbSelection(parsed, 2));
    assert(Save("WheelSettings", "View", "Advanced"));
    const auto first = Read(Module::UserIniPath);
    assert(first.starts_with(old));
    assert(first.find("View = Advanced") != std::string::npos);
    assert(Save("WheelSettings", "View", "Simple"));
    assert(Read(Module::UserIniPath).starts_with(old));
    auto backup = Module::UserIniPath; backup += L".before-wheel-settings.bak";
    assert(Read(backup) == old);
    const auto lockedContents = Read(Module::UserIniPath);
    HANDLE locked = CreateFileW(Module::UserIniPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    assert(locked != INVALID_HANDLE_VALUE);
    assert(!Save("WheelSettings", "View", "Advanced"));
    assert(Read(Module::UserIniPath) == lockedContents);
    CloseHandle(locked);
    assert(SavePending());
    assert(Read(Module::UserIniPath).find("FFBGlobalStrength = 0.37") != std::string::npos);
    assert(Read(Module::UserIniPath).find("DirectInputFFB = false") != std::string::npos);

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.FontGlobalScale = 1.5f;
    io.Fonts->AddFontDefault();
    Overlay::WheelSettingsFont = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 14.0f);
    unsigned char* pixels; int fw, fh;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &fw, &fh);
    std::ofstream(directory / "font-atlas.rgba", std::ios::binary).write(reinterpret_cast<const char*>(pixels), fw * fh * 4);
    std::ofstream(directory / "font-atlas.json") << "{\"width\":" << fw << ",\"height\":" << fh << '}';
    Settings::FFBSpringStrength = 0.29f;
    Settings::DirectInputFFB = false;
    Settings::TelemetryEnabled = false;
    for (auto size : {ImVec2(1280, 720), ImVec2(3840, 2160)})
        for (int p = 0; p < 6; ++p)
            for (bool advanced : {false, true})
            {
                const auto text = Frame(static_cast<Page>(p), advanced, static_cast<int>(size.x), static_cast<int>(size.y));
                assert(text.find("Stop FFB") != std::string::npos);
                assert(text.find("Close") != std::string::npos);
                if (p == 2)
                {
                    assert(text.find("Strength") != std::string::npos);
                    assert((text.find("Centering") != std::string::npos) == advanced);
                    assert((text.find("Custom tuning active") != std::string::npos) == !advanced);
                }
                std::ofstream(directory / (std::to_string(static_cast<int>(size.x)) + "-page" + std::to_string(p) + (advanced ? "-advanced.txt" : "-simple.txt"))) << text;
                ExportDrawData(directory / (std::to_string(static_cast<int>(size.x)) + "-page" + std::to_string(p) + (advanced ? "-advanced.json" : "-simple.json")));
            }
    fixtureInput.connected = true;
    fixtureInput.name = "Example wheel with a deliberately long device name for layout verification";
    Settings::UseDirectInputRemap = true;
    Frame(Page::Setup, false, 1280, 720);
    ExportDrawData(directory / "1280-long-device-simple.json");
    Settings::WheelSettingsScale = 1.5f;
    Frame(Page::Ffb, false, 3840, 2160);
    ExportDrawData(directory / "3840-large-scale-simple.json");
    Settings::WheelSettingsScale = 1.0f;
    assert(Settings::FFBSpringStrength == 0.29f);
    assert(!Settings::DirectInputFFB && !Settings::TelemetryEnabled);
    assert(zeros > 0);
    fixtureInput.connected = true;
    fixtureInput.name = "Fixture wheel";
    fixtureInput.guid = "{11111111-2222-3333-0405-060708090a0b}";
    // Provisional calibration is one transaction: no binding, identity or
    // endpoints become effective before a successful atomic save.
    Settings::DIRemapDeviceGuid = "old-saved-guid";
    Settings::DIRemapSteeringAxis = 0;
    Settings::DIRemapCalibration[0] = {};
    BeginAxisCalibration(Settings::DIRemapSteeringAxis, "SteeringAxis", 0, false, fixtureInput);
    const auto restFrame = Frame(Page::Controls, false, 1280, 720);
    assert(restFrame.find("Capture center") != std::string::npos);
    ExportDrawData(directory / "1280-calibration-rest-simple.json");
    capture.candidate = 3; capture.stage = 2;
    capture.calibration = {true, 1000, 26000, 61000};
    capture.invert = true; capture.deadzone = .04f;
    fixtureInput.axes[3] = 26000;
    const auto preview = Frame(Page::Controls, false, 1280, 720);
    assert(preview.find("Save calibration") != std::string::npos && preview.find("Cancel") != std::string::npos);
    ExportDrawData(directory / "1280-calibration-preview-simple.json");
    const auto beforeCalibration = Read(Module::UserIniPath);
    assert(Settings::DIRemapSteeringAxis == 0 && !Settings::DIRemapCalibration[0].enabled);
    locked = CreateFileW(Module::UserIniPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    assert(locked != INVALID_HANDLE_VALUE);
    assert(!SaveAxisCalibration(fixtureInput));
    assert(pending.empty() && Read(Module::UserIniPath) == beforeCalibration);
    assert(Settings::DIRemapSteeringAxis == 0 && Settings::DIRemapDeviceGuid == "old-saved-guid" && !Settings::DIRemapCalibration[0].enabled);
    CloseHandle(locked);
    fixtureInput.connected = false;
    Frame(Page::Controls, false, 1280, 720);
    assert(!IsCapturing());
    StopFfb(); // A later unrelated save must not commit cancelled calibration.
    assert(Read(Module::UserIniPath).find("SteeringEnabled") == std::string::npos);
    fixtureInput.connected = true;
    BeginAxisCalibration(Settings::DIRemapSteeringAxis, "SteeringAxis", 0, false, fixtureInput);
    capture.candidate = 3; capture.stage = 2;
    capture.calibration = {true, 1000, 26000, 61000};
    capture.invert = true; capture.deadzone = .04f;
    auto wrongDevice = fixtureInput; wrongDevice.guid = "different-guid";
    assert(!SaveAxisCalibration(wrongDevice));
    capture.calibration.center = 62000;
    assert(!SaveAxisCalibration(fixtureInput));
    assert(pending.empty() && Settings::DIRemapSteeringAxis == 0);
    capture.calibration.center = 26000;
    assert(SaveAxisCalibration(fixtureInput));
    assert(Settings::DIRemapSteeringAxis == 3 && Settings::DIRemapDeviceGuid == fixtureInput.guid);
    assert(Settings::DIRemapCalibration[0].enabled && Settings::DIRemapCalibration[0].center == 26000);
    assert(Settings::DIRemapSteeringInvert && Settings::SteeringDeadZone == .04f);
    const auto calibrated = Read(Module::UserIniPath);
    for (const auto* expected : {"SteeringAxis = 3", "SteeringEnabled = true", "SteeringCenter = 26000", "DeviceGuid = {11111111-2222-3333-0405-060708090a0b}"})
        assert(calibrated.find(expected) != std::string::npos);
    capture = {};
    // Replacing an axis begins with fresh endpoints, never the previous range.
    BeginAxisCalibration(Settings::DIRemapSteeringAxis, "SteeringAxis", 0, false, fixtureInput);
    assert(!capture.calibration.enabled && capture.stage == 0 && capture.candidate == -1);
    capture = {};
    Frame(Page::Controls, false, 1280, 720);
    ExportDrawData(directory / "1280-controls-connected-simple.json");
    // A separate pedal selection stays provisional and cannot alter Steering
    // or its FFB-follow identity, including after a failed save/cancellation.
    fixturePedal = fixtureInput;
    fixturePedal.guid = "{22222222-2222-3333-0405-060708090a0b}";
    fixturePedal.name = "Fixture USB pedals";
    DInputRemap::inputChoices = {{fixtureInput.guid, fixtureInput.name}, {fixturePedal.guid, fixturePedal.name}};
    BeginAxisCalibration(Settings::DIRemapAccelAxis, "AccelerationAxis", 1, false, fixturePedal);
    auto pedalFrame = Frame(Page::Controls, false, 1280, 720);
    assert(pedalFrame.find("Input device") != std::string::npos && pedalFrame.find("Capture rest") != std::string::npos);
    ExportDrawData(directory / "1280-separate-pedal-device-simple.json");
    capture.candidate = 4; capture.stage = 2;
    capture.calibration = {true, 5000, 30000, 55000}; capture.invert = true; capture.deadzone = .02f;
    const auto oldThrottleAxis = Settings::DIRemapAccelAxis;
    const auto steeringIdentity = Settings::DIRemapDeviceGuid;
    locked = CreateFileW(Module::UserIniPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    assert(locked != INVALID_HANDLE_VALUE);
    assert(!SaveAxisCalibration(fixturePedal));
    assert(Settings::DIRemapAccelDeviceGuid.empty() && Settings::DIRemapAccelAxis == oldThrottleAxis && pending.empty());
    CloseHandle(locked);
    const int zeroCount = zeros;
    assert(SaveAxisCalibration(fixturePedal));
    assert(Settings::DIRemapAccelDeviceGuid == fixturePedal.guid && Settings::DIRemapAccelDeviceName == fixturePedal.name);
    assert(Settings::DIRemapAccelAxis == 4 && Settings::DIRemapCalibration[1].enabled);
    assert(Settings::DIRemapDeviceGuid == steeringIdentity && zeros == zeroCount);
    assert(Read(Module::UserIniPath).find("ThrottleDeviceGuid = " + fixturePedal.guid) != std::string::npos);
    EndCapture();
    auto newWheel = fixtureInput;
    newWheel.guid = "{33333333-2222-3333-0405-060708090a0b}";
    newWheel.name = "Replacement wheel";
    const auto previousPedalGuid = Settings::DIRemapAccelDeviceGuid;
    BeginAxisCalibration(Settings::DIRemapSteeringAxis, "SteeringAxis", 0, false, newWheel);
    capture.candidate = 0; capture.stage = 2;
    capture.calibration = {true, 1000, 30000, 61000};
    assert(SaveAxisCalibration(newWheel));
    assert(Settings::DIRemapDeviceGuid == newWheel.guid && primaryAdoptions >= 2);
    assert(Settings::DIRemapAccelDeviceGuid == previousPedalGuid); // Explicit pedal override retained.
    assert(Settings::DIRemapBrakeDeviceGuid == fixtureInput.guid); // Old primary source pinned.
    assert(Read(Module::UserIniPath).find("BrakeDeviceGuid = " + fixtureInput.guid) != std::string::npos);
    EndCapture();
    fixturePedal = {};
    int oldButton = 9;
    BeginCapture(oldButton, "Change camera", "ButtonChangeView", false, fixtureInput);
    fixtureInput.buttons[3] = true;
    Frame(Page::Cameras, false, 1280, 720);
    assert(capture.candidate == 3 && oldButton == 9);
    fixtureInput.connected = false;
    Frame(Page::Cameras, false, 1280, 720);
    assert(!IsCapturing() && oldButton == 9);
    fixtureInput.connected = true;
    fixturePedal.connected = true; fixturePedal.guid = "{44444444-2222-3333-0405-060708090a0b}";
    fixturePedal.name = "Separate shifter with a long device name";
    DInputRemap::inputChoices.push_back({fixturePedal.guid, fixturePedal.name});
    Settings::DIShifterDeviceGuid = fixturePedal.guid;
    Settings::DIShifterGearMode = "hpattern";
    Frame(Page::Controls, false, 1280, 720);
    for (auto* window : ImGui::GetCurrentContext()->Windows)
        if (std::string(window->Name).find("Page contents") != std::string::npos)
            window->StateStorage.SetInt(window->GetID("Separate shifter"), 1);
    Frame(Page::Controls, false, 1280, 720);
    ExportDrawData(directory / "1280-optional-shifter-simple.json");
    BeginCapture(Settings::DIShifterButtonGear1, "Gear 1", "ButtonGear1", false, fixturePedal, "DirectInput.Shifter");
    auto buttonFrame = Frame(Page::Controls, false, 1280, 720);
    assert(buttonFrame.find("Input device") != std::string::npos && buttonFrame.find("Cancel") != std::string::npos);
    ExportDrawData(directory / "1280-optional-button-capture-simple.json");
    capture.candidate = 89;
    const auto previousShifterButton = Settings::DIShifterButtonGear1;
    locked = CreateFileW(Module::UserIniPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    assert(!SaveCapturedBinding(fixturePedal));
    assert(Settings::DIShifterButtonGear1 == previousShifterButton && pending.empty());
    CloseHandle(locked);
    assert(SaveCapturedBinding(fixturePedal) && Settings::DIShifterButtonGear1 == 89);
    assert(Settings::DIShifterDeviceGuid == fixturePedal.guid);
    EndCapture();
    // Picker switched A -> B after this frame read A. An A press must never
    // become a B binding; the production observer rearms on B's own snapshot.
    BeginCapture(Settings::DIAuxButtonChangeView, "Change camera", "ButtonChangeView", false, fixtureInput, "DirectInput.Aux");
    auto oldDeviceFrame = fixtureInput; oldDeviceFrame.buttons[91] = true;
    capture.initial = fixturePedal;
    ObserveButtonCapture(oldDeviceFrame);
    assert(capture.candidate == -1 && oldDeviceFrame.guid == fixturePedal.guid);
    auto newDeviceFrame = fixturePedal; newDeviceFrame.buttons[92] = true;
    ObserveButtonCapture(newDeviceFrame); assert(capture.candidate == 92);
    EndCapture();
    // A whole-role device change must preflight retained buttons as well as
    // the proposed Camera button: moved Confirm would conflict with Back.
    Settings::DIAuxDeviceGuid = fixturePedal.guid; Settings::DIAuxButtonA = 73;
    const int previousBack = Settings::DIRemapButtonB; Settings::DIRemapButtonB = 73;
    BeginCapture(Settings::DIAuxButtonChangeView, "Change camera", "ButtonChangeView", false, fixtureInput, "DirectInput.Aux");
    capture.candidate = 90;
    assert(!SaveCapturedBinding(fixtureInput));
    assert(Settings::DIAuxDeviceGuid == fixturePedal.guid && capture.message.find("group's saved buttons") != std::string::npos);
    EndCapture();
    Settings::DIRemapButtonB = previousBack; Settings::DIAuxButtonA = -1; Settings::DIAuxDeviceGuid.clear();
    BeginCapture(Settings::DIShifterButtonGear2, "Gear 2", "ButtonGear2", false, fixturePedal, "DirectInput.Shifter");
    capture.candidate = 89;
    assert(!SaveCapturedBinding(fixturePedal) && capture.message.find("already assigned") != std::string::npos);
    EndCapture();
    BeginCapture(Settings::DIShifterButtonGear1, "Gear 1", "ButtonGear1", false, fixturePedal, "DirectInput.Shifter");
    capture.clear = true;
    locked = CreateFileW(Module::UserIniPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    assert(!SaveCapturedBinding(fixturePedal) && Settings::DIShifterButtonGear1 == 89 && pending.empty());
    CloseHandle(locked);
    assert(SaveCapturedBinding(fixturePedal) && Settings::DIShifterButtonGear1 == -1);
    EndCapture();
    BeginCapture(Settings::WheelSettingsKey, "Settings", "SettingsKey", false, {}, "WheelSettings");
    capture.keyboard = true; capture.candidate = Settings::WheelStopKey;
    assert(!SaveCapturedBinding({}));
    capture.candidate = VK_F11; assert(!SaveCapturedBinding({}));
    capture.candidate = VK_F9; assert(!SaveCapturedBinding({}));
    Settings::HudToggleKey = "F3"; capture.candidate = VK_F3; assert(!SaveCapturedBinding({}));
    Settings::HudToggleKey.clear();
    capture.candidate = VK_F4;
    auto shortcutFrame = Frame(Page::Controls, false, 1280, 720);
    assert(shortcutFrame.find("Detected: F4") != std::string::npos);
    ExportDrawData(directory / "1280-shortcut-capture-simple.json");
    assert(SaveCapturedBinding({}) && Settings::WheelSettingsKey == VK_F4);
    EndCapture();
    assert(Read(Module::UserIniPath).find("SettingsKey = 115") != std::string::npos);
    Settings::DirectInputFFB = true;
    StopFfb();
    assert(!Settings::DirectInputFFB);
    assert(Read(Module::UserIniPath).find("DirectInputFFB = false") != std::string::npos);
    ImGui::DestroyContext();
    std::cout << "PASS: actual INI parser migration (absent FFB section and layered selection), old tune/Off preservation, atomic failure/retry, 30 UI frames/bounds, calibration device/axis/endpoints transaction, primary replacement/pedal preservation/zero-before-swap and persistent Stop FFB.\n";
}
