// DirectInput Force Feedback via DirectInput COM API
// Provides steering wheel force feedback using EVWORK_CAR telemetry data.
// Effects: steering weight (constant force), collision impact,
//          rumble strip, gear shift, road texture, tire slip.
// Uses IDirectInputEffect::SetParameters with DIEP_START for reliable
// real-time updates on all wheel drivers (SDL3 Haptic doesn't work with Moza/DD wheels).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <dinput.h>
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

// Forward declarations from hooks_inputremap.cpp
namespace DInputRemap
{
	IDirectInputDevice8A* GetPrimaryDevice();
	bool IsPrimaryInitialized();
}

// Forward declaration from Proxy.cpp
extern IDirectInput8A* g_RealDirectInput8;

namespace FFB
{
	// DirectInput FFB state
	static IDirectInputDevice8A* ffbDevice = nullptr;
	static IDirectInputEffect* constantForceEffect = nullptr;
	static bool ownsDevice = false;  // true if FFB created its own device handle
	static bool initialized = false;
	static bool initAttempted = false;

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
	static LONG prevConstantLevel = 0;  // DI range: ±10000

	// Gear shift timer (frames remaining)
	static int gearShiftTimer = 0;

	// Warmup counter: ramp force scaling from 0 to 1 over first N frames
	static int warmupFrames = 0;
	static const int WARMUP_THRESHOLD = 30; // ~0.5 sec at 60Hz

	// ---------- DirectInput FFB helpers ----------

	static bool CreateConstantForceEffect()
	{
		if (!ffbDevice) return false;

		// Mirror the test bench (FfbTestService.cs) exactly:
		// Single axis (X = steering), cartesian, infinite duration, gain from INI
		DWORD axes[1] = { DIJOFS_X };
		LONG directions[1] = { 0 };
		DICONSTANTFORCE cf = {};
		cf.lMagnitude = 0;

		DIEFFECT eff = {};
		eff.dwSize = sizeof(DIEFFECT);
		eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
		eff.dwDuration = INFINITE;
		eff.dwSamplePeriod = 0;
		eff.dwGain = (DWORD)(std::clamp(Settings::FFBGlobalStrength, 0.0f, 1.0f) * 10000.0f);
		eff.dwTriggerButton = DIEB_NOTRIGGER;
		eff.dwTriggerRepeatInterval = 0;
		eff.cAxes = 1;
		eff.rgdwAxes = axes;
		eff.rglDirection = directions;
		eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
		eff.lpvTypeSpecificParams = &cf;
		eff.dwStartDelay = 0;

		HRESULT hr = ffbDevice->CreateEffect(
			GUID_ConstantForce, &eff, &constantForceEffect, nullptr);

		if (FAILED(hr))
		{
			spdlog::error("FFB: CreateEffect(ConstantForce) failed (HRESULT 0x{:08X})", (unsigned)hr);
			return false;
		}

		spdlog::info("FFB: Constant force effect created (gain: {}%)",
			(int)(Settings::FFBGlobalStrength * 100.0f));
		return true;
	}

	static void SetConstantForce(LONG magnitude)
	{
		if (!ffbDevice) return;

		DICONSTANTFORCE cf;
		cf.lMagnitude = std::clamp(magnitude, (LONG)-10000, (LONG)10000);

		DIEFFECT eff = {};
		eff.dwSize = sizeof(DIEFFECT);
		eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
		eff.lpvTypeSpecificParams = &cf;

		HRESULT hr = E_FAIL;

		// Try existing effect first
		if (constantForceEffect)
		{
			hr = constantForceEffect->SetParameters(
				&eff, DIEP_TYPESPECIFICPARAMS | DIEP_START);
		}

		// If handle is invalid (E_HANDLE / 0x80070006), the device was re-acquired
		// by the remap code's Poll() error handling, which invalidates all effects.
		// Recreate the effect on the fly.
		if (hr == E_HANDLE || hr == DIERR_NOTDOWNLOADED || !constantForceEffect)
		{
			if (constantForceEffect)
			{
				constantForceEffect->Release();
				constantForceEffect = nullptr;
			}

			if (CreateConstantForceEffect())
			{
				// Set the magnitude on the freshly created effect
				hr = constantForceEffect->SetParameters(
					&eff, DIEP_TYPESPECIFICPARAMS | DIEP_START);
				spdlog::info("FFB: Recreated constant force effect after handle loss");
			}
			else
			{
				spdlog::error("FFB: Failed to recreate constant force effect");
				return;
			}
		}
		else if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED)
		{
			ffbDevice->Acquire();
			hr = constantForceEffect->SetParameters(
				&eff, DIEP_TYPESPECIFICPARAMS | DIEP_START);
		}

