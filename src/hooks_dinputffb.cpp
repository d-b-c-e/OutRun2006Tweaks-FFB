// DirectInput Force Feedback via SDL3 Haptic API
// Provides steering wheel force feedback using EVWORK_CAR telemetry data.
// Effects: center spring, damper, steering weight (constant force),
//          collision impact, rumble strip, gear shift, road texture, tire slip.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <SDL3/SDL.h>
#include <cmath>
#include <algorithm>
#include <string>

#include "hook_mgr.hpp"
#include "plugin.hpp"
#include "game_addrs.hpp"
#include "game.hpp"
#include "telemetry.hpp"

// External vibration data from hooks_forcefeedback.cpp
extern float VibrationLeftMotor;
extern float VibrationRightMotor;

// Telemetry shared memory (written every frame for SimHub / bass shakers)
namespace Telemetry
{
	static HANDLE hMapFile = nullptr;
	static OutRun2006TelemetryData* pData = nullptr;
	static bool initialized = false;
	static uint32_t packetId = 0;

	static bool Init()
	{
		if (!Settings::TelemetryEnabled)
			return false;

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
		return true;
	}

	static void Write(EVWORK_CAR* car, bool inGameplay)
	{
		if (!pData) return;

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
		initialized = false;
		spdlog::info("Telemetry: Shared memory closed");
	}
}

namespace FFB
{
	// Haptic device state
	static SDL_Haptic* hapticDevice = nullptr;
	static bool initialized = false;
	static bool initAttempted = false; // prevents repeated init attempts
	static std::string deviceName;
	static float wheelTorqueScale = 1.0f;

	// Effect IDs (-1 = not created)
	static int springEffectId = -1;
	static int damperEffectId = -1;
	static int constantEffectId = -1;
	static int sineEffectId = -1;
	static int frictionEffectId = -1;

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

	// Crash impulse state
	static int crashImpulseTimer = 0;      // Frames remaining for crash jolt
	static float crashImpulseForce = 0.0f; // Direction and magnitude of crash jolt

	// Previous constant force level (deadband to prevent micro-oscillations)
	static int16_t prevConstantLevel = 0;

	// Gear shift timer (frames remaining)
	static int gearShiftTimer = 0;

	// Known wheel profiles for direct drive torque scaling
	struct WheelProfile
	{
		const char* nameSubstring;
		float maxTorqueNm;
	};

	static const WheelProfile KnownWheels[] =
	{
		{"Moza R3", 3.9f},
		{"Moza R5", 5.5f},
		{"Moza R9", 9.0f},
		{"Moza R12", 12.0f},
		{"Moza R16", 16.0f},
		{"Moza R21", 21.0f},
		{"Fanatec CSL DD", 8.0f},
		{"Fanatec GT DD", 12.0f},
		{"Fanatec DD1", 20.0f},
		{"Fanatec DD2", 25.0f},
		{"Simucube", 25.0f},
		{"Simagic Alpha", 15.0f},
		{"Simagic M10", 10.0f},
		{"Logitech G29", 2.2f},
		{"Logitech G920", 2.2f},
		{"Logitech G923", 2.2f},
		{"Logitech G27", 2.2f},
		{"Logitech G25", 2.2f},
		{"Logitech G Pro", 11.0f},
		{"Logitech PRO", 11.0f},
		{"Thrustmaster T300", 3.9f},
		{"Thrustmaster T500", 3.0f},
		{"Thrustmaster T-GT", 4.2f},
		{"Thrustmaster TS-XW", 4.5f},
		{"Thrustmaster TS-PC", 4.5f},
		{"Thrustmaster T818", 10.0f},
		{"Thrustmaster T248", 3.5f},
		{"Thrustmaster T150", 2.5f},
		{"Thrustmaster TMX", 2.5f},
		{"VRS DFP", 20.0f},
		{"Cammus", 10.0f},
		{nullptr, 0.0f}
	};

	static constexpr float ReferenceNm = 2.2f; // Logitech G29 as baseline

