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

// Surface-type -> roughness coefficient LUT, decompiled from the game's own
// Xbox vibration code (defined in hooks_forcefeedback.cpp). Returns 0.0 for
// asphalt up to 0.9 for rough surfaces; sets *a3 |= 1 on water surfaces.
extern double __cdecl sub_1149C0(unsigned int surfaceMask, int loadColiType, DWORD* waterFlag);

// Forza Motorsport "Dash" UDP packet (311 bytes, little-endian)
// Emitted to localhost:8000 for Moza Pit House wheel display
#pragma pack(push, 1)
struct ForzaDashPacket
{
	// SLED section (0-231)
	int32_t  IsRaceOn;                    //   0
	uint32_t TimestampMS;                 //   4
	float    EngineMaxRpm;                //   8
	float    EngineIdleRpm;               //  12
	float    CurrentEngineRpm;            //  16
	float    AccelerationX;               //  20
	float    AccelerationY;               //  24
	float    AccelerationZ;               //  28
	float    VelocityX;                   //  32
	float    VelocityY;                   //  36
	float    VelocityZ;                   //  40
	float    AngularVelocityX;            //  44
	float    AngularVelocityY;            //  48
	float    AngularVelocityZ;            //  52
	float    Yaw;                         //  56
	float    Pitch;                       //  60
	float    Roll;                        //  64
	float    NormSuspTravelFL;            //  68
	float    NormSuspTravelFR;            //  72
	float    NormSuspTravelRL;            //  76
	float    NormSuspTravelRR;            //  80
	float    TireSlipRatioFL;             //  84
	float    TireSlipRatioFR;             //  88
	float    TireSlipRatioRL;             //  92
	float    TireSlipRatioRR;             //  96
	float    WheelRotSpeedFL;             // 100
	float    WheelRotSpeedFR;             // 104
	float    WheelRotSpeedRL;             // 108
	float    WheelRotSpeedRR;             // 112
	int32_t  WheelOnRumbleFL;             // 116
	int32_t  WheelOnRumbleFR;             // 120
	int32_t  WheelOnRumbleRL;             // 124
	int32_t  WheelOnRumbleRR;             // 128
	float    WheelPuddleFL;              // 132
	float    WheelPuddleFR;              // 136
	float    WheelPuddleRL;              // 140
	float    WheelPuddleRR;              // 144
	float    SurfaceRumbleFL;             // 148
	float    SurfaceRumbleFR;             // 152
	float    SurfaceRumbleRL;             // 156
	float    SurfaceRumbleRR;             // 160
	float    TireSlipAngleFL;             // 164
	float    TireSlipAngleFR;             // 168
	float    TireSlipAngleRL;             // 172
	float    TireSlipAngleRR;             // 176
	float    TireCombinedSlipFL;          // 180
	float    TireCombinedSlipFR;          // 184
	float    TireCombinedSlipRL;          // 188
	float    TireCombinedSlipRR;          // 192
	float    SuspTravelMetersFL;          // 196
	float    SuspTravelMetersFR;          // 200
	float    SuspTravelMetersRL;          // 204
	float    SuspTravelMetersRR;          // 208
	int32_t  CarOrdinal;                  // 212
	int32_t  CarClass;                    // 216
	int32_t  CarPerformanceIndex;         // 220
	int32_t  DrivetrainType;              // 224
	int32_t  NumCylinders;                // 228
	// DASH section (232-310)
	float    PositionX;                   // 232
	float    PositionY;                   // 236
	float    PositionZ;                   // 240
	float    Speed;                       // 244 (m/s)
	float    Power;                       // 248
	float    Torque;                      // 252
	float    TireTempFL;                  // 256
	float    TireTempFR;                  // 260
	float    TireTempRL;                  // 264
	float    TireTempRR;                  // 268
	float    Boost;                       // 272
	float    Fuel;                        // 276
	float    DistanceTraveled;            // 280
	float    BestLap;                     // 284
	float    LastLap;                     // 288
	float    CurrentLap;                  // 292
	float    CurrentRaceTime;             // 296
	uint16_t LapNumber;                   // 300
	uint8_t  RacePosition;               // 302
	uint8_t  Accel;                       // 303 (throttle 0-255)
	uint8_t  Brake;                       // 304 (0-255)
	uint8_t  Clutch;                      // 305
	uint8_t  HandBrake;                   // 306
	uint8_t  Gear;                        // 307 (0=R, 1-10=fwd)
	int8_t   Steer;                       // 308 (-127..127)
	int8_t   NormDrivingLine;             // 309
	int8_t   NormAIBrakeDiff;             // 310
};
#pragma pack(pop)
static_assert(sizeof(ForzaDashPacket) == 311, "ForzaDashPacket must be 311 bytes");

