#pragma once

#include <filesystem>
#include "axis_calibration.hpp"

#include "game.hpp"
#include "settings.hpp"

extern void DInput_RegisterNewDevices(); // hooks_input.cpp
extern void SetVibration(int userId, float leftMotor, float rightMotor); // hooks_forcefeedback.cpp
extern void AudioHooks_Update(int numUpdates); // hooks_audio.cpp
extern void CDSwitcher_ReadIni(const std::filesystem::path& iniPath);

namespace Module
{
	// Info about our module
	inline HMODULE DllHandle{ 0 };
	inline std::filesystem::path DllPath{};

	// Info about the module we've been loaded into
	inline HMODULE ExeHandle{ 0 };
	inline std::filesystem::path ExePath{};

	inline std::filesystem::path LogPath{};
	inline std::filesystem::path IniPath{};
	inline std::filesystem::path UserIniPath{};
	inline std::filesystem::path LodIniPath{};
	inline std::filesystem::path OverlayIniPath{};
	inline std::filesystem::path BindingsIniPath{};

	template <typename T>
	inline T* exe_ptr(uintptr_t offset) { if (ExeHandle) return (T*)(((uintptr_t)ExeHandle) + offset); else return nullptr; }
	inline uint8_t* exe_ptr(uintptr_t offset) { if (ExeHandle) return (uint8_t*)(((uintptr_t)ExeHandle) + offset); else return nullptr; }

	template <typename T>
	inline T fn_ptr(uintptr_t offset) { if (ExeHandle) return (T)(((uintptr_t)ExeHandle) + offset); else return nullptr; }

	// Deduce the type by providing it as an argument, no need for ugly decltype stuff
	template <typename T>
	inline T fn_ptr(uintptr_t offset, T& var)
	{
		if (ExeHandle)
			return reinterpret_cast<T>(((uintptr_t)ExeHandle) + offset);
		else
			return nullptr;
	}

	void init();
}

namespace Game
{
	enum class GamepadType
	{
		None,
		PC,
		Xbox,
		PS,
		Switch
	};

	inline static const char* PadTypes[] =
	{
		"None",
		"PC",
		"Xbox",
		"PlayStation",
		"Switch"
	};

	inline std::chrono::system_clock::time_point StartupTime;
	inline float DeltaTime = (1.f / 60.f);

	inline GamepadType CurrentPadType = GamepadType::PC;
	inline GamepadType ForcedPadType = GamepadType::None;
};

// Each setting is defined in the file that implements the feature it belongs
// to, next to the hook that reads it. Only the ones read from more than one
// file are declared here.
namespace Settings
{
	extern Setting<int> FramerateLimit;                    // hooks_framerate.cpp
	extern Setting<int> FramerateFastLoad;                 // hooks_framerate.cpp
	extern Setting<bool> FramerateInterpolation;           // hooks_framerate.cpp

	extern Setting<float> FramerateInterpolationDebugAlpha; // interpolation.cpp
	extern Setting<bool> FramerateInterpolationDebugLog;    // interpolation.cpp

	// [Triple] Screens (STD-022): 0 Off, 1 Surround (default: the window as before, which already goes wide on a
	// Surround desktop), 2 Separate monitors (one borderless window over three equal side-by-side monitors; planar
	// like Surround, not angle-correct). Separate monitors needs AutoDetectResolution (no outrun2006.ini), or an
	// outrun2006.ini resolution equal to the span.
	inline int TripleScreens = 1;
	// Runtime, not read from the INI: the span chosen at start-up, used for the borderless window's position.
	inline bool TripleSpanActive = false;
	inline long TripleSpanLeft = 0, TripleSpanTop = 0, TripleSpanWidth = 0, TripleSpanHeight = 0;