	static float DetectWheelTorque(const char* name)
	{
		if (!name) return 0.0f;

		std::string devName(name);
		std::transform(devName.begin(), devName.end(), devName.begin(), ::tolower);

		for (int i = 0; KnownWheels[i].nameSubstring; i++)
		{
			std::string pattern(KnownWheels[i].nameSubstring);
			std::transform(pattern.begin(), pattern.end(), pattern.begin(), ::tolower);
			if (devName.find(pattern) != std::string::npos)
				return KnownWheels[i].maxTorqueNm;
		}
		return 0.0f;
	}

	static void CreateEffects(unsigned int features)
	{
		SDL_HapticEffect effect;

		// NOTE: Condition effects (Spring, Damper, Friction) are DISABLED.
		// DD wheel drivers (Moza, Fanatec, Simucube) have internal PID controllers
		// that oscillate when fighting external spring/damper effects, causing
		// persistent vibration. The arcade game only uses constant force anyway.
		// Centering feel should come from the wheel's own spring setting in its
		// configuration software (e.g., Moza Pit House).

		if (Settings::FFBSpringStrength > 0.0f)
			spdlog::info("FFB: Spring disabled for DD wheels (use wheel software for centering)");
		if (Settings::FFBDamperStrength > 0.0f)
			spdlog::info("FFB: Damper disabled for DD wheels (use wheel software for damping)");

		// 1. Constant force -- steering weight, collision jolts, gear shift kicks
		// This is the ONLY force-producing effect. Matches the arcade approach
		// which uses a single motor output byte for all FFB.
		if (features & SDL_HAPTIC_CONSTANT)
		{
			memset(&effect, 0, sizeof(effect));
			effect.type = SDL_HAPTIC_CONSTANT;
			effect.constant.length = SDL_HAPTIC_INFINITY;
			effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
			effect.constant.direction.dir[0] = 1;
			effect.constant.level = 0;

			constantEffectId = SDL_CreateHapticEffect(hapticDevice, &effect);
			if (constantEffectId >= 0)
			{
				SDL_RunHapticEffect(hapticDevice, constantEffectId, SDL_HAPTIC_INFINITY);
				spdlog::info("FFB: Constant force created (id: {})", constantEffectId);
			}
			else
				spdlog::warn("FFB: Failed to create constant force: {}", SDL_GetError());
		}

		// Sine wave and friction are NOT created — they also cause DD oscillation.
		// Rumble strip / road texture can be revisited later with careful tuning
		// or by using short-duration effects instead of persistent ones.

	}

