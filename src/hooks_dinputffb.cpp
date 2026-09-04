// DirectInput Force Feedback via DirectInput COM API
// Provides steering wheel force feedback using EVWORK_CAR telemetry data.
// Effects: steering weight (constant force), collision impact,
//          rumble strip, gear shift, road texture, tire slip.
// Uses IDirectInputEffect::SetParameters with DIEP_START for reliable
// real-time updates on all wheel drivers (SDL3 Haptic doesn't work with Moza/DD wheels).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <dinput.h>
#include <commctrl.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "comctl32.lib")
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <string>

#include "hook_mgr.hpp"
#include "plugin.hpp"
#include "game_addrs.hpp"
#include "game.hpp"
#include "telemetry.hpp"
#include "wheelffb.h"      // dbce-wheel-mod-toolkit C ABI (lib/toolkit/include)
#include "force_profile.h" // shared force model + versioned tuning profiles

// External vibration data from hooks_forcefeedback.cpp
extern float VibrationLeftMotor;
extern float VibrationRightMotor;

// Surface-type -> roughness coefficient LUT, decompiled from the game's own
// Xbox vibration code (defined in hooks_forcefeedback.cpp). Returns 0.0 for
// asphalt up to 0.9 for rough surfaces; sets *a3 |= 1 on water surfaces.
extern double __cdecl sub_1149C0(unsigned int surfaceMask, int loadColiType, DWORD* waterFlag);

// Forza "Data Out" telemetry, emitted to localhost:8000 for the Moza Pit
// House display and SimHub. The 311-byte FM7 "Dash" layout and its encoder
// now come from dbce-wheel-mod-toolkit rather than a local struct, so the
// sizes every receiver validates against (232 sled / 311 FM7 / 324 Horizon)
// are pinned in one place with a conformance test. The struct fields are the
// same ones, lower-cased: IsRaceOn -> isRaceOn, Speed -> speed and so on.
#include "forza_packet.h"

// Telemetry: shared memory (SimHub) + Forza UDP (Moza Pit House display)
// Forward declarations from hooks_inputremap.cpp.
// Declared above Telemetry (not just above FFB) because the telemetry packet
// now reads live pedal positions from the remap layer.
namespace DInputRemap
{
	IDirectInputDevice8A* GetPrimaryDevice();
	bool IsPrimaryInitialized();
	bool GetPrimaryDeviceGuid(GUID* out);
	int GetTelemetryAccel();   // 0-255, or -1 if no primary device
	int GetTelemetryBrake();   // 0-255, or -1 if no primary device
}

namespace Telemetry
{
	// Shared memory for SimHub plugin
	static HANDLE hMapFile = nullptr;
	static OutRun2006TelemetryData* pData = nullptr;
	static bool initialized = false;
	static uint32_t packetId = 0;

	// Forza UDP for Moza Pit House wheel display
	static SOCKET udpSocket = INVALID_SOCKET;
	static sockaddr_in udpAddr = {};
	static bool udpInitialized = false;
	static const int FORZA_UDP_PORT = 8000;

	// Approximate gear ratios for RPM synthesis (OutRun doesn't expose RPM)
	// These create a believable RPM range on the wheel display
	static const float GearRatios[] = { 0.0f, 3.5f, 2.1f, 1.4f, 1.0f, 0.8f, 0.65f };
	static const float MaxRPM = 8500.0f;
	static const float IdleRPM = 900.0f;
	static const float MaxSpeedMps = 90.0f; // ~324 km/h, OutRun top speed approx

	static bool Init()
	{
		if (!Settings::TelemetryEnabled)
			return false;

		// Init shared memory
		const std::string& name = Settings::TelemetrySharedMemName;
		hMapFile = CreateFileMappingA(
			INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
			sizeof(OutRun2006TelemetryData), name.c_str());

		if (!hMapFile)
		{
			spdlog::error("Telemetry: CreateFileMapping failed (err={})", GetLastError());
			return false;
		}

		pData = static_cast<OutRun2006TelemetryData*>(
			MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(OutRun2006TelemetryData)));

		if (!pData)
		{
			spdlog::error("Telemetry: MapViewOfFile failed (err={})", GetLastError());
			CloseHandle(hMapFile);
			hMapFile = nullptr;
			return false;
		}

		memset(pData, 0, sizeof(OutRun2006TelemetryData));
		pData->version = TELEMETRY_VERSION;
		initialized = true;
		spdlog::info("Telemetry: Shared memory '{}' created ({} bytes)", name, sizeof(OutRun2006TelemetryData));

