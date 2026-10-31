#pragma once
// Pure maths for the three-view spike (triple_views.cpp): the toolkit rig (src/vendor/triple-screen, toolkit 09e6c4f) to
// OutRun's camera inputs per panel. No game or Direct3D dependency; tools/tests/triple_views_math_test.cpp checks it.
#include <array>
#include <cmath>
#include <string>
#include "vendor/triple-screen/geometry/dbce_triple_geometry.hpp"

namespace TripleViewsMath {
// What 0x484BD0 builds one panel's projection from: the base fov +AC (0x483C10 derives the vertical fov +A0 from it),
// the aspect +B8 and the shift +C4/+C8 (0x404310: right = u + u * shiftX, top = t + t * shiftY), and the panel's turn
// about the vertical axis in radians (+ turns right; the camera's y angle +12C takes it with the opposite sign).
struct Panel { float baseFov, aspect, shiftX, shiftY, turn; };

// 0x483C10: +A0 = 2 atan(0.75 tan(+AC / 2)), and back.
inline double VerticalFov(double baseFov) { return 2 * std::atan(0.75 * std::tan(baseFov / 2)); }
inline double BaseFov(double verticalFov) { return 2 * std::atan(std::tan(verticalFov / 2) / 0.75); }

// No measurements: equal panels of `aspect`, the game's own fov, the sides turned by one panel's horizontal field of
// view so the three pictures join without a gap or overlap.
inline std::array<Panel, 3> Seamless(double baseFov, double aspect)
{
    const double turn = 2 * std::atan(aspect * std::tan(VerticalFov(baseFov) / 2));
    std::array<Panel, 3> p{};
    for (int i = 0; i < 3; ++i) p[i] = {float(baseFov), float(aspect), 0.f, 0.f, float(turn * (i - 1))};
    return p;
}

// A measured rig: each panel's frustum from the eye (toolkit build_rig + project) in OutRun's terms. False, with `out`
// unchanged, when the rig does not build or a panel does not project.
inline bool Measured(const dbce::triple_screen::Rig& rig, std::array<Panel, 3>& out)
{
    using namespace dbce::triple_screen;
    std::array<Surface, 3> s{};
    if (build_rig(rig, s) != Error::none) return false;
    std::array<Panel, 3> p{};
    for (int i = 0; i < 3; ++i) {
        Projection pr{};
        if (project(s[i], {0, 0, 0}, 1.0, 1000.0, pr) != Error::none) return false;
        const double halfW = (pr.right - pr.left) / 2, halfH = (pr.top - pr.bottom) / 2;
        p[i] = {float(BaseFov(2 * std::atan(halfH))), float(halfW / halfH), float((pr.right + pr.left) / 2 / halfW),
                float((pr.top + pr.bottom) / 2 / halfH), float(std::atan2(pr.camera_forward.x, -pr.camera_forward.z))};
    }
    out = p;
    return true;
}

// What the last race frame actually drew (triple_views.cpp sets it per frame) and the F7 notice for it.
enum class Shown { Original, GameFov, MeasuredFov, Unavailable };

// `wanted`: Angle is chosen (or forced). `drawnFor`: the choice the last frame was drawn with; until a frame shows a new
// choice the notice says nothing rather than guess. `why`: the reason angle correction is unavailable.
inline std::string ViewLabel(bool wanted, bool drawnFor, Shown shown, const std::string& why)
{
    if (!wanted) return "Original game view (1/2)";
    if (drawnFor != wanted) return "";
    switch (shown) {
    case Shown::GameFov: return "Angle correction \xC2\xB7 Game FOV (2/2)";
    case Shown::MeasuredFov: return "Angle correction \xC2\xB7 Measured FOV (2/2)";
    default: return "Original game view (1/2): angle correction unavailable (" + why + ")";
    }
}

// MatchGameFov (the toolkit's default framing): the eye distance at which the centre panel shows the game's own
// vertical field of view, so the rig's angles and bezels place the side views around the game's picture.
inline bool MatchGameFov(dbce::triple_screen::Rig& rig, double baseFov)
{
    double eye = 0;
    if (dbce::triple_screen::match_vertical_fov(rig.panel_height_mm, VerticalFov(baseFov) * 180 / 3.141592653589793, eye) !=
        dbce::triple_screen::Error::none) return false;
    rig.eye_distance_mm = eye;
    return true;
}
}