	extern Setting<bool> AllowHorn;
	extern Setting<bool> AllowWAV;
	extern Setting<int> UIScalingMode;                     // hooks_uiscaling.cpp
	extern Setting<int> UILetterboxing;                    // hooks_uiscaling.cpp
	extern Setting<int> SkyGlowFactor;                     // hooks_graphics.cpp
	extern Setting<bool> SkyGlowTwoStep;                   // hooks_graphics.cpp
	extern Setting<bool> UseHiDefCharacters;               // hooks_graphics.cpp
	extern Setting<int> DrawDistanceIncrease;              // hooks_drawdistance.cpp
	extern Setting<int> DrawDistanceBehind;                // hooks_drawdistance.cpp

	extern Setting<bool> AllowFLAC;                        // hooks_flac.cpp

	extern Setting<bool> UseNewInput;                      // input_manager.cpp
	extern Setting<bool> BypassGameSensitivity;            // input_manager.cpp
	extern Setting<float> SteeringDeadZone;                // hooks_input.cpp
	extern Setting<bool> ControllerHotPlug;                // hooks_input.cpp
	extern Setting<int> ImpulseVibrationMode;              // hooks_input.cpp
	extern Setting<float> ImpulseVibrationLeftMultiplier;  // hooks_input.cpp
	extern Setting<float> ImpulseVibrationRightMultiplier; // hooks_input.cpp
	extern Setting<int> VibrationMode;                     // hooks_forcefeedback.cpp
	extern Setting<int> VibrationStrength;                 // hooks_forcefeedback.cpp
	extern Setting<int> VibrationControllerId;             // hooks_forcefeedback.cpp
	extern Setting<bool> WindowedBorderless;               // hooks_graphics.cpp

	extern Setting<bool> RestoreJPClarissa;                // hooks_misc.cpp
	extern Setting<std::string> DemonwareServerOverride;   // hooks_misc.cpp
	extern Setting<bool> SingleCoreAffinity;               // hooks_misc.cpp
	extern Setting<bool> FixFullPedalChecks;               // hooks_bugfixes.cpp
	extern Setting<bool> OverlayEnabled;                   // overlay/hooks_overlay.cpp

	extern Setting<int> InputBackend;					   // input_manager.cpp

	// Track list for the CD switcher. Read by its own parser rather than as a
	// setting, since INIReader doesn't preserve the order of a section's keys.
	inline std::vector<std::pair<std::string, std::string>> CDTracks;

	extern Setting<int> AnisotropicFiltering;
	extern Setting<int> ReflectionResolution;
	extern Setting<bool> TransparencySupersampling;
	extern Setting<bool> ScreenEdgeCullFix;
	extern Setting<bool> DisableVehicleLODs;
	extern Setting<bool> DisableStageCulling;
	extern Setting<bool> FixZBufferPrecision;
	extern Setting<float> CarBaseShadowOpacity;

	extern Setting<std::string> TextureBaseFolder;
	extern Setting<bool> SceneTextureReplacement;
	extern Setting<bool> SceneTextureExtract;
	extern Setting<bool> UITextureReplacement;
	extern Setting<bool> UITextureExtract;
	extern Setting<bool> EnableTextureCache;
	extern Setting<bool> UseNewTextureAllocator;

	extern Setting<bool> DefaultManualTransmission;

	// DirectInput axis/button remapping (mutually exclusive with UseNewInput)
	inline bool UseDirectInputRemap = false;
	inline std::string DIRemapDeviceGuid = "auto";
	inline int DIRemapSteeringAxis = 0;
	inline bool DIRemapSteeringInvert = false;
	inline float DIRemapSteeringSensitivity = 1.0f;
	inline int DIRemapAccelAxis = 1;
	inline bool DIRemapAccelInvert = false;
	inline int DIRemapBrakeAxis = 2;
	inline bool DIRemapBrakeInvert = false;
	inline WheelInput::Calibration DIRemapCalibration[3];
	inline float DIRemapAccelDeadzone = 0;
	inline float DIRemapBrakeDeadzone = 0;
	// Empty GUID retains the legacy primary-wheel source. Explicit pedal
	// identities never fall back to another attached device.
	inline std::string DIRemapAccelDeviceGuid, DIRemapAccelDeviceName;
	inline std::string DIRemapBrakeDeviceGuid, DIRemapBrakeDeviceName;
	inline int DIRemapButtonA = 0;
	inline int DIRemapButtonB = 1;
	inline int DIRemapButtonX = 2;
	inline int DIRemapButtonY = 3;
	inline int DIRemapButtonStart = 7;
	inline int DIRemapButtonBack = 6;
	inline int DIRemapButtonGearUp = 4;
	inline int DIRemapButtonGearDown = 5;
	inline int DIRemapButtonChangeView = 8;
	inline int DIRemapButtonSelUp = -1;
	inline int DIRemapButtonSelDown = -1;
	inline int DIRemapButtonSelLeft = -1;
	inline int DIRemapButtonSelRight = -1;

