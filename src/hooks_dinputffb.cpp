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

// External vibration data from hooks_forcefeedback.cpp
extern float VibrationLeftMotor;
extern float VibrationRightMotor;

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

	// Previous frame state for edge detection
	static uint32_t prevGear = 0;
	static uint32_t prevCollisionFlags = 0;
	static float prevSpeed = 0.0f;

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

		// 1. Center Spring -- self-centering force, scales with speed
		if (features & SDL_HAPTIC_SPRING)
		{
			memset(&effect, 0, sizeof(effect));
			effect.type = SDL_HAPTIC_SPRING;
			effect.condition.length = SDL_HAPTIC_INFINITY;
			effect.condition.direction.type = SDL_HAPTIC_CARTESIAN;
			effect.condition.direction.dir[0] = 1;
			effect.condition.right_sat[0] = 0xFFFF;
			effect.condition.left_sat[0] = 0xFFFF;
			int16_t coeff = int16_t(std::clamp(int(0x5000 * Settings::FFBSpringStrength), 0, 0x7FFF));
			effect.condition.right_coeff[0] = coeff;
			effect.condition.left_coeff[0] = coeff;
			effect.condition.deadband[0] = 0;
			effect.condition.center[0] = 0;

			springEffectId = SDL_CreateHapticEffect(hapticDevice, &effect);
			if (springEffectId >= 0)
			{
				SDL_RunHapticEffect(hapticDevice, springEffectId, SDL_HAPTIC_INFINITY);
				spdlog::info("FFB: Spring effect created (id: {})", springEffectId);
			}
			else
				spdlog::warn("FFB: Failed to create spring: {}", SDL_GetError());
		}

		// 2. Damper -- resistance to rapid steering changes
		if (features & SDL_HAPTIC_DAMPER)
		{
			memset(&effect, 0, sizeof(effect));
			effect.type = SDL_HAPTIC_DAMPER;
			effect.condition.length = SDL_HAPTIC_INFINITY;
			effect.condition.direction.type = SDL_HAPTIC_CARTESIAN;
			effect.condition.direction.dir[0] = 1;
			effect.condition.right_sat[0] = 0xFFFF;
			effect.condition.left_sat[0] = 0xFFFF;
			int16_t coeff = int16_t(std::clamp(int(0x3000 * Settings::FFBDamperStrength), 0, 0x7FFF));
			effect.condition.right_coeff[0] = coeff;
			effect.condition.left_coeff[0] = coeff;

			damperEffectId = SDL_CreateHapticEffect(hapticDevice, &effect);
			if (damperEffectId >= 0)
			{
				SDL_RunHapticEffect(hapticDevice, damperEffectId, SDL_HAPTIC_INFINITY);
				spdlog::info("FFB: Damper effect created (id: {})", damperEffectId);
			}
			else
				spdlog::warn("FFB: Failed to create damper: {}", SDL_GetError());
		}

		// 3. Constant force -- steering weight, collision jolts, gear shift kicks
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

		// 4. Sine wave -- rumble strips, road texture vibration, tire slip buzz
		if (features & SDL_HAPTIC_SINE)
		{
			memset(&effect, 0, sizeof(effect));
			effect.type = SDL_HAPTIC_SINE;
			effect.periodic.length = SDL_HAPTIC_INFINITY;
			effect.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
			effect.periodic.direction.dir[0] = 1;
			effect.periodic.period = 50; // 20 Hz default
			effect.periodic.magnitude = 0;
			effect.periodic.offset = 0;
			effect.periodic.phase = 0;

			sineEffectId = SDL_CreateHapticEffect(hapticDevice, &effect);
			if (sineEffectId >= 0)
			{
				SDL_RunHapticEffect(hapticDevice, sineEffectId, SDL_HAPTIC_INFINITY);
				spdlog::info("FFB: Sine effect created (id: {})", sineEffectId);
			}
			else
				spdlog::warn("FFB: Failed to create sine: {}", SDL_GetError());
		}

		// 5. Friction -- tire grip simulation
		if (features & SDL_HAPTIC_FRICTION)
		{
			memset(&effect, 0, sizeof(effect));
			effect.type = SDL_HAPTIC_FRICTION;
			effect.condition.length = SDL_HAPTIC_INFINITY;
			effect.condition.direction.type = SDL_HAPTIC_CARTESIAN;
			effect.condition.direction.dir[0] = 1;
			effect.condition.right_sat[0] = 0xFFFF;
			effect.condition.left_sat[0] = 0xFFFF;
			int16_t coeff = int16_t(std::clamp(int(0x4000 * Settings::FFBTireSlip), 0, 0x7FFF));
			effect.condition.right_coeff[0] = coeff;
			effect.condition.left_coeff[0] = coeff;

			frictionEffectId = SDL_CreateHapticEffect(hapticDevice, &effect);
			if (frictionEffectId >= 0)
			{
				SDL_RunHapticEffect(hapticDevice, frictionEffectId, SDL_HAPTIC_INFINITY);
				spdlog::info("FFB: Friction effect created (id: {})", frictionEffectId);
			}
			else
				spdlog::warn("FFB: Failed to create friction: {}", SDL_GetError());
		}
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

		// Set global gain (combines user strength * torque scale)
		if (features & SDL_HAPTIC_GAIN)
		{
			int gain = int(Settings::FFBGlobalStrength * wheelTorqueScale * 100.0f);
			gain = std::clamp(gain, 0, 100);
			SDL_SetHapticGain(hapticDevice, gain);
			spdlog::info("FFB: Global gain set to {}%", gain);
		}

		CreateEffects(features);

		initialized = true;
		spdlog::info("FFB: Initialization complete");
		return true;
	}

	void Update(EVWORK_CAR* car)
	{
		if (!car)
			return;

		// Lazy initialization: SDL_Init must happen AFTER DllMain returns
		// (it creates threads, which deadlock under the Windows loader lock)
		if (!initialized)
		{
			if (!DeferredInit())
				return;
		}

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
		// CENTER SPRING -- speed-dependent self-centering
		// ================================================================
		if (springEffectId >= 0)
		{
			// Soft at standstill, progressively stiffer with speed
			float springBase = 0.15f + 0.85f * speedNorm;
			int springCoeff = int(0x7FFF * springBase * Settings::FFBSpringStrength);
			springCoeff = std::clamp(springCoeff, 0, 0x7FFF);

			memset(&effect, 0, sizeof(effect));
			effect.type = SDL_HAPTIC_SPRING;
			effect.condition.length = SDL_HAPTIC_INFINITY;
			effect.condition.direction.type = SDL_HAPTIC_CARTESIAN;
			effect.condition.direction.dir[0] = 1;
			effect.condition.right_sat[0] = 0xFFFF;
			effect.condition.left_sat[0] = 0xFFFF;
			effect.condition.right_coeff[0] = int16_t(springCoeff);
			effect.condition.left_coeff[0] = int16_t(springCoeff);
			effect.condition.deadband[0] = 0;
			effect.condition.center[0] = 0;

			SDL_UpdateHapticEffect(hapticDevice, springEffectId, &effect);
		}

		// ================================================================
		// DAMPER -- speed-dependent resistance to rapid steering
		// ================================================================
		if (damperEffectId >= 0)
		{
			float damperBase = 0.2f + 0.8f * speedNorm;
			int damperCoeff = int(0x7FFF * damperBase * Settings::FFBDamperStrength);
			damperCoeff = std::clamp(damperCoeff, 0, 0x7FFF);

			memset(&effect, 0, sizeof(effect));
			effect.type = SDL_HAPTIC_DAMPER;
			effect.condition.length = SDL_HAPTIC_INFINITY;
			effect.condition.direction.type = SDL_HAPTIC_CARTESIAN;
			effect.condition.direction.dir[0] = 1;
			effect.condition.right_sat[0] = 0xFFFF;
			effect.condition.left_sat[0] = 0xFFFF;
			effect.condition.right_coeff[0] = int16_t(damperCoeff);
			effect.condition.left_coeff[0] = int16_t(damperCoeff);

			SDL_UpdateHapticEffect(hapticDevice, damperEffectId, &effect);
		}

		// ================================================================
		// CONSTANT FORCE -- steering weight + collision + gear shift
		// ================================================================
		if (constantEffectId >= 0)
		{
			float totalForce = 0.0f;

			// --- Steering weight from lateral forces ---
			// The lateral force fields indicate cornering load.
			// Positive = force pushing one way, negative = the other.
			// We combine them and scale by speed for a natural cornering feel.
			float lateralCombined = (lateralForce1 + lateralForce2);
			float steeringWeight = lateralCombined * speedNorm * Settings::FFBSteeringWeight;
			totalForce += steeringWeight;

			// --- Wall/collision impact ---
			bool collisionActive = (stateFlags & 0x40) != 0;
			bool wasColliding = (prevCollisionFlags & 0x40) != 0;

			if (collisionActive && !wasColliding)
			{
				// New collision onset -- sharp jolt
				float impactDir = (impactForce >= 0.0f) ? 1.0f : -1.0f;
				float impactMag = std::clamp(std::abs(impactForce) * 2.0f, 0.0f, 1.0f);
				totalForce += impactDir * impactMag * Settings::FFBWallImpact;
			}
			else if (collisionActive)
			{
				// Sustained collision -- lighter continuous force
				totalForce += impactForce * 0.5f * Settings::FFBWallImpact;
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

			// Apply inversion if configured
			if (Settings::FFBInvertForce)
				totalForce = -totalForce;

			totalForce = std::clamp(totalForce, -1.0f, 1.0f);
			int16_t level = int16_t(totalForce * 32767.0f);

			memset(&effect, 0, sizeof(effect));
			effect.type = SDL_HAPTIC_CONSTANT;
			effect.constant.length = SDL_HAPTIC_INFINITY;
			effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
			effect.constant.direction.dir[0] = 1;
			effect.constant.level = level;

			SDL_UpdateHapticEffect(hapticDevice, constantEffectId, &effect);
		}

		// ================================================================
		// SINE WAVE -- rumble strips + road texture + tire slip vibration
		// ================================================================
		if (sineEffectId >= 0)
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
			float lateralMag = std::abs(lateralForce1) + std::abs(lateralForce2);
			if (lateralMag > 0.3f && speed > 0.1f)
			{
				float slipIntensity = std::clamp((lateralMag - 0.3f) * 2.0f, 0.0f, 1.0f);
				sineMag += slipIntensity * 0.5f * Settings::FFBTireSlip;
				sinePeriod = std::min(sinePeriod, uint16_t(25)); // Fast 40 Hz buzz for slip
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

		// ================================================================
		// FRICTION -- tire grip simulation (reduces when sliding)
		// ================================================================
		if (frictionEffectId >= 0)
		{
			float lateralMag = std::abs(lateralForce1) + std::abs(lateralForce2);
			// High lateral force = low grip = reduced friction FFB
			float gripFactor = 1.0f - std::clamp(lateralMag * 2.0f, 0.0f, 0.8f);
			int frictionCoeff = int(0x6000 * gripFactor * speedNorm * Settings::FFBTireSlip);
			frictionCoeff = std::clamp(frictionCoeff, 0, 0x7FFF);

			memset(&effect, 0, sizeof(effect));
			effect.type = SDL_HAPTIC_FRICTION;
			effect.condition.length = SDL_HAPTIC_INFINITY;
			effect.condition.direction.type = SDL_HAPTIC_CARTESIAN;
			effect.condition.direction.dir[0] = 1;
			effect.condition.right_sat[0] = 0xFFFF;
			effect.condition.left_sat[0] = 0xFFFF;
			effect.condition.right_coeff[0] = int16_t(frictionCoeff);
			effect.condition.left_coeff[0] = int16_t(frictionCoeff);

			SDL_UpdateHapticEffect(hapticDevice, frictionEffectId, &effect);
		}

		// Store previous frame state for next-frame edge detection
		prevGear = curGear;
		prevCollisionFlags = stateFlags;
		prevSpeed = speed;
	}

	void Shutdown()
	{
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
		return Settings::DirectInputFFB;
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
