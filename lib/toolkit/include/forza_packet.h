// forza_packet.h - header-only Forza "Data Out" telemetry encoder (C++11, MSVC and GCC).
//
// The C++ counterpart of Dbce.Wheel.Telemetry.ForzaPacket, for native mods
// (dinput8 proxies, emulator forks). Three layouts, all understood by SimHub and
// most dashboards; receivers validate by EXACT length:
//
//   FORZA_SLED_232     physics sled only
//   FORZA_FM7_DASH_311 sled + 79-byte dash at offset 232 (Forza Motorsport 7 "Dash")
//   FORZA_HORIZON_324  sled + 12 unknown bytes + dash at 244 + 1 trailing byte
//
// 323 (232 + 12 + 79) is rejected silently: SimHub logs "started receiving
// unprocessed data" at packet rate and never connects (milestone-wheel-tools,
// 2026-08-30). The trailing byte is unused by every real Forza title; mods stamp
// a sentinel there ('R' mod, 'K' idle keeper, 'S' synthetic) so a probe can tell
// whose packets it sees.
//
// Semantics that took a session each to learn (knowledge/TELEMETRY-FORZA.md):
// IsRaceOn true from the start-line countdown and false in menus; gear 0 renders
// as reverse and Forza has no neutral, so show 1; keep emitting zeroed frames
// between races (with the same engine constants - a fully zero packet can wedge
// SimHub); missing data is "leave the field alone", not a confident zero.
//
// Lineage: milestone-wheel-tools src/telemetry.cpp structs (validated live),
// art-of-sim-rally ForzaPacket.cs offsets (Speed@256, Gear@319 validated against
// SimHub via cruisn-collection). Wheel arrays are FL, FR, RL, RR everywhere.
#pragma once
#include <cstdint>
#include <cstring>

namespace dbce { namespace forza {

enum Layout { FORZA_SLED_232 = 232, FORZA_FM7_DASH_311 = 311, FORZA_HORIZON_324 = 324 };

#pragma pack(push, 1)
struct Sled {
    int32_t  isRaceOn;          //   0
    uint32_t timestampMs;       //   4
    float engineMaxRpm;         //   8
    float engineIdleRpm;        //  12
    float currentEngineRpm;     //  16
    float accX, accY, accZ;     //  20  (local, m/s^2)
    float velX, velY, velZ;     //  32  (local, m/s)
    float angX, angY, angZ;     //  44  (rad/s)
    float yaw, pitch, roll;     //  56
    float normSusp[4];          //  68  FL FR RL RR, 0..1
    float slipRatio[4];         //  84
    float wheelRotSpeed[4];     // 100  rad/s
    int32_t onRumble[4];        // 116  0/1
    float puddle[4];            // 132
    float surfaceRumble[4];     // 148
    float slipAngle[4];         // 164
    float combinedSlip[4];      // 180
    float suspTravel[4];        // 196  metres
    int32_t carOrdinal;         // 212
    int32_t carClass;           // 216
    int32_t carPI;              // 220
    int32_t drivetrain;         // 224  0 FWD 1 RWD 2 AWD
    int32_t cylinders;          // 228
};
struct Dash {
    float posX, posY, posZ;     //  +0
    float speed;                // +12  m/s
    float power;                // +16  W
    float torque;               // +20  Nm
    float tireTemp[4];          // +24
    float boost;                // +40
    float fuel;                 // +44
    float distance;             // +48
    float bestLap, lastLap, currentLap, currentRaceTime;  // +52
    uint16_t lapNumber;         // +68
    uint8_t racePosition;       // +70
    uint8_t accel, brake, clutch, handbrake;  // +71..+74, 0..255
    uint8_t gear;               // +75
    int8_t  steer;              // +76  -127..127
    int8_t  drivingLine;        // +77
    int8_t  aiBrakeDiff;        // +78
};
#pragma pack(pop)
static_assert(sizeof(Sled) == 232, "Forza sled must be 232 bytes");
static_assert(sizeof(Dash) == 79,  "Forza dash must be 79 bytes");

// Offsets a receiver or a probe would use. Horizon dash = FM7 dash + 12.
enum { OFF_SPEED_FM7 = 232 + 12, OFF_SPEED_HORIZON = 244 + 12,
       OFF_GEAR_FM7 = 232 + 75,  OFF_GEAR_HORIZON = 244 + 75,
       OFF_STEER_FM7 = 232 + 76, OFF_STEER_HORIZON = 244 + 76,
       OFF_SENTINEL_HORIZON = 323 };

// Clamp helpers for the byte fields.
inline uint8_t pedal01(float f) { if (f < 0) f = 0; if (f > 1) f = 1; return (uint8_t)(f * 255.f + 0.5f); }
inline int8_t  steer11(float f) { if (f < -1) f = -1; if (f > 1) f = 1; return (int8_t)(f * 127.f); }

// Serialises into `out` (at least `layout` bytes). Returns the packet length,
// or 0 if the buffer is too small. The buffer is fully written.
inline int build(Layout layout, const Sled& sled, const Dash& dash, uint8_t* out, int outSize, uint8_t sentinel = 0)
{
    const int size = (int)layout;
    if (!out || outSize < size) return 0;
    std::memset(out, 0, (size_t)size);
    std::memcpy(out, &sled, sizeof(Sled));
    if (layout == FORZA_SLED_232) return size;
    int n = 232;
    if (layout == FORZA_HORIZON_324) n += 12;          // the Horizon insert stays zero
    std::memcpy(out + n, &dash, sizeof(Dash)); n += (int)sizeof(Dash);
    if (layout == FORZA_HORIZON_324) out[n++] = sentinel;   // trailing byte: 324 exactly
    return n;
}

// Idle-keeper frame: dash stays live at zero with the same engine constants the
// game sends; gear 1. See Dbce.Wheel.Telemetry.IdleKeeper for the reasoning.
inline int build_idle(Layout layout, float engineMaxRpm, float engineIdleRpm, uint32_t timestampMs, uint8_t* out, int outSize)
{
    Sled s; std::memset(&s, 0, sizeof s);
    s.isRaceOn = 1; s.timestampMs = timestampMs; s.engineMaxRpm = engineMaxRpm; s.engineIdleRpm = engineIdleRpm;
    Dash d; std::memset(&d, 0, sizeof d);
    d.gear = 1;
    return build(layout, s, d, out, outSize, 0x4B /* 'K' */);
}

}} // namespace dbce::forza