		prevConstantLevel = cf.lMagnitude;
	}

	// Deferred initialization -- called from Update() on first game tick.
	bool DeferredInit()
	{
		if (initAttempted)
			return initialized;
		initAttempted = true;

		spdlog::info("FFB: Starting deferred initialization (DirectInput)...");

		// Path A: Reuse the primary device from DInputRemap (preferred)
		// The remap code opens it in EXCLUSIVE mode when FFB is enabled.
		if (Settings::UseDirectInputRemap && DInputRemap::IsPrimaryInitialized())
		{
			ffbDevice = DInputRemap::GetPrimaryDevice();
			if (ffbDevice)
			{
				// Verify FFB support
				DIDEVCAPS caps = {};
				caps.dwSize = sizeof(DIDEVCAPS);
				ffbDevice->GetCapabilities(&caps);
				if (caps.dwFlags & DIDC_FORCEFEEDBACK)
				{
					ownsDevice = false;
					spdlog::info("FFB: Using shared device from DInputRemap (FFB supported)");
				}
				else
				{
					spdlog::warn("FFB: Shared device does not support FFB, trying standalone...");
					ffbDevice = nullptr;
				}
			}
			else
			{
				spdlog::warn("FFB: DInputRemap primary device not available, trying standalone...");
			}
		}

		// Path B: Open our own FFB device (standalone, no remap)
		if (!ffbDevice)
		{
			IDirectInput8A* di = g_RealDirectInput8 ? g_RealDirectInput8 : Game::DirectInput8();
			if (!di)
			{
				spdlog::error("FFB: No DirectInput8 interface available");
				return false;
			}

			// Enumerate FFB-capable devices
			struct EnumCtx { IDirectInput8A* di; IDirectInputDevice8A* bestDevice; };
			EnumCtx ctx = { di, nullptr };

			di->EnumDevices(DI8DEVCLASS_GAMECTRL,
				[](LPCDIDEVICEINSTANCEA inst, LPVOID pCtx) -> BOOL {
					auto* c = static_cast<EnumCtx*>(pCtx);
					IDirectInputDevice8A* dev = nullptr;
					if (SUCCEEDED(c->di->CreateDevice(inst->guidInstance, &dev, nullptr)))
					{
						DIDEVCAPS caps = {};
						caps.dwSize = sizeof(DIDEVCAPS);
						dev->GetCapabilities(&caps);
						if (caps.dwFlags & DIDC_FORCEFEEDBACK)
						{
							spdlog::info("FFB: Found FFB device: '{}'", inst->tszInstanceName);
							c->bestDevice = dev;
							return DIENUM_STOP;
						}
						dev->Release();
					}
					return DIENUM_CONTINUE;
				},
				&ctx, DIEDFL_FORCEFEEDBACK);

			if (!ctx.bestDevice)
			{
				spdlog::warn("FFB: No FFB-capable devices found");
				return false;
			}

			ffbDevice = ctx.bestDevice;
			ownsDevice = true;

			ffbDevice->SetDataFormat(&c_dfDIJoystick2);
			ffbDevice->SetCooperativeLevel(Game::GameHwnd(),
				DISCL_EXCLUSIVE | DISCL_BACKGROUND);
			ffbDevice->Acquire();
		}

		// Disable autocenter
		DIPROPDWORD dipdw = {};
		dipdw.diph.dwSize = sizeof(DIPROPDWORD);
		dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
		dipdw.diph.dwObj = 0;
		dipdw.diph.dwHow = DIPH_DEVICE;
		dipdw.dwData = FALSE; // DIPAUTOCENTER_OFF = 0
		ffbDevice->SetProperty(DIPROP_AUTOCENTER, &dipdw.diph);

		// Create the constant force effect
		if (!CreateConstantForceEffect())
		{
			spdlog::error("FFB: Failed to create constant force effect");
			if (ownsDevice)
			{
				ffbDevice->Unacquire();
				ffbDevice->Release();
			}
			ffbDevice = nullptr;
			return false;
		}

		initialized = true;
		spdlog::info("FFB: Initialization complete (DirectInput)");
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
		if (!initialized || !constantForceEffect)
			return;

		DWORD now = GetTickCount();
		DWORD elapsed = now - lastUpdateTick;

		// If Update() hasn't been called for 250ms and forces are non-zero, zero them
		if (elapsed > 250 && lastUpdateTick > 0 && prevConstantLevel != 0)
		{
			SetConstantForce(0);
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
			if (constantForceEffect && prevConstantLevel != 0)
			{
				SetConstantForce(0);
				smoothedLateral = 0.0f;
				crashImpulseTimer = 0;
			}
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

		if (constantForceEffect && updateForces)
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

				// Steering weight force. satForce is ±1.0 from SAT curve, speedNorm is 0-1.
				// With FFBSteeringWeight=1.0, a hard turn at top speed produces ~0.7 output
				// (before tanh), which is a strong but not overwhelming force.
				// Users adjust with FFBSteeringWeight slider + wheel software.
				float steeringWeight = satForce * speedNorm * Settings::FFBSteeringWeight;
				float maxSteeringForce = Settings::FFBSteeringWeight;
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
				float rumbleIntensity = speedNorm * Settings::FFBRumbleStrip * 0.08f;
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
				float slipRumble = slipWave * slipAmount * Settings::FFBTireSlip * 0.06f;
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
				float revForce = engineWave * engineIntensity * speedFade * 0.04f;
				totalForce += revForce;
			}
			else
			{
				enginePhase = 0.0f;
			}

			// Apply inversion if configured
			if (Settings::FFBInvertForce)
				totalForce = -totalForce;

			// Warmup ramp: scale force from 0→1 over first 0.5 sec of gameplay.
			// Prevents garbage telemetry on initial frames from spiking the wheel.
			totalForce *= warmupScale;

			// Soft saturation via tanh: preserves relative force differences
			// near the limit instead of hard-clipping to +/-1.0.
			// Forces below ~0.5 are nearly linear; above 0.8 they compress smoothly.
			totalForce = std::tanh(totalForce);

			// Convert to DirectInput range: ±10000 (matching test bench)
			LONG diMagnitude = (LONG)(totalForce * 10000.0f);

			// Slew-rate limiter: prevent micro-oscillations on DD wheels
			// by capping how fast the force can change between frames.
			// Max ~6% change per frame (~60 Hz = full sweep in ~280ms).
			// Crash impulses and gear shift kicks bypass the limiter.
			LONG slewDelta = diMagnitude - prevConstantLevel;
			constexpr LONG maxSlew = 600; // ~6% of 10000
			bool bypassSlew = (crashImpulseTimer > 80) || (gearShiftTimer > 0);
			if (std::abs(slewDelta) > maxSlew && !bypassSlew)
				diMagnitude = prevConstantLevel + ((slewDelta > 0) ? maxSlew : -maxSlew);

			// Deadband: skip updating if the level barely changed.
			LONG delta = std::abs(diMagnitude - prevConstantLevel);
			if (delta > 15 || crashImpulseTimer > 80)
			{
				SetConstantForce(diMagnitude);
			}
		}

		// Diagnostic logging: every 2 seconds
		{
			static DWORD lastDiagTime = 0;
			DWORD now = GetTickCount();
			if (now - lastDiagTime >= 2000)
			{
				lastDiagTime = now;
				spdlog::info("FFB DIAG: spd={:.3f} lat={:.2f} smoothLat={:.2f} steer={:.3f} constLvl={} warmup={}/{}",
					speed, lateralForce1 + lateralForce2, smoothedLateral, steeringAngle,
					(int)prevConstantLevel, warmupFrames, WARMUP_THRESHOLD);
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

		if (!constantForceEffect && !ffbDevice)
			return;

		spdlog::info("FFB: Shutting down...");

		__try
		{
			if (constantForceEffect)
			{
				// Zero force so wheel doesn't stay stuck
				SetConstantForce(0);
				constantForceEffect->Stop();
				constantForceEffect->Release();
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			spdlog::warn("FFB: Exception releasing effect (0x{:X})", GetExceptionCode());
		}
		constantForceEffect = nullptr;

		__try
		{
			if (ffbDevice && ownsDevice)
			{
				ffbDevice->Unacquire();
				ffbDevice->Release();
			}
			// If !ownsDevice, the remap code owns the device lifetime
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			spdlog::warn("FFB: Exception releasing device (0x{:X})", GetExceptionCode());
		}

		ffbDevice = nullptr;
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