		// Init Forza UDP socket for Moza Pit House
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0)
		{
			udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (udpSocket != INVALID_SOCKET)
			{
				udpAddr.sin_family = AF_INET;
				udpAddr.sin_port = htons(FORZA_UDP_PORT);
				udpAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
				udpInitialized = true;
				spdlog::info("Telemetry: Forza UDP emitter ready (127.0.0.1:{})", FORZA_UDP_PORT);
			}
			else
			{
				spdlog::warn("Telemetry: Failed to create UDP socket (err={})", WSAGetLastError());
			}
		}

		return true;
	}

	static void Write(EVWORK_CAR* car, bool inGameplay)
	{
		// Write to shared memory (SimHub)
		if (pData)
		{
			pData->packetId = ++packetId;
			pData->speed = car->field_1C4;
			pData->steeringAngle = car->field_1D0;
			pData->lateralG1 = car->field_264;
			pData->lateralG2 = car->field_268;
			pData->impactForce = car->field_178;
			pData->gear = car->cur_gear_208;
			pData->prevGear = car->dword1D8;
			pData->stateFlags = car->field_8;
			pData->carFlags = car->flags_4;
			pData->surfaceType[0] = car->water_flag_24C[0];
			pData->surfaceType[1] = car->water_flag_24C[1];
			pData->surfaceType[2] = car->water_flag_24C[2];
			pData->surfaceType[3] = car->water_flag_24C[3];
			pData->vibrationLeft = VibrationLeftMotor;
			pData->vibrationRight = VibrationRightMotor;
			pData->gameMode = Game::current_mode ? *Game::current_mode : 0;
			pData->isInGameplay = inGameplay ? 1 : 0;
		}

		// Send Forza UDP (Moza Pit House wheel display)
		if (udpInitialized && udpSocket != INVALID_SOCKET)
		{
			dbce::forza::Sled sled = {};
			dbce::forza::Dash dash = {};

			sled.isRaceOn = inGameplay ? 1 : 0;
			sled.timestampMs = GetTickCount();

			// Speed: convert normalized (0-2+) to m/s
			float speedMps = car->field_1C4 * MaxSpeedMps;
			dash.speed = speedMps;

			// Gear
			uint32_t gear = car->cur_gear_208;
			dash.gear = (uint8_t)std::clamp(gear, 0u, 10u);

			// Synthesize RPM from speed and gear
			// OutRun doesn't expose RPM, so we calculate a plausible value
			float gearRatio = (gear > 0 && gear < 7) ? GearRatios[gear] : 1.0f;
			float speedNorm = std::clamp(car->field_1C4 / 2.0f, 0.0f, 1.0f);
			float rpm = IdleRPM + speedNorm * gearRatio * (MaxRPM - IdleRPM);
			rpm = std::clamp(rpm, IdleRPM, MaxRPM);

			sled.currentEngineRpm = rpm;
			sled.engineMaxRpm = MaxRPM;
			sled.engineIdleRpm = IdleRPM;

			// Steering angle mapped to Forza's -127..127 range
			dash.steer = dbce::forza::steer11(car->field_1D0);

			// Lateral acceleration (for display)
			sled.accX = car->field_264 + car->field_268;

			// Throttle and brake, straight from the wheel's own pedals.
			//
			// OutRun's car struct never exposes pedal position -- the game only
			// keeps the resulting speed -- so these come from the input remap
			// layer, which is already reading and normalising both axes every
			// frame for the game itself. That makes them true pedal travel
			// rather than something inferred from acceleration.
			//
			// SimHub surfaces Forza's Accel/Brake bytes as GameData.Throttle
			// and GameData.Brake, which is what drives brake lights and
			// pedal-based ShakeIt effects. -1 means no wheel is bound, in which
			// case the field is left at zero rather than asserting a value.
			int accelPedal = DInputRemap::GetTelemetryAccel();
			int brakePedal = DInputRemap::GetTelemetryBrake();
			if (accelPedal >= 0)
				dash.accel = (uint8_t)accelPedal;
			if (brakePedal >= 0)
				dash.brake = (uint8_t)brakePedal;

			// Surface rumble (for display indicators)
			bool offRoad = car->water_flag_24C[0] > 1 || car->water_flag_24C[1] > 1 ||
			               car->water_flag_24C[2] > 1 || car->water_flag_24C[3] > 1;
			if (offRoad)
			{
				for (int i = 0; i < 4; i++)
					sled.surfaceRumble[i] = 1.0f;
			}

			// FM7 "Dash": 232-byte sled + the 79-byte dash block. build() zeroes
			// the buffer, so a field this game has no source for stays 0.
			uint8_t frame[dbce::forza::FORZA_FM7_DASH_311];
			int n = dbce::forza::build(dbce::forza::FORZA_FM7_DASH_311, sled, dash,
			                           frame, sizeof(frame));
			sendto(udpSocket, (const char*)frame, n, 0,
				(sockaddr*)&udpAddr, sizeof(udpAddr));
		}
	}

	static void Shutdown()
	{
		if (pData)
		{
			UnmapViewOfFile(pData);
			pData = nullptr;
		}
		if (hMapFile)
		{
			CloseHandle(hMapFile);
			hMapFile = nullptr;
		}
		if (udpSocket != INVALID_SOCKET)
		{
			closesocket(udpSocket);
			udpSocket = INVALID_SOCKET;
		}
		initialized = false;
		spdlog::info("Telemetry: Shared memory closed");
	}
}

// Forward declaration from Proxy.cpp
extern IDirectInput8A* g_RealDirectInput8;

namespace FFB
{
	// DirectInput FFB state. The device, the effects and their whole lifecycle
	// live in WheelFfb.dll (dbce-wheel-mod-toolkit) now; what stays here is the
	// force model, which is the part that is actually about OutRun.
	static WheelFfbApi ffb = {};
	static bool ffbLoaded = false;
	static bool initialized = false;
	static bool initAttempted = false;

	// Hardware periodic effects (road texture / tire slip). 25-40 Hz content
	// synthesized through 60 Hz constant-force updates loses ~26% to zero-order-
	// hold rolloff, more to wheelbase driver smoothing, and 30-60% to tanh
	// compression when riding on steering load. Hardware periodics render inside
	// the wheelbase at full fidelity regardless of our update rate. Slot ids
	// from the DLL; -1 means the driver offers none and the constant-force
	// synthesis fallback carries the vibration instead.
	static int slotRoadTexture = -1;   // GUID_Sine, surface LUT driven
	static int slotTireSlip = -1;      // GUID_Sine, drift chatter / engine idle
	static bool periodicsActive = false;

	// Panic flag: once set (process exit path), no further DI output is issued
	static volatile bool panicStopped = false;

	// The shared force model, when FFBProfile names one.
	//
	// Settings::FFBProfile = legacy          this file's own model (the default)
	//                      = arcade-outrun@1 the toolkit model, tuned as this game shipped
	//                      = <name>@<n>      any profile in force-profiles.ini
	//
	// The profile file sits beside dinput8.dll and is read at startup, so a tune
	// is swapped by editing a text file and restarting - no rebuild. Put your own
	// in force-profiles.user.ini, which an update never overwrites.
	//
	// What does NOT move into the toolkit is everything above this line: reading
	// the car struct, the game's own surface-roughness LUT, crash detection from
	// the speed window, the drift estimate. Working out what the car is doing is
	// per game; deciding how that should FEEL is not, and that is what the shared
	// model owns.
	static bool useSharedModel = false;
	static dbce::force::Profile sharedProfile;
	static dbce::force::Model*  sharedModel = nullptr;
	static dbce::force::Shaper* sharedShaper = nullptr;
	static uint32_t sharedPrevGear = 0;

	// Watchdog: timestamp of last Update() call for staleness detection
	static volatile DWORD lastUpdateTick = 0;

	// Previous frame state for edge detection
	static uint32_t prevGear = 0;
	static uint32_t prevCollisionFlags = 0;
	static float prevSpeed = 0.0f;

	// Exponential moving average for lateral forces (low-pass filter)
	static float smoothedLateral = 0.0f;

	// Crash detection: accumulate speed loss over a sliding window
	static float speedHistory[8] = {};     // Last 8 frames of speed
	static int speedHistoryIdx = 0;

	// Pre-crash lateral history: the collision response corrupts the lateral
	// signal AT impact, so crash direction is read from ~8 frames earlier
	static float latHistory[16] = {};
	static int latHistoryIdx = 0;

