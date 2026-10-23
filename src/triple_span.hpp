#pragma once
// [Triple] Screens = Separate monitors (STD-015/022): the rectangle over exactly three monitors of equal size side by
// side with no gap or overlap ("Sim Racing"). Pure function of the monitor rectangles, so it is tested offline
// (tools/tests/triple_span_fixture.cpp); hooks_misc.cpp feeds it EnumDisplayMonitors. Any other layout, including
// Surround (which Windows reports as one display), gives false.
#include <windows.h>
#include <algorithm>
#include <vector>

inline bool TripleSpanFromMonitors(std::vector<RECT> r, RECT& out)
{
	if (r.size() != 3)
		return false;
	std::sort(r.begin(), r.end(), [](const RECT& a, const RECT& b) { return a.left < b.left; });
	for (size_t i = 0; i < r.size(); i++)
	{
		if (r[i].right <= r[i].left || r[i].bottom <= r[i].top)
			return false;
		if (r[i].top != r[0].top || r[i].bottom != r[0].bottom || r[i].right - r[i].left != r[0].right - r[0].left)
			return false;
		if (i && r[i].left != r[i - 1].right)
			return false;
	}
	out = { r[0].left, r[0].top, r[2].right, r[0].bottom };
	return true;
}

// HUD on the centre screen (STD-007/019): on a window at least three screens wide (the toolkit's 2.9:1 wide-primary
// rule), the width that edge-anchored HUD sprites spread to is the middle third, never narrower than the 4:3 HUD.
// A single 32:9 monitor gets that centred 4:3 area (wider than its middle third; it has no centre panel).
inline float TripleHudWidth(float width, float height, int tripleScreens)
{
	if (tripleScreens == 0 || !(height > 0) || width < height * 2.9f)
		return width;
	return (std::max)(width / 3, height * 4 / 3);
}
