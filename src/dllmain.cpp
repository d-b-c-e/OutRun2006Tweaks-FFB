#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#include <filesystem>
#include <ini.h>

#include "hook_mgr.hpp"
#include "resource.h"
#include "plugin.hpp"
#include "game_addrs.hpp"
#include "wheel_settings_policy.hpp"
#include "signal_mute_policy.hpp"
#include "profile_controls.hpp"
#include "remap_inject.hpp"

void InitExceptionHandler(); // hooks_exceptions.cpp

namespace Module
{
	constexpr std::string_view TargetFilename = "OR2006C2C.exe";

	constexpr std::string_view IniFileName = "OutRun2006Tweaks.ini";
	constexpr std::string_view UserIniFileName = "OutRun2006Tweaks.user.ini";
	constexpr std::string_view LodIniFileName = "OutRun2006Tweaks.lods.ini";
	constexpr std::string_view OverlayIniFileName = "OutRun2006Tweaks.overlay.ini";
	constexpr std::string_view BindingsIniFileName = "OutRun2006Tweaks.input.ini";
	constexpr std::string_view LogFileName = "OutRun2006Tweaks.log";

	void init()
	{
		if (!DllHandle)
			return;

		ExeHandle = GetModuleHandle(nullptr);

		// Fetch paths of the DLL & EXE
		DllPath = Util::GetModuleFilePath(DllHandle);
		ExePath = Util::GetModuleFilePath(ExeHandle);

		// Setup Log & INI paths, always located next to the DLL instead of the EXE
		auto dllParent = DllPath.parent_path();
		LogPath = dllParent / LogFileName;
		IniPath = dllParent / IniFileName;
		UserIniPath = dllParent / UserIniFileName;
		LodIniPath = dllParent / LodIniFileName;
		OverlayIniPath = dllParent / OverlayIniFileName;
		BindingsIniPath = dllParent / BindingsIniFileName;

		Game::init();
	}

	void to_log()
	{
		// Print some info about the users setup to log, can come in useful for debugging issues
		spdlog::info("EXE module (base address: {:p}):", fmt::ptr(ExeHandle));
		spdlog::info("  File path: {}", ExePath.string());
		spdlog::info("  Header timestamp: {}", Util::GetModuleTimestamp(ExeHandle));
		spdlog::info("DLL module (base address: {:p}):", fmt::ptr(DllHandle));
		spdlog::info("  File path: {}", DllPath.string());
	}
};

