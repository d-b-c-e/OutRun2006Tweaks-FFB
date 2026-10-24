#pragma once
// [DirectInput.Shifter] GearMode = hpattern (hooks_inputremap.cpp). OutRun has six forward gears and no neutral, reverse or
// clutch; it changes gear only by GearUp / GearDown pulses. The rule follows the decomp's reviewed H-pattern (ba07f67):
// - exactly one held gate 1-6 selects a gear. No gate, two gates or the reverse gate select none, and the car keeps its gear.
// - a pulse goes out only while the car's gear word (+208) is 1-6 and differs from the gate.
// - after three pulses that left the gear unchanged (a gate above the car's top gear, an automatic gearbox), none until the
//   gate or the gear changes.
// Pure, so it is tested offline (tools/tests/hpattern_gate_fixture.cpp).
#include <cstdint>

inline int HPatternGate(const bool gates[6], bool reverseHeld)
{
	if (reverseHeld) return 0;
	int gate = 0;
	for (int k = 0; k < 6; ++k)
		if (gates[k])
		{
			if (gate) return 0;
			gate = k + 1;
		}
	return gate;
}

struct HPatternPulses
{
	static constexpr int Unanswered = 3;
	int gate = 0, sent = 0;
	uint32_t gearAtPulse = 0;

	// +1 shift up, -1 shift down or 0, for a step the caller may pulse on (in a race, past its cooldown).
	int Step(int newGate, uint32_t gear)
	{
		if (newGate != gate) { gate = newGate; sent = 0; }
		if (gate < 1 || gate > 6 || gear < 1 || gear > 6 || gear == uint32_t(gate)) { sent = 0; return 0; }
		if (gear != gearAtPulse) sent = 0;
		if (sent >= Unanswered) return 0;
		++sent;
		gearAtPulse = gear;
		return gear < uint32_t(gate) ? 1 : -1;
	}
};