	// Deferred initialization -- called from Update() on first tick, NOT from DllMain.
	// SDL_Init creates threads internally, which deadlocks if called during DllMain
	// (Windows loader lock prevents thread creation during DLL_PROCESS_ATTACH).
	bool DeferredInit()
	{
		if (initAttempted)
			return initialized;
		initAttempted = true;

		spdlog::info("FFB: Starting deferred initialization...");

		// Initialize SDL haptic + joystick subsystems
		// SDL_Init is safe to call multiple times; it adds subsystems incrementally
		if (!SDL_Init(SDL_INIT_HAPTIC | SDL_INIT_JOYSTICK))
		{
			spdlog::error("FFB: Failed to init SDL haptic subsystem: {}", SDL_GetError());
			return false;
		}

		// Enumerate haptic devices
		int numHaptics = 0;
		SDL_HapticID* hapticIds = SDL_GetHaptics(&numHaptics);

		if (!hapticIds || numHaptics <= 0)
		{
			spdlog::warn("FFB: No haptic devices found -- FFB effects will be disabled");
			if (hapticIds) SDL_free(hapticIds);
			return false;
		}

		spdlog::info("FFB: Found {} haptic device(s):", numHaptics);
		for (int i = 0; i < numHaptics; i++)
		{
			const char* name = SDL_GetHapticNameForID(hapticIds[i]);
			spdlog::info("  [{}] {}", i, name ? name : "(unknown)");
		}

		// Select device: -1 = auto (first), 0+ = specific index
		int deviceIdx = Settings::FFBDevice;
		if (deviceIdx < 0 || deviceIdx >= numHaptics)
			deviceIdx = 0;

		hapticDevice = SDL_OpenHaptic(hapticIds[deviceIdx]);
		SDL_free(hapticIds);

		if (!hapticDevice)
		{
			spdlog::error("FFB: Failed to open haptic device {}: {}", deviceIdx, SDL_GetError());
			return false;
		}

		// Query device capabilities
		const char* rawName = SDL_GetHapticName(hapticDevice);
		deviceName = rawName ? rawName : "(unknown)";
		unsigned int features = SDL_GetHapticFeatures(hapticDevice);
		int maxEffects = SDL_GetMaxHapticEffects(hapticDevice);
		int maxPlaying = SDL_GetMaxHapticEffectsPlaying(hapticDevice);

		spdlog::info("FFB: Opened '{}' (features: 0x{:08X}, max effects: {}, max playing: {})",
			deviceName, features, maxEffects, maxPlaying);

		// Log supported effect types
		if (features & SDL_HAPTIC_CONSTANT) spdlog::info("FFB:   Supports: Constant force");
		if (features & SDL_HAPTIC_SINE) spdlog::info("FFB:   Supports: Sine wave");
		if (features & SDL_HAPTIC_SPRING) spdlog::info("FFB:   Supports: Spring");
		if (features & SDL_HAPTIC_DAMPER) spdlog::info("FFB:   Supports: Damper");
		if (features & SDL_HAPTIC_FRICTION) spdlog::info("FFB:   Supports: Friction");
		if (features & SDL_HAPTIC_GAIN) spdlog::info("FFB:   Supports: Gain control");
		if (features & SDL_HAPTIC_AUTOCENTER) spdlog::info("FFB:   Supports: Autocenter control");

		// Direct drive torque scaling
		float wheelNm = Settings::FFBWheelTorqueNm;
		if (wheelNm <= 0.0f)
		{
			wheelNm = DetectWheelTorque(deviceName.c_str());
			if (wheelNm > 0.0f)
				spdlog::info("FFB: Auto-detected wheel: {:.1f} Nm (profile match)", wheelNm);
			else
				spdlog::info("FFB: Unknown wheel -- using reference scaling (set FFBWheelTorqueNm in INI for proper DD scaling)");
		}
		else
		{
			spdlog::info("FFB: Manual wheel torque: {:.1f} Nm", wheelNm);
		}

		if (wheelNm > ReferenceNm)
			wheelTorqueScale = ReferenceNm / wheelNm;
		else
			wheelTorqueScale = 1.0f;

		spdlog::info("FFB: Torque scale: {:.3f} (reference: {:.1f} Nm)", wheelTorqueScale, ReferenceNm);

		// Disable hardware autocenter -- we provide our own spring
		if (features & SDL_HAPTIC_AUTOCENTER)
			SDL_SetHapticAutocenter(hapticDevice, 0);

		// Set global gain (user strength only -- torque scaling applied per-force)
		if (features & SDL_HAPTIC_GAIN)
		{
			int gain = int(Settings::FFBGlobalStrength * 100.0f);
			gain = std::clamp(gain, 0, 100);
			SDL_SetHapticGain(hapticDevice, gain);
			spdlog::info("FFB: Global gain set to {}% (torque scale {:.3f} applied to steering weight only)", gain, wheelTorqueScale);
		}

		CreateEffects(features);

		initialized = true;
		spdlog::info("FFB: Initialization complete");
		return true;
	}

	int updateCounter = 0;

	// Phase accumulators for sine wave synthesis (smooth vibration effects)
	float rumblePhase = 0.0f;
	float slipPhase = 0.0f;
	float enginePhase = 0.0f;

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
		if (!initialized || !hapticDevice || constantEffectId < 0)
			return;

		DWORD now = GetTickCount();
		DWORD elapsed = now - lastUpdateTick;