	// Crash impulse state
	static int crashImpulseTimer = 0;      // Frames remaining for crash jolt
	static float crashImpulseForce = 0.0f; // Direction and magnitude of crash jolt

	// Previous constant force level (deadband to prevent micro-oscillations)
	static LONG prevConstantLevel = 0;  // DI range: ±10000
	// Previous structural (post-tanh, pre-vibration) level for slew limiting
	static LONG prevStructLevel = 0;

	// Gear shift timer (frames remaining)
	static int gearShiftTimer = 0;

	// Water splash burst (lake/beach stages at high speed)
	static int splashTimer = 0;
	static float splashAmp = 0.0f;

	// Warmup counter: ramp force scaling from 0 to 1 over first N frames
	static int warmupFrames = 0;
	static const int WARMUP_THRESHOLD = 30; // ~0.5 sec at 60Hz

	// Diagnostic: observed steering-derivative (field_1D4) range since last log
	static float diagSteerRateMin = 0.0f;
	static float diagSteerRateMax = 0.0f;

	// Which EVWORK_CAR field actually carries steering position.
	//
	// field_1D0 has been read as "signed steering position, -1..1" and field_1D4
	// as its derivative, but the 2026-08-11 validation drive disagrees: across
	// 170 samples |1D0| never exceeded 0.011 and sat at or below 0.001 in 92% of
	// them, while 1D4 - tracked as a true min/max, not sampled - reached 0.54. A
	// derivative cannot outrun its own integral by fifty times, so at least one
	// label is wrong, and either way the spring and damper terms of the
	// centre-out model have been running on almost nothing. That is why the
	// lateral term still does all the work the redesign meant to demote.
	//
	// Rather than guess an offset, scan the neighbouring floats: one lap with
	// FFBDiagnosticLog=true prints the real range of each. The steering field is
	// the one that reaches roughly +-1 (or +-full lock in whatever unit), changes
	// sign with the corner, and returns to zero on the straights.
	struct FieldProbe { const char* name; float lo; float hi; };
	static FieldProbe steerProbe[] = {
		{ "1C8", 0.0f, 0.0f },
		{ "1CC", 0.0f, 0.0f },
		{ "1D0", 0.0f, 0.0f },   // read as steering position today
		{ "1D4", 0.0f, 0.0f },   // read as steering rate today
		{ "1DC", 0.0f, 0.0f },
		{ "1E0", 0.0f, 0.0f },
	};

	// ---------- Force output, through the toolkit ----------
	//
	// Everything that used to sit here - open the device exclusively, create the
	// constant force, recreate it and ramp back in when the handle dies, two
	// GUID_Sine periodics with the driver strategy probe, re-acquire after a
	// focus change, zero and release in the right order at exit - is WheelFfb.dll
	// now. This project donated most of those rules to it; keeping a second copy
	// is how this repo and art-of-sim-rally came to fix the same bug on
	// different days.

	// FFBGlobalStrength used to be the DirectInput effect gain (dwGain). The DLL
	// runs its effect at full gain, so the strength is applied to the magnitude
	// instead. Both are the same linear scale, so the feel is unchanged.
	static float StrengthScale()
	{
		return std::clamp(Settings::FFBGlobalStrength, 0.0f, 1.0f);
	}

	static void SetConstantForce(LONG magnitude)
	{
		if (!ffbLoaded || panicStopped)
			return;
		magnitude = std::clamp(magnitude, (LONG)-10000, (LONG)10000);
		LONG scaled = (LONG)std::clamp((float)magnitude * StrengthScale(), -10000.0f, 10000.0f);
		// Y is always zero: OutRun steers on one axis. The DLL decides how to
		// encode that for the wheel in front of it - three wheels disagreed
		// about direction versus magnitude sign, and it carries the encoding
		// all three accept.
		ffb.SetDeviceForcesXY(scaled, 0);
		prevConstantLevel = magnitude;
	}

	// Envelope update for a hardware periodic effect. Caller rate-limits to
	// ~15 Hz; the DLL additionally drops an update whose magnitude moved less
	// than 5% and period less than 10%, and recreates a slot whose handle died.
	static void UpdatePeriodic(int slot, float magnitude01, float freqHz)
	{
		if (slot < 0 || !ffbLoaded || panicStopped)
			return;
		float mag = std::clamp(magnitude01, 0.0f, 1.0f) * StrengthScale();
		ffb.UpdatePeriodicEffect(slot, (int)(mag * 10000.0f),
			(int)(std::clamp(freqHz, 1.0f, 100.0f) * 1000.0f));
	}

	// ---------- Exit-path guards (fixes exit stuck-force) ----------

	// Emergency zero-torque for process exit. The ordering that matters - zero,
	// stop, STOPALL, actuators off, RESET, unacquire, and only then restore
	// autocentre, which may only be written on an unacquired device - is inside
	// the DLL. What still matters here is WHEN: while the game window exists.
	// By DLL_PROCESS_DETACH the OS has force-unacquired the exclusive device and
	// none of it reaches the wheel, which is exactly how the stuck-force-after-
	// Alt+F4 bug happened.
	void PanicStop()
	{
		if (panicStopped)
			return;
		panicStopped = true;   // stop Update()/watchdog issuing anything further

		if (!ffbLoaded)
			return;

		spdlog::info("FFB: PanicStop -- zeroing forces before window teardown");
		ffb.PanicStop();
	}

	// Zero all force output without tearing anything down (Alt-Tab, menus, watchdog)
	void ZeroAllForces()
	{
		if (!initialized || panicStopped)
			return;
		if (prevConstantLevel != 0)
			SetConstantForce(0);
		prevStructLevel = 0;
		if (periodicsActive)
		{
			UpdatePeriodic(slotRoadTexture, 0.0f, 25.0f);
			UpdatePeriodic(slotTireSlip, 0.0f, 40.0f);
		}
	}

	// WndProc subclass: WM_CLOSE arrives on the game's main thread BEFORE the
	// window is destroyed (Alt+F4 -> WM_SYSCOMMAND/SC_CLOSE -> WM_CLOSE), so the
	// device is still acquirable and the zero-force actually lands on the wheel.
	static const UINT_PTR FFB_SUBCLASS_ID = 0x0FFB;

