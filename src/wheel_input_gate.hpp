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
struct DiagnosticSnapshot
{
    bool installed = false;
    uint32_t reason = 0, heldMask = 0;
    uint64_t queries = 0, uiBlocked = 0, releaseBlocked = 0, forwarded = 0;
    uint64_t confirmQueries = 0, confirmUiBlocked = 0, confirmReleaseBlocked = 0;
    uint64_t confirmHeldDown = 0, confirmForwarded = 0, confirmPositive = 0;
    uint64_t returnKeyDown = 0, textBlocked = 0;
};
bool Install();
bool Suspended();
// Observes existing dispatch only; never polls devices or changes a gate.
DiagnosticSnapshot Diagnostics();
bool SuppressTextMessage(uint32_t message, uintptr_t key, intptr_t detail);
}
