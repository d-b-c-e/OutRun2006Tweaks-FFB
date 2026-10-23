// Offline topology fixture for src/triple_span.hpp ([Triple] Screens = Separate monitors). Pure logic, no game.
#include "../../src/triple_span.hpp"
#include <cstdio>

static int failures;
static void check(bool ok, const char* what)
{
	std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		failures++;
}
static RECT R(long l, long t, long r, long b) { return RECT{ l, t, r, b }; }

int main()
{
	RECT out{};
	bool ok = TripleSpanFromMonitors({ R(0, 0, 2560, 1440), R(-2560, 0, 0, 1440), R(2560, 0, 5120, 1440) }, out);
	check(ok && out.left == -2560 && out.top == 0 && out.right == 5120 && out.bottom == 1440,
		"three contiguous equal monitors (primary in the middle, any order) span -2560..5120 x 0..1440");
	ok = TripleSpanFromMonitors({ R(0, 0, 1920, 1080), R(1920, 0, 3840, 1080), R(3840, 0, 5760, 1080) }, out);
	check(ok && out.left == 0 && out.right == 5760 && out.bottom == 1080, "three contiguous equal 1080p monitors from 0");
	check(!TripleSpanFromMonitors({ R(0, 0, 7680, 1440) }, out), "one display (Surround) is rejected");
	check(!TripleSpanFromMonitors({ R(0, 0, 2560, 1440), R(2560, 0, 5120, 1440) }, out), "two monitors are rejected");
	check(!TripleSpanFromMonitors({ R(-2560, 0, 0, 1440), R(0, 0, 2560, 1440), R(2560, 0, 5120, 1440), R(5120, 0, 7680, 1440) }, out),
		"four monitors are rejected");
	check(!TripleSpanFromMonitors({ R(-2570, 0, -10, 1440), R(0, 0, 2560, 1440), R(2560, 0, 5120, 1440) }, out),
		"a gap between monitors is rejected");
	check(!TripleSpanFromMonitors({ R(-2550, 0, 10, 1440), R(0, 0, 2560, 1440), R(2560, 0, 5120, 1440) }, out),
		"overlapping monitors are rejected");
	check(!TripleSpanFromMonitors({ R(-2560, 0, 0, 1080), R(0, 0, 2560, 1440), R(2560, 0, 5120, 1440) }, out),
		"unequal heights are rejected");
	check(!TripleSpanFromMonitors({ R(-1920, 0, 0, 1440), R(0, 0, 2560, 1440), R(2560, 0, 5120, 1440) }, out),
		"unequal widths are rejected");
	check(!TripleSpanFromMonitors({ R(-2560, 100, 0, 1540), R(0, 0, 2560, 1440), R(2560, 0, 5120, 1440) }, out),
		"a vertically offset monitor is rejected");
	check(!TripleSpanFromMonitors({ R(0, 0, 2560, 1440), R(0, 1440, 2560, 2880), R(0, 2880, 2560, 4320) }, out),
		"three stacked monitors are rejected");
	check(!TripleSpanFromMonitors({ R(0, 0, 0, 1440), R(0, 0, 0, 1440), R(0, 0, 0, 1440) }, out), "empty rectangles are rejected");
	// HUD edge anchors: (anchor width - 4:3 HUD width) / 2 is the OnlineArcade side spacing in hooks_uiscaling.cpp.
	check(TripleHudWidth(7680, 1440, 1) == 2560 && (TripleHudWidth(7680, 1440, 1) - 1920) / 2 == 320,
		"Surround 7680x1440: side HUD stops 320 px outside the 4:3 HUD, at the centre screen's edges");
	check(TripleHudWidth(5760, 1080, 2) == 1920, "Separate monitors 5760x1080: the centre 1920 px");
	check(TripleHudWidth(7680, 1440, 0) == 7680, "Off keeps the full width");
	check(TripleHudWidth(2560, 1440, 1) == 2560, "a single 16:9 window is unchanged");
	check(TripleHudWidth(3440, 1440, 1) == 3440, "21:9 (2.4:1) is unchanged");
	check(TripleHudWidth(5120, 1440, 1) == 1920, "32:9 counts as wide (2.9 rule) but never narrower than the 4:3 HUD");
	check(TripleHudWidth(7680, 0, 1) == 7680, "zero height keeps the full width");
	// Notification border (notifications.hpp: startX = width - toast width - border - 10).
	check(NotificationBorder(7680, 1440, true, 1, 1) == 2560, "Surround 7680x1440 race: toasts at the centre screen's right edge");
	check(NotificationBorder(7680, 1440, true, 1, 2) == 2560, "Separate monitors 7680x1440 race: the same");
	check(NotificationBorder(7680, 1440, true, 1, 0) == 0, "Off: race toasts at the window's right edge, as before");
	check(NotificationBorder(2560, 1440, true, 1, 1) == 0, "16:9 race: unchanged (window edge)");
	check(NotificationBorder(5120, 1440, true, 1, 1) == 1600, "32:9 race: the centred 4:3 HUD area's right edge");
	check(NotificationBorder(7680, 1440, false, 1, 1) == 2880.5f, "letterboxed menu: outside the 4:3, as before");
	check(NotificationBorder(7680, 1440, false, 0, 1) == 0 && NotificationBorder(7680, 1440, false, 2, 1) == 0,
		"menus with UILetterboxing 0 or 2: window edge, as before");
	std::printf("%s triple span topology, %d failures\n", failures ? "FAIL" : "ok", failures);
	return failures ? 1 : 0;
}
