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

static int zeros = 0;
static DInputRemap::UiSnapshot fixtureInput;
bool overlay_visible = false;
void ForceShowCursor(bool) {}
OverlayWindow::OverlayWindow() {}
bool Overlay::settings_write() { return true; }
namespace DInputRemap { UiSnapshot ReadUiSnapshot() { return fixtureInput; } }
namespace FFB
{
void ZeroAllForces() { ++zeros; }
const char* UiStatus() { return Settings::DirectInputFFB ? "Paused while settings are open" : "Off"; }
static std::vector<DeviceChoice> choices;
const std::vector<DeviceChoice>& UiDevices() { return choices; }
void RefreshUiDevices() {}
void SelectionChanged() { ++zeros; }
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
    unsigned char* pixels; int fw, fh;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &fw, &fh);
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
            }
    assert(Settings::FFBSpringStrength == 0.29f);
    assert(!Settings::DirectInputFFB && !Settings::TelemetryEnabled);
    assert(zeros > 0);
    fixtureInput.connected = true;
    fixtureInput.name = "Fixture wheel";
    int oldButton = 9;
    BeginCapture(oldButton, "Change camera", "ButtonChangeView", false, fixtureInput);
    fixtureInput.buttons[3] = true;
    Frame(Page::Cameras, false, 1280, 720);
    assert(capture.candidate == 3 && oldButton == 9);
    fixtureInput.connected = false;
    Frame(Page::Cameras, false, 1280, 720);
    assert(!IsCapturing() && oldButton == 9);
    Settings::DirectInputFFB = true;
    StopFfb();
    assert(!Settings::DirectInputFFB);
    assert(Read(Module::UserIniPath).find("DirectInputFFB = false") != std::string::npos);
    ImGui::DestroyContext();
    std::cout << "PASS: migration, old tune/Off preservation, atomic write failure/retry, backup, 24 UI frames, Simple filtering, capture disconnect rollback, persistent Stop FFB.\n";
}