// Telemetry: shared memory (SimHub) + Forza UDP (Moza Pit House display)
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
			ForzaDashPacket pkt = {};

			pkt.IsRaceOn = inGameplay ? 1 : 0;
			pkt.TimestampMS = GetTickCount();

			// Speed: convert normalized (0-2+) to m/s
			float speedMps = car->field_1C4 * MaxSpeedMps;
			pkt.Speed = speedMps;

			// Gear
			uint32_t gear = car->cur_gear_208;
			pkt.Gear = (uint8_t)std::clamp(gear, 0u, 10u);

			// Synthesize RPM from speed and gear
			// OutRun doesn't expose RPM, so we calculate a plausible value
			float gearRatio = (gear > 0 && gear < 7) ? GearRatios[gear] : 1.0f;
			float speedNorm = std::clamp(car->field_1C4 / 2.0f, 0.0f, 1.0f);
			float rpm = IdleRPM + speedNorm * gearRatio * (MaxRPM - IdleRPM);
			rpm = std::clamp(rpm, IdleRPM, MaxRPM);

			pkt.CurrentEngineRpm = rpm;
			pkt.EngineMaxRpm = MaxRPM;
			pkt.EngineIdleRpm = IdleRPM;

			// Steering angle mapped to Forza's -127..127 range
			pkt.Steer = (int8_t)std::clamp((int)(car->field_1D0 * 127.0f), -127, 127);

			// Lateral acceleration (for display)
			pkt.AccelerationX = car->field_264 + car->field_268;

			// Surface rumble (for display indicators)
			bool offRoad = car->water_flag_24C[0] > 1 || car->water_flag_24C[1] > 1 ||
			               car->water_flag_24C[2] > 1 || car->water_flag_24C[3] > 1;
			if (offRoad)
			{
				pkt.SurfaceRumbleFL = pkt.SurfaceRumbleFR = 1.0f;
				pkt.SurfaceRumbleRL = pkt.SurfaceRumbleRR = 1.0f;
			}

			sendto(udpSocket, (const char*)&pkt, sizeof(pkt), 0,
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

// Forward declarations from hooks_inputremap.cpp
namespace DInputRemap
{
	IDirectInputDevice8A* GetPrimaryDevice();
	bool IsPrimaryInitialized();
	bool GetPrimaryDeviceGuid(GUID* out);
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

	// Hardware periodic effects (road texture / tire slip). 25-40 Hz content
	// synthesized through 60 Hz constant-force updates loses ~26% to zero-order-
	// hold rolloff, more to wheelbase driver smoothing, and 30-60% to tanh
	// compression when riding on steering load. Hardware periodics render inside
	// the wheelbase at full fidelity regardless of our update rate.
	static IDirectInputEffect* roadTextureEffect = nullptr; // GUID_Sine, surface LUT driven
	static IDirectInputEffect* tireSlipEffect = nullptr;    // GUID_Sine, drift chatter / engine idle
	static bool periodicsActive = false;
	static bool periodicsProbed = false;
	static int periodicStrategy = -1; // -1=untested, 0=SetParameters w/o DIEP_START (ideal), 1=with DIEP_START
	struct PeriodicState { DWORD lastMag = 0; DWORD lastPeriod = 0; };
	static PeriodicState roadTextureState;
	static PeriodicState tireSlipState;

	// Panic flag: once set (process exit path), no further DI output is issued
	static volatile bool panicStopped = false;

	// Post-recreation ramp-in + recreation backoff (anti-jerk):
	// after an effect is recreated, fade force back in over ~250ms instead of
	// stepping straight to the requested magnitude; if recreation fails, hold
	// off retries for 500ms instead of thrashing the driver at 60 Hz.
	static int recreateRampFrames = 0;
	static const int RECREATE_RAMP_FRAMES = 15;
	static DWORD recreateHoldoffUntil = 0;

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

	// ---------- Periodic effect helpers (hardware-rendered vibration) ----------

	static IDirectInputEffect* CreatePeriodicEffect(REFGUID guidType, const char* name)
	{
		if (!ffbDevice) return nullptr;

		DWORD axes[1] = { DIJOFS_X };
		LONG directions[1] = { 0 };
		DIPERIODIC pf = {};
		pf.dwMagnitude = 0;
		pf.lOffset = 0;
		pf.dwPhase = 0;
		pf.dwPeriod = 1000000 / 25; // 25 Hz initial; retuned by envelope updates

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
		eff.cbTypeSpecificParams = sizeof(DIPERIODIC);
		eff.lpvTypeSpecificParams = &pf;
		eff.dwStartDelay = 0;

		IDirectInputEffect* fx = nullptr;
		HRESULT hr = ffbDevice->CreateEffect(guidType, &eff, &fx, nullptr);
		if (FAILED(hr))
		{
			spdlog::warn("FFB: CreateEffect({}) failed (HRESULT 0x{:08X})", name, (unsigned)hr);
			return nullptr;
		}

		// Start silently (magnitude 0) so envelope updates only need SetParameters
		hr = fx->Start(1, 0);
		if (FAILED(hr))
			spdlog::warn("FFB: {} Start failed (HRESULT 0x{:08X})", name, (unsigned)hr);

		spdlog::info("FFB: {} periodic effect created", name);
		return fx;
	}

	static void CreatePeriodicEffects()
	{
		if (!Settings::FFBUsePeriodicEffects || !ffbDevice)
		{
			periodicsActive = false;
			return;
		}

		// One-time probe: log which periodic effect types the driver exposes
		if (!periodicsProbed)
		{
			periodicsProbed = true;
			ffbDevice->EnumEffects([](LPCDIEFFECTINFOA info, LPVOID) -> BOOL {
				spdlog::info("FFB: Driver periodic effect available: '{}'", info->tszName);
				return DIENUM_CONTINUE;
			}, nullptr, DIEFT_PERIODIC);
		}

		if (!roadTextureEffect)
		{
			roadTextureEffect = CreatePeriodicEffect(GUID_Sine, "RoadTexture(Sine)");
			roadTextureState = {};
		}
		if (!tireSlipEffect)
		{
			tireSlipEffect = CreatePeriodicEffect(GUID_Sine, "TireSlip(Sine)");
			tireSlipState = {};
		}

		periodicsActive = (roadTextureEffect != nullptr && tireSlipEffect != nullptr);
		if (!periodicsActive)
			spdlog::warn("FFB: Periodic effects unavailable -- using constant-force vibration fallback (15 Hz cap, post-compressor injection)");
	}

	// Envelope update for a hardware periodic effect. Caller rate-limits to
	// ~15 Hz; additionally skips the DI call unless magnitude moved >5%,
	// period moved >10%, or the effect must be silenced.
	static void UpdatePeriodicEffect(IDirectInputEffect*& fx, PeriodicState& st, float magnitude01, float freqHz)
	{
		if (!fx || panicStopped) return;

		DWORD mag = (DWORD)(std::clamp(magnitude01, 0.0f, 1.0f) * 10000.0f);
		freqHz = std::clamp(freqHz, 1.0f, 100.0f);
		DWORD period = (DWORD)(1000000.0f / freqHz);

		bool silence = (mag == 0 && st.lastMag != 0);
		bool magChanged = std::abs((long)mag - (long)st.lastMag) > 500;                            // >5%
		bool periodChanged = (st.lastPeriod != 0) &&
			(std::abs((long)period - (long)st.lastPeriod) * 10 > (long)st.lastPeriod);             // >10%
		if (!magChanged && !periodChanged && !silence)
			return;

		DIPERIODIC pf = {};
		pf.dwMagnitude = mag;
		pf.dwPeriod = period;

		DIEFFECT eff = {};
		eff.dwSize = sizeof(DIEFFECT);
		eff.cbTypeSpecificParams = sizeof(DIPERIODIC);
		eff.lpvTypeSpecificParams = &pf;

		// Update-strategy matrix (driver behavior differs per wheelbase):
		// prefer SetParameters WITHOUT DIEP_START (no phase reset); fall back
		// to DIEP_START if the driver rejects parameter-only updates.
		DWORD flags = DIEP_TYPESPECIFICPARAMS | ((periodicStrategy == 1) ? DIEP_START : 0);
		HRESULT hr = fx->SetParameters(&eff, flags);

		if (periodicStrategy == -1)
		{
			if (SUCCEEDED(hr))
			{
				periodicStrategy = 0;
				spdlog::info("FFB: Periodic update strategy: SetParameters without DIEP_START");
			}
			else
			{
				hr = fx->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS | DIEP_START);
				if (SUCCEEDED(hr))
				{
					periodicStrategy = 1;
					spdlog::info("FFB: Periodic update strategy: SetParameters with DIEP_START");
				}
			}
		}

		if (hr == E_HANDLE || hr == DIERR_NOTDOWNLOADED)
		{
			// Effect died (device re-acquired) -- recreated later, rate-limited
			spdlog::warn("FFB: Periodic effect handle lost (HRESULT 0x{:08X}), scheduling recreation", (unsigned)hr);
			fx->Release();
			fx = nullptr;
			periodicsActive = false;
			recreateHoldoffUntil = GetTickCount() + 500;
			return;
		}
		else if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED)
		{
			ffbDevice->Acquire();
			hr = fx->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS | DIEP_START);
		}

		if (SUCCEEDED(hr))
		{
			st.lastMag = mag;
			st.lastPeriod = period;
		}
	}

	static void SetConstantForce(LONG magnitude)
	{
		if (!ffbDevice || panicStopped) return;

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
		// somewhere, which invalidates all downloaded effects. Recreate on the fly.
		if (hr == E_HANDLE || hr == DIERR_NOTDOWNLOADED || !constantForceEffect)
		{
			if (constantForceEffect)
			{
				constantForceEffect->Release();
				constantForceEffect = nullptr;
			}

			// Back off: retrying CreateEffect at 60 Hz thrashes the driver
			// during focus transitions
			DWORD now = GetTickCount();
			if (now < recreateHoldoffUntil)
				return;

			if (CreateConstantForceEffect())
			{
				// Anti-jerk: do NOT jump to the requested magnitude. The wheel
				// just went torque -> 0 when the old effect died; stepping
				// straight back to full force is the felt "random jerk"
				// (and the slew limiter can't help -- prevConstantLevel never
				// saw the 0). Restart at zero and let the ramp-in fade the
				// force back over ~250ms.
				DICONSTANTFORCE zeroCf = {};
				DIEFFECT zeroEff = {};
				zeroEff.dwSize = sizeof(DIEFFECT);
				zeroEff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
				zeroEff.lpvTypeSpecificParams = &zeroCf;
				constantForceEffect->SetParameters(&zeroEff, DIEP_TYPESPECIFICPARAMS | DIEP_START);
				recreateRampFrames = RECREATE_RAMP_FRAMES;
				cf.lMagnitude = 0;
				spdlog::info("FFB: Recreated constant force effect after handle loss (ramping in over {} frames)", RECREATE_RAMP_FRAMES);
			}
			else
			{
				recreateHoldoffUntil = now + 500;
				spdlog::error("FFB: Failed to recreate constant force effect (retrying in 500ms)");
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

	// ---------- Exit-path guards (fixes exit stuck-force) ----------

	// Emergency zero-torque for process exit. Non-allocating, never recreates
	// effects, logs every HRESULT. Must run while the game window still exists:
	// by DLL_PROCESS_DETACH the window is gone, the exclusive device has been
	// force-unacquired by the OS, and none of these calls can reach the wheel --
	// which is exactly how the "stuck force after Alt+F4" bug happened.
	void PanicStop()
	{
		if (panicStopped)
			return;
		panicStopped = true; // stop Update()/watchdog from issuing further DI calls

		if (!ffbDevice)
			return;

		spdlog::info("FFB: PanicStop -- zeroing forces before window teardown");

		HRESULT hr;
		if (constantForceEffect)
		{
			DICONSTANTFORCE cf = {};
			DIEFFECT eff = {};
			eff.dwSize = sizeof(DIEFFECT);
			eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
			eff.lpvTypeSpecificParams = &cf;
			hr = constantForceEffect->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS | DIEP_START);
			spdlog::info("FFB: PanicStop constant-force zero => 0x{:08X}", (unsigned)hr);
			hr = constantForceEffect->Stop();
			spdlog::info("FFB: PanicStop constant-force Stop => 0x{:08X}", (unsigned)hr);
		}
		if (roadTextureEffect)
		{
			hr = roadTextureEffect->Stop();
			spdlog::info("FFB: PanicStop road-texture Stop => 0x{:08X}", (unsigned)hr);
		}
		if (tireSlipEffect)
		{
			hr = tireSlipEffect->Stop();
			spdlog::info("FFB: PanicStop tire-slip Stop => 0x{:08X}", (unsigned)hr);
		}

		hr = ffbDevice->SendForceFeedbackCommand(DISFFC_STOPALL);
		spdlog::info("FFB: PanicStop STOPALL => 0x{:08X}", (unsigned)hr);
		hr = ffbDevice->SendForceFeedbackCommand(DISFFC_SETACTUATORSOFF);
		spdlog::info("FFB: PanicStop SETACTUATORSOFF => 0x{:08X}", (unsigned)hr);
		hr = ffbDevice->SendForceFeedbackCommand(DISFFC_RESET);
		spdlog::info("FFB: PanicStop RESET => 0x{:08X}", (unsigned)hr);
		// Driver-side session close -> wheelbase releases any held torque
		hr = ffbDevice->Unacquire();
		spdlog::info("FFB: PanicStop Unacquire => 0x{:08X}", (unsigned)hr);
	}

	// Zero all force output without tearing anything down (Alt-Tab, menus, watchdog)
	void ZeroAllForces()
	{
		if (!initialized || panicStopped)
			return;
		if (constantForceEffect && prevConstantLevel != 0)
			SetConstantForce(0);
		prevStructLevel = 0;
		if (periodicsActive)
		{
			UpdatePeriodicEffect(roadTextureEffect, roadTextureState, 0.0f, 25.0f);
			UpdatePeriodicEffect(tireSlipEffect, tireSlipState, 0.0f, 40.0f);
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

	// Open a dedicated EXCLUSIVE|BACKGROUND device handle for FFB output.
	// Kept separate from the remap layer's NONEXCLUSIVE polling handle so
	// poll-side Acquire() churn can never invalidate our downloaded effects.
	static IDirectInputDevice8A* OpenFfbDevice(const GUID& guid, IDirectInput8A* di)
	{
		IDirectInputDevice8A* dev = nullptr;
		HRESULT hr = di->CreateDevice(guid, &dev, nullptr);
		if (FAILED(hr))
		{
			spdlog::error("FFB: CreateDevice for dedicated FFB handle failed (HRESULT 0x{:08X})", (unsigned)hr);
			return nullptr;
		}

		dev->SetDataFormat(&c_dfDIJoystick2);
		hr = dev->SetCooperativeLevel(Game::GameHwnd(), DISCL_EXCLUSIVE | DISCL_BACKGROUND);
		if (FAILED(hr))
			spdlog::warn("FFB: SetCooperativeLevel(EXCLUSIVE|BACKGROUND) failed (HRESULT 0x{:08X})", (unsigned)hr);
		hr = dev->Acquire();
		if (FAILED(hr))
			spdlog::warn("FFB: Initial Acquire failed (HRESULT 0x{:08X}), will retry on first force", (unsigned)hr);

		return dev;
	}

	// Deferred initialization -- called from Update() on first game tick.
	bool DeferredInit()
	{
		if (initAttempted)
			return initialized;
		initAttempted = true;

		spdlog::info("FFB: Starting deferred initialization (DirectInput)...");

		IDirectInput8A* di = g_RealDirectInput8 ? g_RealDirectInput8 : Game::DirectInput8();
		if (!di)
		{
			spdlog::error("FFB: No DirectInput8 interface available");
			return false;
		}

		// Path A: Open our OWN handle on the remap layer's primary device GUID.
		// (Previously FFB shared the remap polling handle in EXCLUSIVE mode;
		// any poll-side re-Acquire() then destroyed the downloaded effects,
		// producing torque->0->full-step transients. The remap handle is now
		// NONEXCLUSIVE; this one is EXCLUSIVE|BACKGROUND.)
		if (Settings::UseDirectInputRemap && DInputRemap::IsPrimaryInitialized())
		{
			GUID guid;
			if (DInputRemap::GetPrimaryDeviceGuid(&guid))
			{
				auto* dev = OpenFfbDevice(guid, di);
				if (dev)
				{
					DIDEVCAPS caps = {};
					caps.dwSize = sizeof(DIDEVCAPS);
					dev->GetCapabilities(&caps);
					if (caps.dwFlags & DIDC_FORCEFEEDBACK)
					{
						ffbDevice = dev;
						ownsDevice = true;
						spdlog::info("FFB: Opened dedicated FFB handle on remap primary device");
					}
					else
					{
						spdlog::warn("FFB: Remap primary device does not support FFB, trying enumeration...");
						dev->Unacquire();
						dev->Release();
					}
				}
			}
			else
			{
				spdlog::warn("FFB: DInputRemap primary GUID not available, trying enumeration...");
			}
		}

		// Path B: Enumerate first FFB-capable device (standalone / fallback)
		if (!ffbDevice)
		{

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

		// Hardware periodic effects for road texture / tire slip (falls back
		// to constant-force synthesis if the driver rejects them)
		CreatePeriodicEffects();

		// Exit guards: zero the wheel while the window still exists
		InstallExitGuards();

		initialized = true;
		spdlog::info("FFB: Initialization complete (DirectInput)");
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
		if (!initialized || !constantForceEffect || panicStopped)
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

		// Post-recreation ramp-in: after the constant-force effect had to be
		// recreated, fade the force back over ~250ms instead of stepping to
		// full torque (a 250ms fade from zero is imperceptible; a step is a jerk)
		float recreateScale = 1.0f;
		if (recreateRampFrames > 0)
		{
			recreateScale = static_cast<float>(RECREATE_RAMP_FRAMES - recreateRampFrames) / static_cast<float>(RECREATE_RAMP_FRAMES);
			recreateRampFrames--;
		}

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
		float steer = car->field_1D0;                      // Signed steering position, post-sensitivity (-1..1)
		float steerRate = car->field_1D4;                  // Steering derivative (game's own, no differentiation noise)

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

		if (constantForceEffect)
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
			UpdatePeriodicEffect(roadTextureEffect, roadTextureState, roadAmp, roadFreq);
			UpdatePeriodicEffect(tireSlipEffect, tireSlipState, slipAmp, slipFreq);
		}

		// Recreate dead periodic effects (rate-limited, respects holdoff)
		if (Settings::FFBUsePeriodicEffects && (!roadTextureEffect || !tireSlipEffect)
			&& (updateCounter % 60 == 0) && GetTickCount() >= recreateHoldoffUntil)
		{
			CreatePeriodicEffects();
			if (!periodicsActive)
				recreateHoldoffUntil = GetTickCount() + 500;
		}

		// Diagnostic logging: every 2 seconds (gated behind FFBDiagnosticLog)
		if (Settings::FFBDiagnosticLog)
		{
			diagSteerRateMin = std::min(diagSteerRateMin, steerRate);
			diagSteerRateMax = std::max(diagSteerRateMax, steerRate);

			static DWORD lastDiagTime = 0;
			DWORD now = GetTickCount();
			if (now - lastDiagTime >= 2000)
			{
				lastDiagTime = now;
				spdlog::info("FFB DIAG: spd={:.3f} steer={:.3f} rate=[{:.5f}..{:.5f}] lat={:.2f} drift={:.2f} rough={:.2f} constLvl={} periodics={} warmup={}/{}",
					speed, steer, diagSteerRateMin, diagSteerRateMax, smoothedLateral, driftAmt, roughness,
					(int)prevConstantLevel, periodicsActive, warmupFrames, WARMUP_THRESHOLD);
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

		if (!constantForceEffect && !ffbDevice)
			return;

		spdlog::info("FFB: Shutting down...");

		// Zero/stop everything first. If an exit guard (WM_CLOSE subclass or
		// ExitProcess hook) already ran, this is a no-op. If we only got here
		// via DLL_PROCESS_DETACH it is best-effort -- the window may already be
		// gone and the DI calls may fail (which is why the exit guards exist).
		// Note: PanicStop never routes through SetConstantForce, whose E_HANDLE
		// recovery branch would recreate and restart the effect mid-teardown.
		PanicStop();

		__try
		{
			if (ffbDevice)
			{
				// Re-enable autocenter to return wheel to neutral
				// (property writes work on an unacquired device)
				DIPROPDWORD dipdw = {};
				dipdw.diph.dwSize = sizeof(DIPROPDWORD);
				dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
				dipdw.diph.dwObj = 0;
				dipdw.diph.dwHow = DIPH_DEVICE;
				dipdw.dwData = TRUE; // DIPAUTOCENTER_ON
				ffbDevice->SetProperty(DIPROP_AUTOCENTER, &dipdw.diph);

				if (constantForceEffect)
					constantForceEffect->Release();
				if (roadTextureEffect)
					roadTextureEffect->Release();
				if (tireSlipEffect)
					tireSlipEffect->Release();
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			spdlog::warn("FFB: Exception releasing effect (0x{:X})", GetExceptionCode());
		}
		constantForceEffect = nullptr;
		roadTextureEffect = nullptr;
		tireSlipEffect = nullptr;
		periodicsActive = false;

		__try
		{
			if (ffbDevice && ownsDevice)
			{
				// PanicStop already called Unacquire()
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
