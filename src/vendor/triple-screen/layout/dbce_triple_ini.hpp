#pragma once
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "../geometry/dbce_triple_geometry.hpp"

// C++17. The [Triple] INI contract Wheelkit Apply writes for projected-triples mods (see docs/native-compositor.md).
// `get(key, buffer, size)` returns true and fills a NUL-terminated value when the key exists; mods wrap
// GetPrivateProfileStringA or their own parser. Width is the panel CHORD; CurveRadiusMm 0 means flat.
namespace dbce::triple_screen {
enum class Screens { off, surround, separate_monitors };
struct IniTriple {
    Screens screens;
    Rig rig;
    double curve_radius_mm;
    const char* refusal;   // nullptr when the geometry is usable; Screens is valid either way
};

template <class Get> IniTriple read_ini_triple(Get get) {
    IniTriple t{Screens::off, {}, 0, nullptr};
    char b[64];
    if (get("Screens", b, sizeof b)) {
        if (!std::strcmp(b, "Surround")) t.screens = Screens::surround;
        else if (!std::strcmp(b, "Separate monitors")) t.screens = Screens::separate_monitors;
    }
    struct Key { const char* name; double* to; double low, high; bool required; };
    double width = 0, height = 0, eye = 0, left = 0, right = 0, bezel = 0, lift = 0, radius = 0;
    const Key keys[] = {{"PanelWidthMm", &width, 100, 3000, true}, {"PanelHeightMm", &height, 50, 2000, true},
                        {"EyeDistanceMm", &eye, 100, 3000, true}, {"LeftYawDegrees", &left, 0, 89.999, true},
                        {"RightYawDegrees", &right, 0, 89.999, true}, {"BezelMm", &bezel, 0, 100, false},
                        {"EyeHeightMm", &lift, -1000, 1000, false}, {"CurveRadiusMm", &radius, 0, 20000, false}};
    for (const Key& k : keys) {
        if (!get(k.name, b, sizeof b)) { if (k.required) { t.refusal = "a [Triple] geometry key is missing"; return t; } continue; }
        char* end = nullptr;
        errno = 0;
        const double v = std::strtod(b, &end);
        if (end == b || *end || errno || !std::isfinite(v) || v < k.low || v > k.high) { t.refusal = "a [Triple] geometry value is out of range"; return t; }
        *k.to = v;
    }
    if (radius != 0 && (radius < 300 || radius < width / 2)) { t.refusal = "CurveRadiusMm is too small for the panel width"; return t; }
    // EyeDistanceMm is measured to the glass centre; the rig wants the chord plane (the one shared conversion).
    if (chord_eye_distance(eye, width, radius, eye) != Error::none) { t.refusal = "EyeDistanceMm is shorter than the panel's curve depth"; return t; }
    t.rig = {width, height, eye, left, right, lift, bezel};
    t.curve_radius_mm = radius;
    return t;
}
} // namespace dbce::triple_screen