		// If Update() hasn't been called for 250ms and forces are non-zero, zero them
		if (elapsed > 250 && lastUpdateTick > 0 && prevConstantLevel != 0)
		{
			SDL_HapticEffect effect;
			memset(&effect, 0, sizeof(effect));
			effect.type = SDL_HAPTIC_CONSTANT;
			effect.constant.length = SDL_HAPTIC_INFINITY;
			effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
			effect.constant.direction.dir[0] = 1;
			effect.constant.level = 0;
			SDL_UpdateHapticEffect(hapticDevice, constantEffectId, &effect);
			prevConstantLevel = 0;
			smoothedLateral = 0.0f;
			crashImpulseTimer = 0;
			spdlog::info("FFB: Watchdog zeroed forces (no Update for {}ms)", elapsed);
		}
	}

	void Update(EVWORK_CAR* car)
	{
		if (!car)
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

		// Lazy initialization: SDL_Init must happen AFTER DllMain returns
		// (it creates threads, which deadlock under the Windows loader lock)
		if (!initialized)
		{
			if (!DeferredInit())
				return;
		}

		// Zero forces when not in gameplay (menus, results, etc.)
		// Prevents the wheel from staying stuck at the last force level
		if (!inGameplay)
		{
			if (constantEffectId >= 0 && prevConstantLevel != 0)
			{
				SDL_HapticEffect effect;
				memset(&effect, 0, sizeof(effect));
				effect.type = SDL_HAPTIC_CONSTANT;
				effect.constant.length = SDL_HAPTIC_INFINITY;
				effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
				effect.constant.direction.dir[0] = 1;
				effect.constant.level = 0;
				SDL_UpdateHapticEffect(hapticDevice, constantEffectId, &effect);
				prevConstantLevel = 0;
				smoothedLateral = 0.0f;
				crashImpulseTimer = 0;
			}
			return;
		}

		// Update rates:
		// - Constant force: every frame (60 Hz) for responsive steering feel.
		//   Prior 30 Hz throttle caused sluggish force transitions.
		// - Condition effects (spring/damper): every 4th frame (~15 Hz) since
		//   DD wheel drivers create motor transients on reprogramming.
		updateCounter++;
		bool updateConditions = (updateCounter % 4 == 0);
		bool updateForces = true;

		// Read telemetry from EVWORK_CAR
		float speed = car->field_1C4;                      // Normalized speed (0.0 - ~1.0+)
		float speedNorm = std::clamp(speed, 0.0f, 1.0f);
		uint32_t stateFlags = car->field_8;                // State/collision bits
		uint32_t carFlags = car->flags_4;                  // Car flags
		float lateralForce1 = car->field_264;              // Lateral force component
		float lateralForce2 = car->field_268;              // Lateral force component
		uint32_t curGear = car->cur_gear_208;              // Current gear number
		uint32_t prevGearState = car->dword1D8;            // Previous gear (per game logic)
		float impactForce = car->field_178;                // Contact/impact force magnitude
		float steeringAngle = car->field_1D0;              // Steering position / yaw rate

		// Surface type from tire contact flags
		uint32_t surfFlags0 = car->water_flag_24C[0];
		uint32_t surfFlags1 = car->water_flag_24C[1];
		uint32_t surfFlags2 = car->water_flag_24C[2];
		uint32_t surfFlags3 = car->water_flag_24C[3];

		// Non-zero and non-1 surface flags generally indicate off-road/rough surface
		// Value 1 = normal asphalt, 2 = sand/gravel, 4 = grass, etc.
		bool offRoad = false;
		if (surfFlags0 > 1 || surfFlags1 > 1 || surfFlags2 > 1 || surfFlags3 > 1)
			offRoad = true;

		SDL_HapticEffect effect;

		// ================================================================
		// CENTER SPRING + DAMPER -- fixed coefficients, NO runtime updates.
		// DD wheel drivers (Moza, Fanatec, etc.) create motor transients when
		// effect parameters are reprogrammed via SDL_UpdateHapticEffect, which
		// causes a ~15 Hz vibration pattern. Coefficients are set once during
		// CreateEffects and left running. Speed-dependent centering is handled
		// by the constant force instead.
		// ================================================================

		// ================================================================
		// CONSTANT FORCE -- steering weight + collision + gear shift
		// ================================================================

		// Always update smoothed lateral (even on non-update frames) for consistent filtering
		{
			float lateralCombined = (lateralForce1 + lateralForce2);
			// Dual-rate EMA: fast attack (0.25) for responsive corner entry,
			// slow decay (0.10) for smooth force release when straightening.
			// This mimics real self-aligning torque behavior — builds quickly
			// as you enter a turn, fades gradually as you exit.
			float alpha = (std::abs(lateralCombined) > std::abs(smoothedLateral)) ? 0.25f : 0.10f;
			smoothedLateral = alpha * lateralCombined + (1.0f - alpha) * smoothedLateral;
		}

		// Track speed history for crash detection (sliding window)
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
				float impactMag = std::clamp((windowDelta - 0.03f) * 12.0f, 0.8f, 1.0f);
				// Use steering angle for crash direction: you hit the wall on the side you steer toward
				float impactDir = (steeringAngle >= 0.0f) ? -1.0f : 1.0f; // Push away from wall

				crashImpulseForce = impactDir * impactMag * Settings::FFBWallImpact;
				crashImpulseTimer = 90; // 1.5 sec cooldown (force active first 10 frames, then lockout)
				// Reset lateral EMA so the collision physics spike doesn't sustain
				// a "pinned" steering weight force after the crash impulse ends.
				smoothedLateral = 0.0f;
				spdlog::info("FFB: CRASH impulse! windowDelta={:.3f} mag={:.2f} dir={:.0f} steerAngle={:.2f} force={:.2f}",
					windowDelta, impactMag, impactDir, steeringAngle, crashImpulseForce);
			}
		}

		// Also trigger on flags8 0x1000 edge (contact event)
		{
			bool collisionActive = (stateFlags & 0x1000) != 0;
			bool wasColliding = (prevCollisionFlags & 0x1000) != 0;
			if (collisionActive && !wasColliding && crashImpulseTimer <= 0)
			{
				// Use steering angle for crash direction (same logic as speed-delta path)
				float flagDir = (steeringAngle >= 0.0f) ? -1.0f : 1.0f;
				crashImpulseForce = flagDir * 0.8f * Settings::FFBWallImpact;
				crashImpulseTimer = 90;
				smoothedLateral = 0.0f; // Reset EMA to prevent post-crash pinning
				spdlog::info("FFB: CRASH impulse from flags8 0x1000! dir={:.0f} steerAngle={:.2f} force={:.2f}",
					flagDir, steeringAngle, crashImpulseForce);
			}
		}

		if (constantEffectId >= 0 && updateForces)
		{
			float totalForce = 0.0f;

			// --- Steering weight (self-aligning torque approximation) ---
			// Uses an inverted-U SAT curve: force builds linearly at low slip,
			// peaks at ~70% of max lateral G, then drops — giving the driver
			// an "understeer warning" as the wheel goes light before grip is lost.
			// Suppressed during active crash impulse to prevent force stacking.
			if (crashImpulseTimer <= 80)
			{
				float lateralNorm = std::clamp(smoothedLateral / 24.0f, -1.0f, 1.0f);

				// SAT curve: inverted-U with drop-off beyond peak grip
				float absLat = std::abs(lateralNorm);
				float satForce;
				if (absLat < 0.7f)
				{
					satForce = lateralNorm; // Linear region
				}
				else
				{
					// Beyond peak grip: force drops (pneumatic trail collapse)
					float sign = (lateralNorm > 0.0f) ? 1.0f : -1.0f;
					float dropoff = 1.0f - 2.0f * (absLat - 0.7f);
					satForce = sign * 0.7f * std::max(dropoff, 0.4f);
				}

				float steeringWeight = satForce * speedNorm * Settings::FFBSteeringWeight * 0.12f * wheelTorqueScale;
				float maxSteeringForce = 0.12f * wheelTorqueScale;
				steeringWeight = std::clamp(steeringWeight, -maxSteeringForce, maxSteeringForce);
				totalForce += steeringWeight;
			}

			// --- Crash impulse (time-limited jolt with long cooldown) ---
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

					totalForce += crashImpulseForce * envelope;
				}
				// Frames 80-1: cooldown only, no force applied, prevents re-trigger
				crashImpulseTimer--;
			}

			// --- Gear shift kick ---
			if (curGear != prevGear && prevGear != 0 && gearShiftTimer <= 0)
				gearShiftTimer = 6; // ~100ms at 60fps

			if (gearShiftTimer > 0)
			{
				float kickDecay = float(gearShiftTimer) / 6.0f;
				float kickForce = kickDecay * 0.3f * Settings::FFBGearShift;
				totalForce += (curGear > prevGear) ? kickForce : -kickForce;
				gearShiftTimer--;
			}

			// --- Surface rumble (off-road / rumble strip) ---
			// Sine wave synthesis at 30 Hz for smooth vibration feel on DD wheels.
			// Square waves have harsh harmonics that feel buzzy; sine is natural.
			if (offRoad && speed > 0.05f)
			{
				rumblePhase = std::fmod(rumblePhase + 30.0f / 60.0f * 6.2832f, 6.2832f);
				float rumbleWave = std::sin(rumblePhase);
				float rumbleIntensity = speedNorm * Settings::FFBRumbleStrip * 0.2f * wheelTorqueScale;
				totalForce += rumbleWave * rumbleIntensity;
			}
			else
			{
				rumblePhase = 0.0f; // Reset phase when not on rumble surface
			}

			// --- Tire slip rumble (high lateral forces = losing grip) ---
			// Sine wave at 22 Hz — slightly offset from rumble strip frequency
			// to avoid harmonic reinforcement when both are active.
			float lateralMag = std::abs(smoothedLateral);
			if (lateralMag > 12.0f && speed > 0.1f)
			{
				slipPhase = std::fmod(slipPhase + 22.0f / 60.0f * 6.2832f, 6.2832f);
				float slipWave = std::sin(slipPhase);
				float slipAmount = std::clamp((lateralMag - 12.0f) / 18.0f, 0.0f, 1.0f);
				float slipRumble = slipWave * slipAmount * Settings::FFBTireSlip * 0.12f * wheelTorqueScale;
				totalForce += slipRumble;
			}
			else
			{
				slipPhase = 0.0f;
			}

			// --- Engine rev vibration (stationary + low speed) ---
			// Sine wave at 15 Hz for smooth idle rumble (not buzzy square wave).
			if (speed < 0.15f)
			{
				enginePhase = std::fmod(enginePhase + 15.0f / 60.0f * 6.2832f, 6.2832f);
				float engineWave = std::sin(enginePhase);

				float engineIntensity = 0.6f;
				float motorVal = std::max(VibrationLeftMotor, VibrationRightMotor);
				if (motorVal > 0.05f)
					engineIntensity = motorVal;

				float speedFade = 1.0f - (speed / 0.15f);
				float revForce = engineWave * engineIntensity * speedFade * 0.25f * wheelTorqueScale;
				totalForce += revForce;
			}
			else
			{
				enginePhase = 0.0f;
			}

			// Apply inversion if configured
			if (Settings::FFBInvertForce)
				totalForce = -totalForce;

			// Soft saturation via tanh: preserves relative force differences
			// near the limit instead of hard-clipping to +/-1.0.
			// Forces below ~0.5 are nearly linear; above 0.8 they compress smoothly.
			totalForce = std::tanh(totalForce);
			int16_t level = int16_t(totalForce * 32767.0f);

			// Slew-rate limiter: prevent micro-oscillations on DD wheels
			// by capping how fast the force can change between frames.
			// Max 6% change per frame (~60 Hz = full sweep in ~280ms).
			// Crash impulses and gear shift kicks bypass the limiter
			// so they feel instant and don't get "pinned" by slow decay.
			int16_t slewDelta = level - prevConstantLevel;
			constexpr int16_t maxSlew = 2000; // ~6% of 32768
			bool bypassSlew = (crashImpulseTimer > 80) || (gearShiftTimer > 0);
			if (std::abs(slewDelta) > maxSlew && !bypassSlew)
				level = prevConstantLevel + ((slewDelta > 0) ? maxSlew : -maxSlew);

			// Deadband: skip updating if the level barely changed.
			// Reduced from 500 (1.5%) to 200 (0.6%) — the slew-rate limiter
			// now handles micro-oscillation prevention, so we can use a
			// tighter deadband for better low-force detail.
			int16_t delta = std::abs(level - prevConstantLevel);
			if (delta > 200 || crashImpulseTimer > 80)
			{
				memset(&effect, 0, sizeof(effect));
				effect.type = SDL_HAPTIC_CONSTANT;
				effect.constant.length = SDL_HAPTIC_INFINITY;
				effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
				effect.constant.direction.dir[0] = 1;
				effect.constant.level = level;

				SDL_UpdateHapticEffect(hapticDevice, constantEffectId, &effect);
				prevConstantLevel = level;
			}
		}

		// ================================================================
		// SINE WAVE -- rumble strips + road texture + tire slip vibration
		// ================================================================
		if (sineEffectId >= 0 && updateForces)
		{
			float sineMag = 0.0f;
			uint16_t sinePeriod = 80; // Default: subtle 12.5 Hz road texture

			// Rumble strip / off-road surface vibration
			if (offRoad && speed > 0.01f)
			{
				sineMag += speedNorm * Settings::FFBRumbleStrip;
				sinePeriod = 30; // Aggressive 33 Hz rumble
			}

			// Road texture -- subtle vibration at speed (only on smooth road)
			if (speed > 0.1f && !offRoad)
			{
				sineMag += speedNorm * 0.15f * Settings::FFBRoadTexture;
			}

			// Tire slip vibration -- high lateral forces indicate loss of grip
			// Use smoothed lateral for consistent feel (same filter as steering weight)
			float lateralMag = std::abs(smoothedLateral);
			if (lateralMag > 10.0f && speed > 0.1f) // Lateral forces range ~0-30
			{
				float slipIntensity = std::clamp((lateralMag - 10.0f) / 20.0f, 0.0f, 1.0f);
				sineMag += slipIntensity * 0.5f * Settings::FFBTireSlip;
				sinePeriod = std::min(sinePeriod, uint16_t(40)); // 25 Hz buzz for slip
			}

			sineMag = std::clamp(sineMag, 0.0f, 1.0f);
			int16_t magnitude = int16_t(sineMag * 32767.0f);

			memset(&effect, 0, sizeof(effect));
			effect.type = SDL_HAPTIC_SINE;
			effect.periodic.length = SDL_HAPTIC_INFINITY;
			effect.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
			effect.periodic.direction.dir[0] = 1;
			effect.periodic.period = sinePeriod;
			effect.periodic.magnitude = magnitude;
			effect.periodic.offset = 0;
			effect.periodic.phase = 0;

			SDL_UpdateHapticEffect(hapticDevice, sineEffectId, &effect);
		}

		// FRICTION -- fixed coefficients, same as spring/damper (no runtime updates)
		// Coefficient set once in CreateEffects.

		// Diagnostic logging: speed window delta + force level, every 2 seconds
		static DWORD lastDiagTime = 0;
		DWORD now = GetTickCount();
		if (now - lastDiagTime >= 2000 && speedHistoryIdx > 6)
		{
			lastDiagTime = now;
			float oldSpd = speedHistory[(speedHistoryIdx - 6) % 8];
			float winDelta = oldSpd - speed;
			spdlog::info("FFB DIAG: spd={:.4f} prevSpd={:.4f} winDelta={:.4f} smoothLat={:.2f} constLvl={} flags8=0x{:X} crashTimer={}",
				speed, oldSpd, winDelta, smoothedLateral, (int)prevConstantLevel, stateFlags, crashImpulseTimer);
		}

		// Store previous frame state for next-frame edge detection
		prevGear = curGear;
		prevCollisionFlags = stateFlags;
		prevSpeed = speed;
	}

	void Shutdown()
	{
		Telemetry::Shutdown();

		if (!hapticDevice)
			return;

		spdlog::info("FFB: Shutting down...");
		SDL_StopHapticEffects(hapticDevice);

		if (springEffectId >= 0) SDL_DestroyHapticEffect(hapticDevice, springEffectId);
		if (damperEffectId >= 0) SDL_DestroyHapticEffect(hapticDevice, damperEffectId);
		if (constantEffectId >= 0) SDL_DestroyHapticEffect(hapticDevice, constantEffectId);
		if (sineEffectId >= 0) SDL_DestroyHapticEffect(hapticDevice, sineEffectId);
		if (frictionEffectId >= 0) SDL_DestroyHapticEffect(hapticDevice, frictionEffectId);

		springEffectId = damperEffectId = constantEffectId = sineEffectId = frictionEffectId = -1;

		SDL_CloseHaptic(hapticDevice);
		hapticDevice = nullptr;
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
		// Only install the inline hook here -- do NOT call SDL_Init.
		// SDL_Init creates threads which deadlocks under the Windows loader lock
		// that is held during DllMain/DLL_PROCESS_ATTACH.
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
