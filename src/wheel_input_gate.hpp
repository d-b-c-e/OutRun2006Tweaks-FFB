#pragma once
#include <cstdint>

namespace WheelInput
{
// A blocked button must be observed released before its next press can reach
// the game. Queries are partial masks; unrelated bits must keep their latch.
struct DispatchGate
{
    uint32_t held = 0;
    void Suspend() { held = UINT32_MAX; }
    uint32_t Filter(uint32_t query, uint32_t now, uint32_t requested, bool suspended)
    {
        if (suspended) { Suspend(); return 0; }
        held &= ~query | now;
        return requested & query & ~held;
    }
};
}

namespace WheelInputGuard
{
bool Install();
bool Suspended();
}