	// [DirectInput.Shifter] — separate shifter device
	inline bool DIShifterEnabled = false;
	inline std::string DIShifterDeviceGuid = "";
	inline std::string DIShifterGearMode = "sequential"; // "sequential" or "hpattern"
	inline int DIShifterButtonGearUp = 4;
	inline int DIShifterButtonGearDown = 5;
	inline int DIShifterButtonGear1 = -1;
	inline int DIShifterButtonGear2 = -1;
	inline int DIShifterButtonGear3 = -1;
	inline int DIShifterButtonGear4 = -1;
	inline int DIShifterButtonGear5 = -1;
	inline int DIShifterButtonGear6 = -1;
	inline int DIShifterButtonGearReverse = -1;

	// [DirectInput.Aux] — button box / stalk device
	inline bool DIAuxEnabled = false;
	inline std::string DIAuxDeviceGuid = "";
	inline int DIAuxButtonA = -1;
	inline int DIAuxButtonB = -1;
	inline int DIAuxButtonX = -1;
	inline int DIAuxButtonY = -1;
	inline int DIAuxButtonStart = -1;
	inline int DIAuxButtonBack = -1;
	inline int DIAuxButtonGearUp = -1;
	inline int DIAuxButtonGearDown = -1;
	inline int DIAuxButtonChangeView = -1;
	inline int DIAuxButtonSelUp = -1;
	inline int DIAuxButtonSelDown = -1;
	inline int DIAuxButtonSelLeft = -1;
	inline int DIAuxButtonSelRight = -1;

	inline bool DirectInputFFB = false;
	inline int FFBDevice = -1;
	inline std::string FFBDeviceGuid = "steering";
	inline std::string FFBDeviceName;
	inline float FFBGlobalStrength = 1.0f;
	inline float FFBSpringStrength = 0.45f;   // virtual centering spring on steering position
	inline float FFBDamperStrength = 0.10f;   // virtual damper on steering derivative
	inline float FFBSteeringWeight = 0.55f;   // lateral road load (secondary term)
	inline float FFBWallImpact = 1.0f;
	inline float FFBRumbleStrip = 0.35f;
	inline float FFBGearShift = 0.3f;
	inline float FFBRoadTexture = 0.6f;       // periodic: surface roughness LUT gain
	inline float FFBTireSlip = 0.35f;         // periodic: drift chatter
	inline float FFBGripLoss = 0.6f;          // drift lightening 0-1 (wheel goes light in a slide)
	inline float FFBWeightTransfer = 0.8f;    // brake/throttle load modulation
	inline float FFBLateralDeadzone = 1.5f;   // lateral slide noise-floor clip (was hardcoded 5.0)
	inline float FFBEngineIdle = 0.08f;       // idle/launch rumble (replaces at-speed engine vib)
	inline bool FFBUsePeriodicEffects = true; // hardware Sine effects; false = CF-synthesis fallback
	inline float FFBWheelTorqueNm = 0.0f;
	inline bool FFBInvertForce = false;
	inline bool FFBDiagnosticLog = false;
	// Which force model and tuning to use. "legacy" is this fork's own model;
	// anything else names a profile in force-profiles.ini beside dinput8.dll
	// (e.g. "arcade-outrun@1"), read at runtime so a tune can be swapped
	// without rebuilding. See docs/FORCE-PROFILES.md.
	inline std::string FFBProfile = "legacy";

