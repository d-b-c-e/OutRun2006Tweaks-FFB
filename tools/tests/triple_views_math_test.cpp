// Offline checks for src/triple_views_math.hpp (tools/tests/Test-TripleViewsMath.ps1).
#include <cstdio>
#include "triple_views_math.hpp"

static int g_checks = 0, g_failed = 0;
static void check(bool ok, const char* what) { ++g_checks; if (!ok) { ++g_failed; std::printf("FAIL %s\n", what); } }
static bool near(double a, double b, double e = 1e-4) { return std::fabs(a - b) <= e; }
static const double kDeg = 3.141592653589793 / 180;

int main()
{
    using namespace TripleViewsMath;
    // The base fov round-trips through 0x483C10's vertical fov.
    check(near(BaseFov(VerticalFov(1.2)), 1.2), "BaseFov inverts VerticalFov");

    // Seamless: centre straight, sides turned by one panel's horizontal fov, symmetric, unshifted.
    const auto s = Seamless(1.4, 16.0 / 9);
    const double h = 2 * std::atan(16.0 / 9 * std::tan(VerticalFov(1.4) / 2));
    check(near(s[1].turn, 0) && near(s[0].turn, -h) && near(s[2].turn, h), "seamless turns are -hfov, 0, +hfov");
    check(near(s[0].shiftX, 0) && near(s[1].aspect, 16.0 / 9) && near(s[2].baseFov, 1.4), "seamless keeps aspect, fov, no shift");

    // A measured flat rig: 697 x 392 mm panels, eye 650 mm, sides at 60 / 50 degrees, bezel 10 mm.
    dbce::triple_screen::Rig rig{697, 392, 650, 60, 50, 0, 10};
    std::array<Panel, 3> m{};
    check(Measured(rig, m), "a measured rig maps");
    check(near(m[1].turn, 0) && near(m[1].shiftX, 0) && near(m[1].shiftY, 0), "the centre panel looks straight ahead");
    check(near(m[1].aspect, 697.0 / 392) && near(VerticalFov(m[1].baseFov), 2 * std::atan(196.0 / 650)), "centre aspect and vertical fov from the panel");
    check(near(m[0].turn, -60 * kDeg) && near(m[2].turn, 50 * kDeg), "the side panels turn by their measured angles");
    check(near(m[0].aspect, 697.0 / 392) && near(m[2].aspect, 697.0 / 392), "every panel keeps the panel's aspect");
    // A symmetric rig gives mirrored sides: opposite turns and opposite shifts (each panel's right axis points the same
    // way in the world, so a mirrored frustum has the opposite shift).
    dbce::triple_screen::Rig even = rig;
    even.right_angle_degrees = even.left_angle_degrees;
    std::array<Panel, 3> e{};
    check(Measured(even, e) && near(e[0].turn, -e[2].turn) && near(e[0].shiftX, -e[2].shiftX) && e[0].shiftX != 0,
          "a symmetric rig mirrors the side views");

    // Eye height: the eye above the panels' centre shifts every frustum down.
    dbce::triple_screen::Rig high = rig;
    high.eye_height_above_center_mm = 50;
    std::array<Panel, 3> hm{};
    check(Measured(high, hm) && hm[1].shiftY < 0 && hm[0].shiftY < 0, "an eye above centre shifts the views down");

    // MatchGameFov: the eye distance that makes the centre panel show the game's vertical fov.
    dbce::triple_screen::Rig match = rig;
    check(MatchGameFov(match, 1.4) && Measured(match, m) && near(m[1].baseFov, 1.4), "MatchGameFov keeps the game's fov on the centre");

    // A rig that cannot build leaves the output alone.
    dbce::triple_screen::Rig bad = rig;
    bad.left_angle_degrees = 95;
    std::array<Panel, 3> keep = s;
    check(!Measured(bad, keep) && near(keep[2].turn, s[2].turn), "an invalid rig is refused without changing the output");

    // A side view turns about the camera's own up axis, for any pitch and roll.
    {
        const Angles level{0, 0.7, 0};
        const Angles t = TurnInView(level, 0.9);
        check(near(t.x, 0) && near(t.z, 0) && near(std::remainder(t.y - (0.7 - 0.9), 2 * 3.141592653589793), 0),
              "with no pitch or roll the turn is y - turn");
        const Angles cam{0.25, -1.1, 0.12};   // pitched and rolled
        const Angles side = TurnInView(cam, -1.0);
        const M3 want = Mul(ViewRotation(cam), RotY(-1.0)), got = ViewRotation(side);
        bool same = true, upKept = true;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) same = same && near(want[r][c], got[r][c], 1e-9);
        const M3 base = ViewRotation(cam);
        for (int r = 0; r < 3; ++r) upKept = upKept && near(base[r][1], got[r][1], 1e-9);   // the camera's up in world
        check(same, "the solved angles rebuild the turned view rotation exactly");
        check(upKept, "the turn keeps the camera's up axis (pitch and roll carried into the side view)");
        check(!near(side.x, cam.x, 1e-3), "a pitched camera's side view needs other angles than a world-y turn");
    }

    // The F7 notice follows what was drawn, never only what was chosen.
    check(ViewLabel(false, false, Shown::Original, "") == "Original game view (1/2)", "Original is shown as Original");
    check(ViewLabel(true, false, Shown::Original, "").empty(), "no notice text until a frame shows the new choice");
    check(ViewLabel(true, true, Shown::GameFov, "").find("Game FOV (2/2)") != std::string::npos, "an angled frame reports Game FOV");
    check(ViewLabel(true, true, Shown::MeasuredFov, "").find("Measured FOV (2/2)") != std::string::npos, "measured framing is named");
    check(ViewLabel(true, true, Shown::Unavailable, "no render target") ==
              "Original game view (1/2): angle correction unavailable (no render target)", "a fallback reports the original view and why");

    std::printf("%s %d triple-views math checks\n", g_failed ? "FAIL" : "PASS", g_checks);
    return g_failed ? 1 : 0;
}