	static LRESULT CALLBACK ExitGuardSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam,
		LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
	{
		switch (uMsg)
		{
		case WM_CLOSE:
		case WM_DESTROY:
		case WM_QUERYENDSESSION:
			PanicStop();
			break;
		case WM_ACTIVATEAPP:
			if (wParam == FALSE)
				ZeroAllForces(); // don't hold torque while Alt-Tabbed
			break;
		}
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	// Belt-and-braces for exit paths that skip WM_CLOSE entirely
	static SafetyHookInline ExitProcess_hk = {};
	static void WINAPI ExitProcess_Hooked(UINT uExitCode)
	{
		PanicStop();
		ExitProcess_hk.stdcall<void>(uExitCode);
	}

	static void InstallExitGuards()
	{
		HWND hwnd = Game::GameHwnd();
		if (hwnd && SetWindowSubclass(hwnd, ExitGuardSubclassProc, FFB_SUBCLASS_ID, 0))
			spdlog::info("FFB: Exit guard installed (WndProc subclass)");
		else
			spdlog::warn("FFB: SetWindowSubclass failed -- exit cleanup relies on ExitProcess hook only");

		if (auto* exitProc = GetProcAddress(GetModuleHandleA("kernel32.dll"), "ExitProcess"))
		{
			ExitProcess_hk = safetyhook::create_inline(exitProc, ExitProcess_Hooked);
			if (ExitProcess_hk)
				spdlog::info("FFB: Exit guard installed (ExitProcess hook)");
			else
				spdlog::warn("FFB: ExitProcess hook failed");
		}
	}

	// Deferred initialization -- called from Update() on first game tick,
	// because DirectInput needs a valid HWND.
	bool DeferredInit()
	{
		if (initAttempted)
			return initialized;
		initAttempted = true;

		spdlog::info("FFB: Starting deferred initialization (WheelFfb)...");

		// Loaded at runtime from beside this DLL, never imported: a missing
		// WheelFfb.dll has to disable force feedback, not stop the game from
		// starting, and a static import would do the latter.
		if (!WheelFfb_LoadBeside(&ffb, Module::DllHandle, L"WheelFfb.dll"))
		{
			const char* missing = WheelFfb_MissingExport(&ffb);
			spdlog::error("FFB: WheelFfb.dll unusable ({}) -- force feedback disabled. "
				"Copy WheelFfb.dll next to dinput8.dll.", missing ? missing : "?");
			WheelFfb_Unload(&ffb);
			return false;
		}
		ffbLoaded = true;

		// Keep the DLL's own log beside the game, with everything else worth
		// reading after a bad session. Set DBCE_FFB_LOG=0 to silence it.
		{
			char logPath[MAX_PATH] = {};
			if (GetModuleFileNameA(Module::DllHandle, logPath, MAX_PATH))
			{
				char* slash = strrchr(logPath, '\\');
				if (slash)
				{
					strncpy_s(slash + 1, sizeof(logPath) - (slash + 1 - logPath), "OutRun2006Tweaks.ffb.log", _TRUNCATE);
					ffb.SetLogPath(logPath);
				}
			}
		}

		spdlog::info("FFB: WheelFfb.dll loaded (version {})", ffb.GetWheelFfbVersion());

		// The same physical device the remap layer polls, but our OWN exclusive
		// handle - poll-side Acquire() churn must never invalidate our downloaded
		// effects. Selected by instance GUID rather than by name, because a
		// Fanatec base presents two identically named devices and only one of
		// them has the actuator.
		GUID guid = {};
		if (Settings::UseDirectInputRemap && DInputRemap::IsPrimaryInitialized() &&
			DInputRemap::GetPrimaryDeviceGuid(&guid))
		{
			ffb.SetPreferredDeviceGuid(&guid);
			spdlog::info("FFB: requesting the remap layer's primary device by GUID");
		}
		else if (Settings::FFBDevice >= 0)
		{
			ffb.SetPreferredDeviceIndex(Settings::FFBDevice);
			spdlog::info("FFB: requesting device index {}", Settings::FFBDevice);
		}

		int count = ffb.EnumerateDevices();
		for (int i = 0; i < count; i++)
		{
			char name[260] = {};
			ffb.GetDeviceName(i, name, sizeof(name));
			spdlog::info("FFB: force-feedback device [{}]: '{}'", i, name);
		}

		if (!ffb.InitDirectInput((int)(INT_PTR)Game::GameHwnd()))
		{
			spdlog::error("FFB: no usable force-feedback device (last HRESULT 0x{:08X})",
				(unsigned)ffb.GetLastHResult());
			return false;
		}
		ffb.StartEffect();

		// Hardware periodics for road texture and tyre slip. -1 from either means
		// the driver exposes no periodic effects, and the constant-force synthesis
		// fallback below carries the vibration instead.
		if (Settings::FFBUsePeriodicEffects)
		{
			slotRoadTexture = ffb.CreatePeriodicEffect(25);
			slotTireSlip = ffb.CreatePeriodicEffect(40);
			periodicsActive = (slotRoadTexture >= 0 && slotTireSlip >= 0);
			if (periodicsActive)
				spdlog::info("FFB: periodic effects on slots {} and {}", slotRoadTexture, slotTireSlip);
			else
				spdlog::warn("FFB: Periodic effects unavailable -- using constant-force vibration fallback (15 Hz cap, post-compressor injection)");
		}

		// --- force model: this file's own, or a named profile from the toolkit ---
		// A profile that will not load must never cost force feedback; fall back
		// to the built-in model and say so.
		if (!Settings::FFBProfile.empty() && _stricmp(Settings::FFBProfile.c_str(), "legacy") != 0)
		{
			char dir[MAX_PATH] = {};
			GetModuleFileNameA(Module::DllHandle, dir, MAX_PATH);
			if (char* slash = strrchr(dir, '\\')) *slash = 0;

			std::string why;
			if (dbce::force::load_profile_dir(dir, Settings::FFBProfile, sharedProfile, &why))
			{
				sharedModel = new dbce::force::Model(sharedProfile.model);
				sharedShaper = new dbce::force::Shaper(sharedProfile.shaper);
				useSharedModel = true;
				spdlog::info("FFB: force profile '{}' - {}", sharedProfile.id(), sharedProfile.description);
				for (size_t i = 0; i < sharedProfile.unknown_keys.size(); i++)
					spdlog::warn("FFB: profile key not understood by this build: {}", sharedProfile.unknown_keys[i]);
			}
			else
			{
				spdlog::error("FFB: force profile '{}' not loaded ({}) -- using the built-in model",
					Settings::FFBProfile, why);
			}
		}
		else
		{
			spdlog::info("FFB: using the built-in (legacy) force model");
		}

		// Exit guards: zero the wheel while the window still exists. Kept here
		// rather than using the DLL's own InstallExitGuards, because this one
		// also hooks ExitProcess for the paths that never see WM_CLOSE.
		InstallExitGuards();

		initialized = true;
		spdlog::info("FFB: Initialization complete (WheelFfb)");
		return true;
	}


	int updateCounter = 0;

	// Phase accumulators for the constant-force FALLBACK vibration synthesis
	// (only used when hardware periodic effects are unavailable)
	float roadPhase = 0.0f;
	float slipPhase = 0.0f;

	// Check if the game is in a state where FFB should be active
	static bool IsInGameplay()
	{
		if (!Game::current_mode) return false;
		GameState state = (GameState)*Game::current_mode;
		return state == STATE_GAME ||
			state == STATE_START ||
			state == STATE_GOAL ||
			state == STATE_TIMEUP ||
			state == STATE_SMPAUSEMENU;
	}

	// Called from a broad game hook to zero forces if Update() hasn't
	// been called recently (handles menu transitions where GamePlCar_Ctrl stops).
	void CheckWatchdog()
	{
		if (!initialized || !ffbLoaded || panicStopped)
			return;

		DWORD now = GetTickCount();
		DWORD elapsed = now - lastUpdateTick;

		// If Update() hasn't been called for 250ms and forces are non-zero, zero them
		if (elapsed > 250 && lastUpdateTick > 0 && prevConstantLevel != 0)
		{
			ZeroAllForces();
			smoothedLateral = 0.0f;
			crashImpulseTimer = 0;
			spdlog::info("FFB: Watchdog zeroed forces (no Update for {}ms)", elapsed);
		}
	}

	void Update(EVWORK_CAR* car)
	{
		if (!car || panicStopped)
			return;

		// Record timestamp for watchdog staleness detection
		lastUpdateTick = GetTickCount();

		// Telemetry shared memory: init once, write every frame (independent of FFB)
		if (!Telemetry::initialized && Settings::TelemetryEnabled)
			Telemetry::Init();

		bool inGameplay = IsInGameplay();
		Telemetry::Write(car, inGameplay);

		// FFB processing only when DirectInputFFB is enabled
		if (!Settings::DirectInputFFB)
			return;

		// Lazy initialization: deferred to first game tick
		if (!initialized)
		{
			if (!DeferredInit())
				return;
		}

		// Zero forces when not in gameplay (menus, results, etc.)
		// Prevents the wheel from staying stuck at the last force level
		if (!inGameplay)
		{
			ZeroAllForces();
			smoothedLateral = 0.0f;
			crashImpulseTimer = 0;
			warmupFrames = 0;
			return;
		}

		// Warmup: ramp force scaling from 0 to 1 over first N frames.
		// Prevents garbage telemetry on initial frames from causing force spikes.
		// Using a ramp instead of a hard cutoff avoids the problem of game state
		// flickering resetting a counter.
		float warmupScale = 1.0f;
		if (warmupFrames < WARMUP_THRESHOLD)
		{
			warmupFrames++;
			warmupScale = static_cast<float>(warmupFrames) / static_cast<float>(WARMUP_THRESHOLD);
		}

		// The post-recreation ramp-in (fade back over ~250 ms instead of stepping
		// to full torque after an effect had to be recreated) now happens inside
		// the DLL, which is the only layer that knows a recreation occurred.
		const float recreateScale = 1.0f;

		// Update rates:
		// - Constant force: every frame (60 Hz) for responsive steering feel.
		// - Periodic effect envelopes: every 4th frame (~15 Hz) -- hardware
		//   renders the waveform itself, the envelope only shapes it.
		updateCounter++;
		bool updateEnvelopes = (updateCounter % 4 == 0);

		// Read telemetry from EVWORK_CAR
		float speed = car->field_1C4;                      // Normalized speed (0.0 - ~1.0+)
		float speedNorm = std::clamp(speed, 0.0f, 1.0f);
		uint32_t stateFlags = car->field_8;                // State/collision bits
		float lateralForce1 = car->field_264;              // Lateral slide component
		float lateralForce2 = car->field_268;              // Lateral slide component (opposite sign convention)
		uint32_t curGear = car->cur_gear_208;              // Current gear number
		// UNCONFIRMED: 1D0 is read as steering position and 1D4 as its rate, but
		// the drive log says |1D0| stays under 0.011 while 1D4 reaches 0.54. Both
		// terms below are therefore suspect. Run one lap with FFBDiagnosticLog
		// and read the FFB STEERSCAN line before tuning either.
		float steer = car->field_1D0;                      // scale unverified
		float steerRate = car->field_1D4;                  // scale unverified

		// ---- Surface roughness from the game's own per-surface table ----
		// sub_1149C0 is the exact LUT the game's Xbox vibration code shipped
		// with: asphalt=0.0 (silent), sand=0.25, grass=0.70, rough=0.85-0.9,
		// water 0.73-0.79 on lake stages (sets waterFlag). Max over 4 tires,
		// same as the original code.
		DWORD waterFlag = 0;
		float roughness = 0.0f;
		for (int i = 0; i < 4; i++)
		{
			roughness = std::max(roughness, (float)sub_1149C0(
				car->water_flag_24C[i], (int)car->OnRoadPlace_5C.loadColiType_0, &waterFlag));
		}

		// ================================================================
		// SIGNAL CONDITIONING -- lateral slide EMA, drift depth, histories
		// ================================================================

		{
			float lateralCombined = (lateralForce1 + lateralForce2);

			// Dual-rate EMA: fast attack (0.25) for responsive corner entry,
			// faster decay (0.20) for snappy arcade feel when straightening.
			float alpha = (std::abs(lateralCombined) > std::abs(smoothedLateral)) ? 0.25f : 0.20f;
			smoothedLateral = alpha * lateralCombined + (1.0f - alpha) * smoothedLateral;
		}

		// Pre-crash lateral history: the collision response corrupts the lateral
		// signal at impact time, so crash direction reads ~8 frames back
		latHistory[latHistoryIdx % 16] = smoothedLateral;
		latHistoryIdx++;

		// Subtractive deadzone on the road-load term only (1.5, was a hard-zero
		// at 5.0 -- ~21% of signal range -- which left the wheel limp through
		// straights and gentle sweepers). The virtual spring now carries center
		// feel, so this only clips the true noise floor.
		float latDz = 0.0f;
		{
			float mag = std::abs(smoothedLateral) - Settings::FFBLateralDeadzone;
			if (mag > 0.0f)
				latDz = (smoothedLateral > 0.0f) ? mag : -mag;
		}
		float latNorm = std::clamp(latDz / 24.0f, -1.0f, 1.0f);

		// Drift depth 0..1 -- the game's slide fields ARE its drift state
		// (the Xbox vibration code uses them purely as slide detectors)
		float driftAmt = std::clamp((std::abs(smoothedLateral) - 12.0f) / 12.0f, 0.0f, 1.0f);

		// THE arcade-drift cue: the wheel LIGHTENS as grip is lost
		// (front tires unloading), instead of getting heavier as before
		float gripFactor = 1.0f - Settings::FFBGripLoss * driftAmt;

		// Track speed history for crash detection + weight transfer (sliding window)
		speedHistory[speedHistoryIdx % 8] = speed;
		speedHistoryIdx++;

		// Detect crash: compare current speed to speed 6 frames ago
		// Wall deceleration is spread across many frames, so per-frame delta is tiny.
		// A 6-frame window (~100ms) captures the full deceleration event.
		// Observed wall hit deltas: ~0.04-0.06 over 6 frames.
		if (crashImpulseTimer <= 0 && speedHistoryIdx > 6)
		{
			float oldSpeed = speedHistory[(speedHistoryIdx - 6) % 8];
			float windowDelta = oldSpeed - speed;
			if (windowDelta > 0.03f && speed > 0.1f) // 3% speed loss at speed = wall hit
			{
				// Direction from PRE-crash lateral history: the collision response
				// corrupts the instantaneous lateral signal at impact (this is why
				// steering angle was abandoned too). Push away from the wall side.
				float latPre = (latHistoryIdx > 8) ? latHistory[(latHistoryIdx - 8) % 16] : smoothedLateral;
				float impactDir = (latPre >= 0.0f) ? -1.0f : 1.0f;

				// Strong jolt that cuts through steering weight (1.5 > max steering of 1.0)
				crashImpulseForce = impactDir * 1.5f * Settings::FFBWallImpact;
				crashImpulseTimer = 90; // 1.5 sec cooldown (force active first 10 frames, then lockout)
				// Reset lateral EMA so the collision physics spike doesn't sustain
				// a "pinned" steering weight force after the crash impulse ends.
				smoothedLateral = 0.0f;
				spdlog::info("FFB: CRASH impulse! windowDelta={:.3f} dir={:.0f} latPre={:.2f} force={:.2f}",
					windowDelta, impactDir, latPre, crashImpulseForce);
			}
		}

		// Also trigger on flags8 0x1000 edge (contact event)
		{
			bool collisionActive = (stateFlags & 0x1000) != 0;
			bool wasColliding = (prevCollisionFlags & 0x1000) != 0;
			if (collisionActive && !wasColliding && crashImpulseTimer <= 0)
			{
				// Same pre-crash direction logic as the speed-delta path
				float latPre = (latHistoryIdx > 8) ? latHistory[(latHistoryIdx - 8) % 16] : smoothedLateral;
				float flagDir = (latPre >= 0.0f) ? -1.0f : 1.0f;
				crashImpulseForce = flagDir * 1.2f * Settings::FFBWallImpact;
				crashImpulseTimer = 90;
				smoothedLateral = 0.0f; // Reset EMA to prevent post-crash pinning
				spdlog::info("FFB: CRASH impulse from flags8 0x1000! dir={:.0f} latPre={:.2f} force={:.2f}",
					flagDir, latPre, crashImpulseForce);
			}
		}

		// ================================================================
		// VIBRATION ENVELOPES -- computed here, rendered either on hardware
		// periodic effects (preferred) or via CF-fallback synthesis
		// ================================================================

		// Road texture: the game's own formula (roughness x speed). Asphalt has
		// roughness 0.0 -> silent (correct: smooth tarmac has no 30 Hz buzz; the
		// spring gradient carries "road connection").
		float roadAmp = roughness * speedNorm * Settings::FFBRoadTexture;
		float roadFreq = 25.0f + 12.0f * speedNorm;

		// Water splash burst at high speed on water surfaces (game's own numbers)
		if (waterFlag && roughness > 0.7f && speed > 0.95f && splashTimer <= 0)
		{
			splashAmp = (roughness - 0.7f) * speed * 0.75f;
			splashTimer = 9; // ~150ms
		}
		if (splashTimer > 0)
		{
			roadAmp = std::max(roadAmp, splashAmp);
			splashTimer--;
		}

		// Tire slip chatter: ramps in with drift depth, frequency dropping
		// 40 -> 28 Hz as the slide deepens (stick-slip period grows).
		// Shares its sine with engine idle -- the states are mutually exclusive.
		float slipAmp = 0.0f;
		float slipFreq = 40.0f;
		if (driftAmt > 0.15f && speed > 0.1f)
		{
			slipAmp = driftAmt * Settings::FFBTireSlip;
			slipFreq = 40.0f - 12.0f * driftAmt;
		}
		else if (speed < 0.05f && car->pedal_amount_34 > 0)
		{
			// Engine idle/launch rumble -- the only engine vibration kept.
			// Continuous at-speed engine ripple is gone: real cabinets didn't
			// render it through the steering motor, and at speed "aliveness"
			// now comes from road texture (which actually renders).
			float throttleNorm = std::clamp(static_cast<float>(car->pedal_amount_34) / 255.0f, 0.0f, 1.0f);
			slipAmp = Settings::FFBEngineIdle * throttleNorm;
			slipFreq = 15.0f + 7.0f * throttleNorm;
		}

		// ================================================================
		// CONSTANT FORCE -- center-out model:
		// backbone = virtual spring on steering position (what the arcade
		// cabinet's mechanical centering did) damped by the game's steering
		// derivative; lateral road load is a SECONDARY term that lightens as
		// the slide deepens; weight transfer modulates; events pulse on top.
		// ================================================================

		if (ffbLoaded && useSharedModel)
		{
			// The game's signals, handed to the shared model. Everything here is
			// already computed above by code that knows OutRun; none of it is
			// tuning, and all the tuning lives in the profile.
			dbce::force::Inputs in;
			in.steer = steer;                       in.has_steer = true;
			in.steer_rate = steerRate * 60.0f;      in.has_steer_rate = true;   // per frame -> per second
			in.speed_mps = speed * Telemetry::MaxSpeedMps;
			// latNorm is already -1..1; the profile's gReference is 1.0 so the
			// model passes it through unchanged.
			in.lateral_g = latNorm;                 in.has_lateral_g = true;
			in.drift_amount = driftAmt;             in.has_drift = true;
			if (speedHistoryIdx > 6)
			{
				in.longitudinal_g = (speed - speedHistory[(speedHistoryIdx - 6) % 8]) * 10.0f;
				in.has_longitudinal_g = true;
			}
			// Texture rides the hardware periodics when the driver has them; the
			// model's own texture term is the fallback.
			if (!periodicsActive) in.texture = std::max(roadAmp, slipAmp);
			if (crashImpulseTimer == 90)            // the tick the crash was detected
			{
				in.impact = std::min(1.0f, std::abs(crashImpulseForce));
				in.impact_direction = crashImpulseForce >= 0.0f ? 1.0f : -1.0f;
			}
			in.gear_shift = (curGear != sharedPrevGear && sharedPrevGear != 0);
			sharedPrevGear = curGear;

			const float dt = 1.0f / 60.0f;
			float shaped = sharedShaper->shape(sharedModel->compute(in, dt),
				speed * Telemetry::MaxSpeedMps * 3.6f, dt, sharedModel->last_was_event);

			// FFBGlobalStrength stays the user's master dial on top of the
			// profile's own shaper.strength, and is applied inside SetConstantForce.
			LONG diMagnitude = std::clamp((LONG)(shaped * 10000.0f), (LONG)-10000, (LONG)10000);
			if (std::abs(diMagnitude - prevConstantLevel) > 15 || sharedModel->last_was_event)
				SetConstantForce(diMagnitude);
		}
		else if (ffbLoaded)
		{
			// --- Backbone: virtual spring ---
			// speedCurve rises fast (full effect by 25% speed) then keeps growing
			// linearly -- parked wheel stays light for menus and the start line,
			// force arrives with the launch. Near-linear speed scaling matches the
			// arcade cab (speed-squared curves read as sim-like).
			float speedCurve = std::clamp(speed / 0.25f, 0.0f, 1.0f) * (0.35f + 0.65f * speedNorm);
			float F_spring = -steer * Settings::FFBSpringStrength * speedCurve;

			// --- Backbone: virtual damper on the game's own steering derivative ---
			// field_1D4 is a per-frame steering delta (small values); the scale
			// factor normalizes it into the same range as the spring term.
			// Verify observed range via FFBDiagnosticLog before fine-tuning.
			// Damper floor of 0.4 keeps a DD wheel from oscillating at low speed
			// where the spring is weak.
			constexpr float STEER_RATE_SCALE = 20.0f;
			float F_damper = -steerRate * STEER_RATE_SCALE * Settings::FFBDamperStrength * (0.4f + 0.6f * speedNorm);

			// --- Secondary: lateral road load, lightened by grip loss ---
			// In grip the wheel loads up; in a drift it goes light (gripFactor).
			// This replaces lateral-slide-as-the-whole-force, which inverted the
			// real relationship (max heaviness exactly when grip was LOST).
			float F_lat = latNorm * speedNorm * Settings::FFBSteeringWeight * gripFactor;

			// --- Weight transfer: modulates, doesn't add ---
			// Hard braking adds up to +30% weight, full throttle sheds up to 20%.
			// A multiplier cannot pull the wheel on a straight.
			float loadMod = 1.0f;
			if (speedHistoryIdx > 6)
			{
				float longAccel = (speed - speedHistory[(speedHistoryIdx - 6) % 8]) * 10.0f;
				loadMod = 1.0f + std::clamp(-longAccel * Settings::FFBWeightTransfer, -0.20f, 0.30f);
			}

			// Structural force (suppressed during the active crash jolt to
			// prevent force stacking)
			float F_struct = 0.0f;
			if (crashImpulseTimer <= 80)
				F_struct = (F_spring + F_lat) * loadMod + F_damper;

			// --- Events ---
			float F_events = 0.0f;

			// Crash impulse (time-limited jolt with long cooldown)
			// Timer starts at 90: frames 90-81 = active jolt, 80-1 = cooldown (no force, no re-trigger)
			if (crashImpulseTimer > 0)
			{
				if (crashImpulseTimer > 80) // Active jolt phase (first 10 frames = ~167ms)
				{
					float envelope;
					if (crashImpulseTimer > 85)
						envelope = 1.0f; // Full force for first ~83ms
					else
						envelope = float(crashImpulseTimer - 80) / 5.0f; // Decay over ~83ms

					F_events += crashImpulseForce * envelope;
				}
				// Frames 80-1: cooldown only, no force applied, prevents re-trigger
				crashImpulseTimer--;
			}

			// Gear shift: symmetric double pulse (+K then -K -- a "thunk").
			// A directional kick reads as "the game yanked the wheel sideways";
			// a real shift jolt is longitudinal, so the lateral pulse must net to zero.
			if (curGear != prevGear && prevGear != 0 && gearShiftTimer <= 0)
				gearShiftTimer = 6; // ~100ms at 60fps

			if (gearShiftTimer > 0)
			{
				float thunk = 0.2f * Settings::FFBGearShift * ((gearShiftTimer > 3) ? 1.0f : -1.0f);
				F_events += thunk;
				gearShiftTimer--;
			}

			float totalForce = F_struct + F_events;

			// Apply inversion if configured
			if (Settings::FFBInvertForce)
				totalForce = -totalForce;

			// Warmup ramp (garbage first frames) and post-recreation ramp-in (anti-jerk)
			totalForce *= warmupScale * recreateScale;

			// Soft saturation via tanh: preserves relative force differences
			// near the limit instead of hard-clipping to +/-1.0.
			float compressed = std::tanh(totalForce);

			// Slew-rate limiter on the STRUCTURAL force only: prevent
			// micro-oscillations on DD wheels by capping change per frame.
			// Crash impulses and gear shift pulses bypass the limiter.
			LONG structMag = (LONG)(compressed * 10000.0f);
			LONG slewDelta = structMag - prevStructLevel;
			constexpr LONG maxSlew = 600; // ~6% of 10000
			bool bypassSlew = (crashImpulseTimer > 80) || (gearShiftTimer > 0);
			if (std::abs(slewDelta) > maxSlew && !bypassSlew)
				structMag = prevStructLevel + ((slewDelta > 0) ? maxSlew : -maxSlew);
			prevStructLevel = structMag;

			// --- CF-fallback vibration (only when hardware periodics are absent) ---
			// Injected AFTER the tanh compressor so cornering load can't eat the
			// ripple (at a load of 0.6 the local tanh slope is ~0.71, at 1.0 it's
			// ~0.42 -- pre-compressor vibration lost 30-60% exactly when it
			// mattered). Synth frequencies capped at 15 Hz: zero-order-hold loss
			// at 15/60 is only ~11%, vs ~26% at 25 Hz.
			float vib = 0.0f;
			if (!periodicsActive)
			{
				if (roadAmp > 0.005f)
				{
					float f = std::min(roadFreq, 15.0f);
					roadPhase = std::fmod(roadPhase + f / 60.0f * 6.2832f, 6.2832f);
					vib += std::sin(roadPhase) * roadAmp;
				}
				else
					roadPhase = 0.0f;

				if (slipAmp > 0.005f)
				{
					float f = std::min(slipFreq, 15.0f);
					slipPhase = std::fmod(slipPhase + f / 60.0f * 6.2832f, 6.2832f);
					vib += std::sin(slipPhase) * slipAmp;
				}
				else
					slipPhase = 0.0f;
			}

			// Convert to DirectInput range: ±10000 (matching test bench)
			LONG diMagnitude = std::clamp(structMag + (LONG)(vib * 10000.0f), (LONG)-10000, (LONG)10000);

			// Deadband: skip updating if the level barely changed.
			LONG delta = std::abs(diMagnitude - prevConstantLevel);
			if (delta > 15 || crashImpulseTimer > 80)
			{
				SetConstantForce(diMagnitude);
			}
		}

		// ================================================================
		// PERIODIC CHANNEL -- hardware-rendered vibration envelopes (~15 Hz)
		// ================================================================

		if (periodicsActive && updateEnvelopes)
		{
			UpdatePeriodic(slotRoadTexture, roadAmp, roadFreq);
			UpdatePeriodic(slotTireSlip, slipAmp, slipFreq);
		}
		// A slot whose handle dies is recreated inside the DLL, behind its own
		// 500 ms hold-off, so there is nothing to retry from here.

		// Diagnostic logging: every 2 seconds (gated behind FFBDiagnosticLog)
		if (Settings::FFBDiagnosticLog)
		{
			diagSteerRateMin = std::min(diagSteerRateMin, steerRate);
			diagSteerRateMax = std::max(diagSteerRateMax, steerRate);

			// Min/max, not an instantaneous sample: the old line sampled steer
			// once every two seconds, which is what made a signal 100x too small
			// look merely quiet.
			const float probed[] = { car->field_1C8, car->field_1CC, car->field_1D0,
			                         car->field_1D4, car->field_1DC, car->field_1E0 };
			static_assert(sizeof(probed) / sizeof(probed[0]) == sizeof(steerProbe) / sizeof(steerProbe[0]),
				"steerProbe names and probed values must line up");
			for (size_t pi = 0; pi < sizeof(probed) / sizeof(probed[0]); pi++)
			{
				steerProbe[pi].lo = std::min(steerProbe[pi].lo, probed[pi]);
				steerProbe[pi].hi = std::max(steerProbe[pi].hi, probed[pi]);
			}

			static DWORD lastDiagTime = 0;
			DWORD now = GetTickCount();
			if (now - lastDiagTime >= 2000)
			{
				lastDiagTime = now;
				spdlog::info("FFB DIAG: spd={:.3f} steer={:.3f} rate=[{:.5f}..{:.5f}] lat={:.2f} drift={:.2f} rough={:.2f} constLvl={} periodics={} warmup={}/{}",
					speed, steer, diagSteerRateMin, diagSteerRateMax, smoothedLateral, driftAmt, roughness,
					(int)prevConstantLevel, periodicsActive, warmupFrames, WARMUP_THRESHOLD);

				char scan[256];
				int at = 0;
				for (size_t pi = 0; pi < sizeof(probed) / sizeof(probed[0]) && at >= 0 && at < (int)sizeof(scan); pi++)
				{
					int wrote = snprintf(scan + at, sizeof(scan) - at, "%s[%.4f..%.4f] ",
						steerProbe[pi].name, steerProbe[pi].lo, steerProbe[pi].hi);
					if (wrote < 0) break;
					at += wrote;
					steerProbe[pi].lo = 0.0f;
					steerProbe[pi].hi = 0.0f;
				}
				spdlog::info("FFB STEERSCAN: {}", scan);

				diagSteerRateMin = 0.0f;
				diagSteerRateMax = 0.0f;
			}
		}

		// Store previous frame state for next-frame edge detection
		prevGear = curGear;
		prevCollisionFlags = stateFlags;
		prevSpeed = speed;
	}

	void Shutdown()
	{
		Telemetry::Shutdown();

		if (!initialized)
			return;

		spdlog::info("FFB: Shutting down...");

		// This runs from DLL_PROCESS_DETACH, under the loader lock. PanicStop is
		// the part that must happen and is a no-op if an exit guard already ran.
		// Nothing else is torn down deliberately: FreeLibrary from DllMain is
		// forbidden, and releasing COM objects here is what used to fault
		// (0xC0000005 on all three effects) and silently skip the autocentre
		// restore. The process is exiting; the OS reclaims the rest.
		PanicStop();

		initialized = false;
		spdlog::info("FFB: Shutdown complete");
	}
}

// ====================================================================
// Hook class -- self-registering via static instance
// ====================================================================
class DirectInputFFBHook : public Hook
{
	const static int GamePlCar_Ctrl_Addr = 0xA8330;

	inline static SafetyHookInline GamePlCar_Ctrl = {};
	static void __cdecl GamePlCar_Ctrl_Hook(EVWORK_CAR* car)
	{
		FFB::Update(car);
		GamePlCar_Ctrl.call(car);
	}

public:
	std::string_view description() override
	{
		return "DirectInputFFB";
	}

	bool validate() override
	{
		return Settings::DirectInputFFB || Settings::TelemetryEnabled;
	}

	bool apply() override
	{
		// Only install the inline hook here -- FFB device init is deferred
		// to the first game tick (DirectInput needs a valid HWND).
		// FFB device initialization is deferred to the first Update() call.
		auto targetAddr = Module::exe_ptr(GamePlCar_Ctrl_Addr);
		GamePlCar_Ctrl = safetyhook::create_inline(targetAddr, GamePlCar_Ctrl_Hook);
		if (!GamePlCar_Ctrl)
		{
			spdlog::error("DirectInputFFB: Failed to hook GamePlCar_Ctrl");
			return false;
		}

		spdlog::info("DirectInputFFB: Hook installed (FFB init deferred to first game tick)");
		return true;
	}

	static DirectInputFFBHook instance;
};
DirectInputFFBHook DirectInputFFBHook::instance;