namespace Settings
{
	bool read_legacy(const std::filesystem::path& iniPath)
	{
		spdlog::info("Settings::read_legacy - reading INI from {}", iniPath.string());

		inih::INIReader ini;
		try
		{
			ini = inih::INIReader(iniPath);
		}
		catch (...)
		{
			spdlog::error("Settings::read_legacy - INI read failed! The file might not exist, or may have duplicate settings inside");
			return false;
		}


		{
			std::string screens = TripleScreens == 2 ? "Separate monitors" : TripleScreens == 0 ? "Off" : "Surround";
			screens = ini.Get("Triple", "Screens", screens);
			TripleScreens = (_stricmp(screens.c_str(), "Separate monitors") == 0 || _stricmp(screens.c_str(), "Separate") == 0) ? 2
				: _stricmp(screens.c_str(), "Off") == 0 ? 0 : 1;
		}








		UseDirectInputRemap = ini.Get("DirectInput", "UseDirectInputRemap", UseDirectInputRemap);
		DIRemapDeviceGuid = ini.Get("DirectInput", "DeviceGuid", DIRemapDeviceGuid);
		DIRemapSteeringAxis = ini.Get("DirectInput", "SteeringAxis", DIRemapSteeringAxis);
		DIRemapSteeringAxis = std::clamp(DIRemapSteeringAxis, -1, 7);
		DIRemapSteeringInvert = ini.Get("DirectInput", "SteeringInvert", DIRemapSteeringInvert);
		DIRemapSteeringSensitivity = ini.Get("DirectInput", "SteeringSensitivity", DIRemapSteeringSensitivity);
		DIRemapSteeringSensitivity = std::clamp(DIRemapSteeringSensitivity, 0.1f, 10.0f);
		DIRemapAccelAxis = ini.Get("DirectInput", "AccelerationAxis", DIRemapAccelAxis);
		DIRemapAccelAxis = std::clamp(DIRemapAccelAxis, -1, 7);
		DIRemapAccelInvert = ini.Get("DirectInput", "AccelerationInvert", DIRemapAccelInvert);
		DIRemapBrakeAxis = ini.Get("DirectInput", "BrakeAxis", DIRemapBrakeAxis);
		DIRemapBrakeAxis = std::clamp(DIRemapBrakeAxis, -1, 7);
		DIRemapBrakeInvert = ini.Get("DirectInput", "BrakeInvert", DIRemapBrakeInvert);
		DIRemapAccelDeadzone = ini.Get("DirectInput", "AccelerationDeadzone", DIRemapAccelDeadzone);
		DIRemapBrakeDeadzone = ini.Get("DirectInput", "BrakeDeadzone", DIRemapBrakeDeadzone);
		DIRemapAccelDeviceGuid = ini.Get("DirectInput", "ThrottleDeviceGuid", DIRemapAccelDeviceGuid);
		DIRemapAccelDeviceName = ini.Get("DirectInput", "ThrottleDeviceName", DIRemapAccelDeviceName);
		DIRemapBrakeDeviceGuid = ini.Get("DirectInput", "BrakeDeviceGuid", DIRemapBrakeDeviceGuid);
		DIRemapBrakeDeviceName = ini.Get("DirectInput", "BrakeDeviceName", DIRemapBrakeDeviceName);
		DIRemapCalibration[0].enabled = ini.Get("DirectInput.Calibration", "SteeringEnabled", DIRemapCalibration[0].enabled);
		DIRemapCalibration[0].minimum = ini.Get("DirectInput.Calibration", "SteeringMinimum", DIRemapCalibration[0].minimum);
		DIRemapCalibration[0].center = ini.Get("DirectInput.Calibration", "SteeringCenter", DIRemapCalibration[0].center);
		DIRemapCalibration[0].maximum = ini.Get("DirectInput.Calibration", "SteeringMaximum", DIRemapCalibration[0].maximum);
		DIRemapCalibration[1].enabled = ini.Get("DirectInput.Calibration", "ThrottleEnabled", DIRemapCalibration[1].enabled);
		DIRemapCalibration[1].minimum = ini.Get("DirectInput.Calibration", "ThrottleMinimum", DIRemapCalibration[1].minimum);
		DIRemapCalibration[1].center = ini.Get("DirectInput.Calibration", "ThrottleCenter", DIRemapCalibration[1].center);
		DIRemapCalibration[1].maximum = ini.Get("DirectInput.Calibration", "ThrottleMaximum", DIRemapCalibration[1].maximum);
		DIRemapCalibration[2].enabled = ini.Get("DirectInput.Calibration", "BrakeEnabled", DIRemapCalibration[2].enabled);
		DIRemapCalibration[2].minimum = ini.Get("DirectInput.Calibration", "BrakeMinimum", DIRemapCalibration[2].minimum);
		DIRemapCalibration[2].center = ini.Get("DirectInput.Calibration", "BrakeCenter", DIRemapCalibration[2].center);
		DIRemapCalibration[2].maximum = ini.Get("DirectInput.Calibration", "BrakeMaximum", DIRemapCalibration[2].maximum);
		DIRemapButtonA = ini.Get("DirectInput", "ButtonA", DIRemapButtonA);
		DIRemapButtonB = ini.Get("DirectInput", "ButtonB", DIRemapButtonB);
		DIRemapButtonX = ini.Get("DirectInput", "ButtonX", DIRemapButtonX);
		DIRemapButtonY = ini.Get("DirectInput", "ButtonY", DIRemapButtonY);
		DIRemapButtonStart = ini.Get("DirectInput", "ButtonStart", DIRemapButtonStart);
		DIRemapButtonBack = ini.Get("DirectInput", "ButtonBack", DIRemapButtonBack);
		DIRemapButtonGearUp = ini.Get("DirectInput", "ButtonGearUp", DIRemapButtonGearUp);
		DIRemapButtonGearDown = ini.Get("DirectInput", "ButtonGearDown", DIRemapButtonGearDown);
		DIRemapButtonChangeView = ini.Get("DirectInput", "ButtonChangeView", DIRemapButtonChangeView);
		DIRemapButtonSelUp = ini.Get("DirectInput", "ButtonSelUp", DIRemapButtonSelUp);
		DIRemapButtonSelDown = ini.Get("DirectInput", "ButtonSelDown", DIRemapButtonSelDown);
		DIRemapButtonSelLeft = ini.Get("DirectInput", "ButtonSelLeft", DIRemapButtonSelLeft);
		DIRemapButtonSelRight = ini.Get("DirectInput", "ButtonSelRight", DIRemapButtonSelRight);

		// [DirectInput.Shifter] — separate shifter device
		DIShifterDeviceGuid = ini.Get("DirectInput.Shifter", "DeviceGuid", DIShifterDeviceGuid);
		DIShifterEnabled = !DIShifterDeviceGuid.empty();
		DIShifterGearMode = ini.Get("DirectInput.Shifter", "GearMode", DIShifterGearMode);
		DIShifterButtonGearUp = ini.Get("DirectInput.Shifter", "ButtonGearUp", DIShifterButtonGearUp);
		DIShifterButtonGearDown = ini.Get("DirectInput.Shifter", "ButtonGearDown", DIShifterButtonGearDown);
		DIShifterButtonGear1 = ini.Get("DirectInput.Shifter", "ButtonGear1", DIShifterButtonGear1);
		DIShifterButtonGear2 = ini.Get("DirectInput.Shifter", "ButtonGear2", DIShifterButtonGear2);
		DIShifterButtonGear3 = ini.Get("DirectInput.Shifter", "ButtonGear3", DIShifterButtonGear3);
		DIShifterButtonGear4 = ini.Get("DirectInput.Shifter", "ButtonGear4", DIShifterButtonGear4);
		DIShifterButtonGear5 = ini.Get("DirectInput.Shifter", "ButtonGear5", DIShifterButtonGear5);
		DIShifterButtonGear6 = ini.Get("DirectInput.Shifter", "ButtonGear6", DIShifterButtonGear6);
		DIShifterButtonGearReverse = ini.Get("DirectInput.Shifter", "ButtonGearReverse", DIShifterButtonGearReverse);

		// [DirectInput.Aux] — button box / stalk device
		DIAuxDeviceGuid = ini.Get("DirectInput.Aux", "DeviceGuid", DIAuxDeviceGuid);
		DIAuxEnabled = !DIAuxDeviceGuid.empty();
		DIAuxButtonA = ini.Get("DirectInput.Aux", "ButtonA", DIAuxButtonA);
		DIAuxButtonB = ini.Get("DirectInput.Aux", "ButtonB", DIAuxButtonB);
		DIAuxButtonX = ini.Get("DirectInput.Aux", "ButtonX", DIAuxButtonX);
		DIAuxButtonY = ini.Get("DirectInput.Aux", "ButtonY", DIAuxButtonY);
		DIAuxButtonStart = ini.Get("DirectInput.Aux", "ButtonStart", DIAuxButtonStart);
		DIAuxButtonBack = ini.Get("DirectInput.Aux", "ButtonBack", DIAuxButtonBack);
		DIAuxButtonGearUp = ini.Get("DirectInput.Aux", "ButtonGearUp", DIAuxButtonGearUp);
		DIAuxButtonGearDown = ini.Get("DirectInput.Aux", "ButtonGearDown", DIAuxButtonGearDown);
		DIAuxButtonChangeView = ini.Get("DirectInput.Aux", "ButtonChangeView", DIAuxButtonChangeView);
		DIAuxButtonSelUp = ini.Get("DirectInput.Aux", "ButtonSelUp", DIAuxButtonSelUp);
		DIAuxButtonSelDown = ini.Get("DirectInput.Aux", "ButtonSelDown", DIAuxButtonSelDown);
		DIAuxButtonSelLeft = ini.Get("DirectInput.Aux", "ButtonSelLeft", DIAuxButtonSelLeft);
		DIAuxButtonSelRight = ini.Get("DirectInput.Aux", "ButtonSelRight", DIAuxButtonSelRight);

		DirectInputFFB = ini.Get("FFB", "DirectInputFFB", DirectInputFFB);
		FFBDevice = ini.Get("FFB", "FFBDevice", FFBDevice);
		const bool legacySelection = WheelSettingsPolicy::RequiresLegacyFfbSelection(ini, FFBDevice);
		if (legacySelection) FFBDeviceGuid = "legacy-index";
		FFBDeviceGuid = ini.Get("FFB", "FFBDeviceGuid", FFBDeviceGuid);
		FFBDeviceName = ini.Get("FFB", "FFBDeviceName", FFBDeviceName);
		FFBProfile = ini.Get("FFB", "FFBProfile", FFBProfile);
		FFBGlobalStrength = ini.Get("FFB", "FFBGlobalStrength", FFBGlobalStrength);
		FFBGlobalStrength = std::clamp(FFBGlobalStrength, 0.0f, 2.0f);
		FFBSpringStrength = ini.Get("FFB", "FFBSpringStrength", FFBSpringStrength);
		FFBSpringStrength = std::clamp(FFBSpringStrength, 0.0f, 2.0f);
		FFBDamperStrength = ini.Get("FFB", "FFBDamperStrength", FFBDamperStrength);
		FFBDamperStrength = std::clamp(FFBDamperStrength, 0.0f, 2.0f);
		FFBSteeringWeight = ini.Get("FFB", "FFBSteeringWeight", FFBSteeringWeight);
		FFBSteeringWeight = std::clamp(FFBSteeringWeight, 0.0f, 2.0f);
		FFBWallImpact = ini.Get("FFB", "FFBWallImpact", FFBWallImpact);
		FFBWallImpact = std::clamp(FFBWallImpact, 0.0f, 2.0f);
		FFBRumbleStrip = ini.Get("FFB", "FFBRumbleStrip", FFBRumbleStrip);
		FFBRumbleStrip = std::clamp(FFBRumbleStrip, 0.0f, 2.0f);
		FFBGearShift = ini.Get("FFB", "FFBGearShift", FFBGearShift);
		FFBGearShift = std::clamp(FFBGearShift, 0.0f, 2.0f);
		FFBRoadTexture = ini.Get("FFB", "FFBRoadTexture", FFBRoadTexture);
		FFBRoadTexture = std::clamp(FFBRoadTexture, 0.0f, 2.0f);
		FFBTireSlip = ini.Get("FFB", "FFBTireSlip", FFBTireSlip);
		FFBTireSlip = std::clamp(FFBTireSlip, 0.0f, 2.0f);
		FFBGripLoss = ini.Get("FFB", "FFBGripLoss", FFBGripLoss);
		FFBGripLoss = std::clamp(FFBGripLoss, 0.0f, 1.0f);
		FFBWeightTransfer = ini.Get("FFB", "FFBWeightTransfer", FFBWeightTransfer);
		FFBWeightTransfer = std::clamp(FFBWeightTransfer, 0.0f, 2.0f);
		FFBLateralDeadzone = ini.Get("FFB", "FFBLateralDeadzone", FFBLateralDeadzone);
		FFBLateralDeadzone = std::clamp(FFBLateralDeadzone, 0.0f, 10.0f);
		FFBEngineIdle = ini.Get("FFB", "FFBEngineIdle", FFBEngineIdle);
		FFBEngineIdle = std::clamp(FFBEngineIdle, 0.0f, 1.0f);
		FFBUsePeriodicEffects = ini.Get("FFB", "FFBUsePeriodicEffects", FFBUsePeriodicEffects);
		FFBWheelTorqueNm = ini.Get("FFB", "FFBWheelTorqueNm", FFBWheelTorqueNm);
		FFBWheelTorqueNm = std::clamp(FFBWheelTorqueNm, 0.0f, 100.0f);
		FFBInvertForce = ini.Get("FFB", "FFBInvertForce", FFBInvertForce);
		FFBDiagnosticLog = ini.Get("FFB", "FFBDiagnosticLog", FFBDiagnosticLog);

		TelemetryEnabled = ini.Get("Telemetry", "Enable", TelemetryEnabled);
		TelemetrySharedMemName = ini.Get("Telemetry", "SharedMemName", TelemetrySharedMemName);


		ShowFrameRate = ini.Get("Overlay", "ShowFrameRate", ShowFrameRate);
		WheelSettingsView = ini.Get("WheelSettings", "View", WheelSettingsView);
		if (WheelSettingsView != "Advanced") WheelSettingsView = "Simple";
		WheelSettingsScale = ini.Get("WheelSettings", "Scale", WheelSettingsScale);
		if (!std::isfinite(WheelSettingsScale)) WheelSettingsScale = 1.0f;
		WheelSettingsScale = std::clamp(WheelSettingsScale, 0.8f, 1.5f);
		WheelSettingsKey = ini.Get("WheelSettings", "SettingsKey", WheelSettingsKey);
		WheelStopKey = ini.Get("WheelSettings", "StopFfbKey", WheelStopKey);
		if (!WheelSettingsPolicy::ValidShortcut(WheelSettingsKey) || !WheelSettingsPolicy::ValidShortcut(WheelStopKey) || WheelSettingsKey == WheelStopKey)
		{
			WheelSettingsKey = VK_F6; WheelStopKey = VK_F8;
			spdlog::warn("Wheel settings: invalid/conflicting shortcuts; using F6 and F8 for this session");
		}


		// INIReader doesn't preserve the order of the keys/values in a section
		// Will need to try opening INI ourselves to grab cd tracks...

		return true;
	}
}
void Plugin_Init()
{
	// setup our log & INI paths
	Module::init();

	// spdlog setup
	{
		std::vector<spdlog::sink_ptr> sinks;
		sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>(true)); // Print logs to debug output
		try
		{
			sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(Module::LogPath.string(), true));
		}
		catch (const std::exception&)
		{
			// spdlog failed to open log file for writing (happens in some WinStore apps)
			// let's just try to continue instead of crashing
		}

		auto combined_logger = std::make_shared<spdlog::logger>("", begin(sinks), end(sinks));
