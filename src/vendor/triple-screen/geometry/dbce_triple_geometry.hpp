#pragma once
#include <array>
#include <cmath>

// C++17, graphics-independent physical geometry. See docs/native-geometry.md.
namespace dbce::triple_screen {
struct Vec3 { double x, y, z; };
inline Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 operator*(Vec3 a, double s) noexcept { return {a.x*s,a.y*s,a.z*s}; }
inline double dot(Vec3 a, Vec3 b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
inline bool finite(Vec3 v) noexcept { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
struct Surface { Vec3 lower_left, lower_right, upper_left; };
struct Rig {
    double panel_width_mm, panel_height_mm, eye_distance_mm;
    double left_angle_degrees, right_angle_degrees, eye_height_above_center_mm, bezel_gap_mm;
};
struct Projection {
    Vec3 camera_position, camera_right, camera_up, camera_forward;
    double left, right, bottom, top, near_plane, far_plane;
};
enum class Error { none, invalid_input, degenerate_surface, eye_behind_surface, degenerate_frustum };
namespace detail {
constexpr double pi = 3.141592653589793238462643383279502884;
inline bool unit(Vec3 v, Vec3& out, double epsilon) noexcept {
    const double length = std::sqrt(dot(v,v));
    if (!std::isfinite(length) || length <= epsilon) return false;
    out = v*(1.0/length); return finite(out);
}
inline Error basis(const Surface& s, Vec3& right, Vec3& up, Vec3& normal) noexcept {
    if (!finite(s.lower_left)||!finite(s.lower_right)||!finite(s.upper_left)) return Error::invalid_input;
    if (!unit(s.lower_right-s.lower_left,right,1e-9) ||
        !unit(s.upper_left-s.lower_left,up,1e-9) || std::abs(dot(right,up))>1e-6 ||
        !unit(cross(right,up),normal,1e-12)) return Error::degenerate_surface;
    return Error::none;
}
inline Surface surface(Vec3 center, Vec3 right, double width, double height) noexcept {
    const Vec3 h=right*(width/2), v={0,height/2,0};
    return {center-h-v,center+h-v,center-h+v};
}
} // namespace detail

// All outputs remain unchanged on error. Panels are ordered left, center, right.
inline Error build_rig(const Rig& r, std::array<Surface,3>& out) noexcept {
    for (double v : {r.panel_width_mm,r.panel_height_mm,r.eye_distance_mm,r.left_angle_degrees,
                     r.right_angle_degrees,r.eye_height_above_center_mm,r.bezel_gap_mm})
        if (!std::isfinite(v)) return Error::invalid_input;
    if (r.panel_width_mm<=0 || r.panel_height_mm<=0 || r.eye_distance_mm<=0 ||
        r.left_angle_degrees<0 || r.left_angle_degrees>=90 ||
        r.right_angle_degrees<0 || r.right_angle_degrees>=90 || r.bezel_gap_mm<0)
        return Error::invalid_input;
    const double l=r.left_angle_degrees*detail::pi/180, a=r.right_angle_degrees*detail::pi/180;
    const Vec3 lr={std::cos(l),0,-std::sin(l)}, rr={std::cos(a),0,std::sin(a)};
    const Vec3 center={0,-r.eye_height_above_center_mm,-r.eye_distance_mm};
    const Vec3 lh=center+Vec3{-r.panel_width_mm/2,0,0}, rh=center+Vec3{r.panel_width_mm/2,0,0};
    const double offset=r.bezel_gap_mm+r.panel_width_mm/2;
    const std::array<Surface,3> candidate={
        detail::surface(lh-lr*offset,lr,r.panel_width_mm,r.panel_height_mm),
        detail::surface(center,{1,0,0},r.panel_width_mm,r.panel_height_mm),
        detail::surface(rh+rr*offset,rr,r.panel_width_mm,r.panel_height_mm)};
    for (const auto& s : candidate) {
        Vec3 right{},up{},normal{};
        const auto error=detail::basis(s,right,up,normal);
        if (error!=Error::none) return error;
    }
    out=candidate; return Error::none;
}

// Corners and eye share a length unit (normally mm). Bounds use near_plane units.
// RH: +X right, +Y up, physical center panel at -Z. No matrix/clip-depth convention.
inline Error project(const Surface& s, Vec3 eye, double near_plane, double far_plane, Projection& out) noexcept {
    if (!finite(eye)||!std::isfinite(near_plane)||!std::isfinite(far_plane)||near_plane<=0||far_plane<=near_plane)
        return Error::invalid_input;
    Vec3 right{},up{},normal{};
    const auto error=detail::basis(s,right,up,normal);
    if (error!=Error::none) return error;
    const Vec3 va=s.lower_left-eye, vb=s.lower_right-eye, vc=s.upper_left-eye;
    const double distance=-dot(va,normal);
    if (!std::isfinite(distance)) return Error::invalid_input;
    if (distance<=1e-9) return Error::eye_behind_surface;
    const double l=dot(right,va)*near_plane/distance, r=dot(right,vb)*near_plane/distance;
    const double b=dot(up,va)*near_plane/distance, t=dot(up,vc)*near_plane/distance;
    if (!std::isfinite(l)||!std::isfinite(r)||!std::isfinite(b)||!std::isfinite(t)||
        !std::isfinite(r-l)||!std::isfinite(t-b)||r-l<=1e-12||t-b<=1e-12)
        return Error::degenerate_frustum;
    out={eye,right,up,normal*(-1),l,r,b,t,near_plane,far_plane}; return Error::none;
}

// UV origin is lower left. Both endpoints are included. Ray origin is eye.
inline Error eye_ray(const Surface& s, Vec3 eye, double u, double v, Vec3& out) noexcept {
    if (!finite(eye)||!std::isfinite(u)||!std::isfinite(v)||u<0||u>1||v<0||v>1) return Error::invalid_input;
    Vec3 right{},up{},normal{},direction{};
    const auto error=detail::basis(s,right,up,normal);
    if (error!=Error::none) return error;
    const Vec3 target=s.lower_left+(s.lower_right-s.lower_left)*u+(s.upper_left-s.lower_left)*v;
    if (!detail::unit(target-eye,direction,1e-12)) return Error::invalid_input;
    out=direction; return Error::none;
}

// The measured eye distance (eye to the centre panel's GLASS centre, Wheelkit/INI EyeDistanceMm) to build_rig's
// eye_distance_mm (eye to the centre panel's CHORD plane). A curved panel's glass centre lies its sag behind the chord,
// so the chord plane is that much closer; radius 0 (flat) returns the input unchanged. Apply exactly once, when
// building the rig from measurements.
inline Error chord_eye_distance(double glass_eye_mm, double chord_mm, double curve_radius_mm, double& out) noexcept {
    if (!std::isfinite(glass_eye_mm)||!std::isfinite(chord_mm)||!std::isfinite(curve_radius_mm)||glass_eye_mm<=0||
        chord_mm<=0||curve_radius_mm<0||(curve_radius_mm>0&&chord_mm>2*curve_radius_mm)) return Error::invalid_input;
    const double h=chord_mm/2;   // sagitta in the cancellation-free form the managed CurvedPanelWarp.Sagitta uses
    const double sag=curve_radius_mm>0?h*h/(curve_radius_mm+std::sqrt(curve_radius_mm*curve_radius_mm-h*h)):0;
    if (glass_eye_mm-sag<=0) return Error::eye_behind_surface;
    out=glass_eye_mm-sag; return Error::none;
}

// Explicit policy only: returns effective eye distance in mm; does not mutate a rig.
inline Error match_vertical_fov(double panel_height_mm, double degrees, double& out) noexcept {
    if (!std::isfinite(panel_height_mm)||!std::isfinite(degrees)||panel_height_mm<=0||degrees<=0||degrees>=180)
        return Error::invalid_input;
    const double distance=panel_height_mm/2/std::tan(degrees*detail::pi/360);
    if (!std::isfinite(distance)||distance<=0) return Error::invalid_input;
    out=distance; return Error::none;
}
} // namespace dbce::triple_screen
