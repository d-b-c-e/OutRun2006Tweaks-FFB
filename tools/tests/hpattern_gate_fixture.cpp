// Offline fixture for src/hpattern_gate.hpp ([DirectInput.Shifter] GearMode = hpattern). Pure logic, no game.
#include "../../src/hpattern_gate.hpp"
#include <cstdio>

static int failures;
static void check(bool ok, const char* what)
{
	std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		failures++;
}

int main()
{
	bool none[6]{}, three[6]{ false, false, true }, twoHeld[6]{ false, true, false, true };
	check(HPatternGate(none, false) == 0 && HPatternGate(three, false) == 3, "no gate selects none; one gate selects its gear");
	check(HPatternGate(twoHeld, false) == 0, "two held gates select none (no lower-gate guess)");
	check(HPatternGate(three, true) == 0, "reverse held with a forward gate selects none");

	HPatternPulses p;
	check(p.Step(3, 1) == 1 && p.Step(3, 2) == 1 && p.Step(3, 3) == 0, "gate 3 from gear 1: up, up, then at the gate");
	check(p.Step(2, 5) == -1 && p.Step(2, 4) == -1 && p.Step(2, 3) == -1 && p.Step(2, 2) == 0, "gate 2 from gear 5: three downs");

	HPatternPulses top;
	check(top.Step(6, 5) == 1 && top.Step(6, 5) == 1 && top.Step(6, 5) == 1, "an unanswered gate gets three pulses");
	check(top.Step(6, 5) == 0 && top.Step(6, 5) == 0, "then none while the gate and gear stay");
	check(top.Step(6, 4) == 1, "a gear change (the game's own) allows pulses again");
	check(top.Step(5, 4) == 1 && top.Step(6, 5) == 1, "a new gate restarts the count");

	HPatternPulses words;
	check(words.Step(3, 0) == 0 && words.Step(3, 7) == 0 && words.Step(3, 0xffffffffu) == 0, "a gear word outside 1-6 is never shifted from");
	check(words.Step(0, 2) == 0 && words.Step(-1, 2) == 0 && words.Step(7, 2) == 0, "no gate, reverse or an invalid gate sends nothing");
	check(words.Step(3, 2) == 1, "a valid gate and gear word pulse again");

	std::printf("%s hpattern gate, %d failures\n", failures ? "FAIL" : "ok", failures);
	return failures ? 1 : 0;
}
