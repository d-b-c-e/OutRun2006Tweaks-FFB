// OutRun 2006 Telemetry Shared Memory
// Exposes per-frame EVWORK_CAR telemetry via named shared memory for external tools.
// Primary consumer: SimHub plugin for bass shaker / ShakeIt integration.
//
// Shared memory name: "OutRun2006Telemetry" (configurable via INI)
// Layout: OutRun2006TelemetryData struct, written every frame by the FFB DLL.

#pragma once

#include <cstdint>

#pragma pack(push, 1)

struct OutRun2006TelemetryData
{
	// Header
	uint32_t version;          // Struct version (1 = initial release)
	uint32_t packetId;         // Incremented each frame (rollover OK)

	// Driving state
	float speed;               // Normalized speed (0.0 - ~1.0+), from field_1C4
	float steeringAngle;       // Steering position / yaw rate, from field_1D0
	float lateralG1;           // Lateral force component 1, from field_264
	float lateralG2;           // Lateral force component 2, from field_268
	float impactForce;         // Contact/impact force magnitude, from field_178

	// Drivetrain
	uint32_t gear;             // Current gear number, from cur_gear_208
	uint32_t prevGear;         // Previous gear (per game logic), from dword1D8

	// Flags
	uint32_t stateFlags;       // State/collision bits, from field_8
	uint32_t carFlags;         // Car flags, from flags_4

	// Surface contact (per-wheel)
	uint32_t surfaceType[4];   // Tire contact flags, from water_flag_24C[0..3]
	                           // 0=airborne, 1=asphalt, 2=sand/gravel, 4=grass, etc.

	// XInput vibration (game's built-in rumble output)
	float vibrationLeft;       // Low-frequency motor (0.0 - 1.0)
	float vibrationRight;      // High-frequency motor (0.0 - 1.0)

	// Game state
	uint32_t gameMode;         // Current game mode/state enum
	uint8_t isInGameplay;      // 1 if actively racing, 0 if in menus/results

	// Padding to align to 8 bytes
	uint8_t _pad[3];
};

#pragma pack(pop)

static_assert(sizeof(OutRun2006TelemetryData) == 76, "Telemetry struct size mismatch");

// Default shared memory name
constexpr const char* TELEMETRY_SHARED_MEM_NAME = "OutRun2006Telemetry";
constexpr uint32_t TELEMETRY_VERSION = 1;