#ifdef _DEBUG
		combined_logger->set_level(spdlog::level::debug);
#else
		combined_logger->set_level(spdlog::level::debug);
#endif
		spdlog::set_default_logger(combined_logger);
		spdlog::flush_on(spdlog::level::debug);

	}

	spdlog::info("OutRun2006Tweaks v" MODULE_VERSION_STR " - github.com/emoose/OutRun2006Tweaks");
    if (OutRunSignalMute::BlocksOutput()) {
        spdlog::warn("SignalCapture: PROCESS MUTE {}; WheelFfb load/init/sends, controller rumble and motion telemetry delivery blocked until exit",
            OutRunSignalMute::StartupMode()==OutRunSignalMute::Mode::LegacyConstant ? "legacy" : "invalid-mode-refused");
    }
	Module::to_log();

	// STD-033: a new rig-profile revision (OutRun2006Tweaks.profile.ini) becomes the remap's user INI keys before
	// either INI is read.
	{
		bool profileApplied = false;
		for (const auto& line : ProfileControls::ApplyAtStartup(Module::IniPath.parent_path(), profileApplied))
			spdlog::info("{}", line);
		// Dev only: arms just for a signal-muted process with inject.on and the profile applied (remap_inject.hpp).
		for (const auto& line : RemapInject::Init(Module::IniPath.parent_path()))
			spdlog::info("{}", line);
	}

	if (!Settings::read(Module::IniPath))
		spdlog::error("Settings::read - Launching game with default OR2006Tweaks INI settings!");

	// Anything past this point counts as an override, and is what gets written
	// back out to the user INI.
	Settings::mark_base_values();

	if (std::filesystem::exists(Module::UserIniPath))
		Settings::read(Module::UserIniPath);

	int argc;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv)
	{
		bool changed = Settings::read_cmd_line(argc, argv);
		LocalFree(argv);
		if (changed)
		{
			// Prevent command-line overrides from being written back to ini
			spdlog::warn("Command-line overrides are in use, disabling INI writes.");
			Settings::DisableSettingsWrite = true;
		}
	}

	Settings::to_log();

	Game::StartupTime = std::chrono::system_clock::now();

	// Create save folder if it doesn't exist, otherwise game will have issues writing savegame...
	auto saveFolder = Module::ExePath.parent_path() / "SaveGame";
	if (!std::filesystem::exists(saveFolder))
	{
		spdlog::warn("Plugin_Init: SaveGame folder doesn't exist, trying to create it...");
		try
		{
			std::filesystem::create_directory(saveFolder);
			spdlog::info("Plugin_Init: SaveGame folder created");
		}
		catch (const std::exception&)
		{
			spdlog::error("Plugin_Init: Failed to create SaveGame folder (may require permissions?) - game might have issues writing savegame!");
		}
	}

	InitExceptionHandler();

	HookManager::ApplyHooks();

	// Hooks declare which settings they read as they apply, so the snapshot and
	// the no-consumer check both have to wait until they've all run.
	Settings::mark_startup_values();
}

#include "Proxy.hpp"

BOOL APIENTRY DllMain(HMODULE hModule, int ul_reason_for_call, LPVOID lpReserved)
{

	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hModule);
		Module::DllHandle = hModule;
		proxy::on_attach(Module::DllHandle);

		static std::once_flag flag;
		std::call_once(flag, Plugin_Init);
	}
	// Detach stays quiet: no waits, logging, foreign ABI, or FreeLibrary.
	// Cleanup runs before game cleanup outside loader lock. Modules are pinned;
	// explicit hot-unload is unsupported.

	return TRUE;
}