	// Telemetry shared memory (for SimHub / bass shakers). On by default (STD-016); [Telemetry] Enable = false turns it off.
	inline bool TelemetryEnabled = true;
	inline std::string TelemetrySharedMemName = "OutRun2006Telemetry";

	extern Setting<int> EnableHollyCourse2;
	extern Setting<bool> SkipIntroLogos;
	extern Setting<bool> DisableCountdownTimer;
	extern Setting<bool> EnableLevelSelect;
	extern Setting<bool> ShowOutRunMilesOnMenu;
	extern Setting<bool> AllowCharacterSelection;
	extern Setting<bool> RandomHighwayAnimSets;
	extern Setting<bool> ProtectLoginData;

	inline bool ShowFrameRate = false;
	inline std::string WheelSettingsView = "Simple";
	inline float WheelSettingsScale = 1.0f;
	inline int WheelSettingsKey = 0x75; // F6, Win32 virtual key
	inline int WheelStopKey = 0x77; // F8

	extern Setting<bool> FixPegasusClopping;
	extern Setting<bool> FixRightSideBunkiAnimations;
	extern Setting<bool> FixC2CRankings;
	extern Setting<bool> PreventDESTSaveCorruption;
	extern Setting<bool> FixLensFlarePath;
	extern Setting<bool> FixIncorrectShading;
	extern Setting<bool> FixParticleRendering;
	extern Setting<bool> HideOnlineSigninText;
}

namespace Util
{
	std::string HttpGetRequest(const std::string& host, const std::wstring& path, int portNum = 80); // network.cpp

	inline uint32_t GetModuleTimestamp(HMODULE moduleHandle)
	{
		if (!moduleHandle)
			return 0;

		uint8_t* moduleData = (uint8_t*)moduleHandle;
		const IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)moduleData;
		const IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)(moduleData + dosHeader->e_lfanew);
		return ntHeaders->FileHeader.TimeDateStamp;
	}

	// Fetches path of module as std::filesystem::path, resizing buffer automatically if path length above MAX_PATH
	inline std::filesystem::path GetModuleFilePath(HMODULE moduleHandle)
	{
		std::vector<wchar_t> buffer(MAX_PATH, L'\0');

		DWORD result = GetModuleFileNameW(moduleHandle, buffer.data(), buffer.size());
		while (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
		{
			// Buffer was too small, resize and try again
			buffer.resize(buffer.size() * 2, L'\0');
			result = GetModuleFileNameW(moduleHandle, buffer.data(), buffer.size());
		}

		return std::wstring(buffer.data(), result);
	}

	inline uint32_t BitCount(uint32_t n)
	{
		n = n - ((n >> 1) & 0x55555555);          // put count of each 2 bits into those 2 bits
		n = (n & 0x33333333) + ((n >> 2) & 0x33333333); // put count of each 4 bits into those 4 bits
		n = (n + (n >> 4)) & 0x0F0F0F0F;          // put count of each 8 bits into those 8 bits
		n = n + (n >> 8);                         // put count of each 16 bits into their lowest 8 bits
		n = n + (n >> 16);                        // put count of each 32 bits into their lowest 8 bits
		return n & 0x0000003F;                    // return the count
	}

	// Function to trim spaces from the start of a string
	inline std::string ltrim(const std::string& s)
	{
		auto start = std::find_if_not(s.begin(), s.end(), [](unsigned char ch)
		{
			return std::isspace(ch);
		});
		return std::string(start, s.end());
	}

	// Function to trim spaces from the end of a string
	inline std::string rtrim(const std::string& s)
	{
		auto end = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char ch)
		{
			return std::isspace(ch);
		});
		return std::string(s.begin(), end.base());
	}

	// Function to trim spaces from both ends of a string
	inline std::string trim(const std::string& s)
	{
		return ltrim(rtrim(s));
	}
}

inline void WaitForDebugger()
{
#ifdef _DEBUG
	while (!IsDebuggerPresent())
	{
	}
#endif
}
