// Three projected views (docs/2026-10-30-triple-views-and-clean-launch-plan.md). With triple screens on, a wide target and
// [Triple] ViewMode = Angle (F7 while driving, saved by the panel's writer; Original when missing), or DBCE_OUTRUN_TRIPLE_VIEWS
// set at process start (development), each race (STATE_GAME) frame render (0x449050) runs once per
// panel: the main camera gets the panel's base fov (+AC), aspect (+B8), shift (+C4/+C8) and turn (y angle +12C, and +388
// which 0x482F20 blends from), its
// matrices and culling planes are recomputed (0x484BD0), and every viewport (read back unmapped), render-target switch onto the frame's base
// target and StretchRect into it is mapped into the panel's third, with a scissor so screen-space draws cannot spill. The
// 2D flush (0x42D710) runs on the centre pass only. The panels come from the toolkit [Triple] measurements in the INIs
// (MatchGameFov, on unless false, keeps the game's vertical fov on the centre panel); without usable measurements they
// are seamless: equal panels, the sides turned by one panel's horizontal field of view. Value: "1", "deg=<n>" to set the
// seamless turn, "flip" to reverse every turn. The log reports three-projections only from three observed passes.
#include "hook_mgr.hpp"
#include "plugin.hpp"
#include "game_addrs.hpp"
#include "triple_views.hpp"
#include "triple_views_math.hpp"
#include "vendor/triple-screen/layout/dbce_triple_ini.hpp"
#include <ini.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace TripleViews {
namespace {
struct Options { bool on = false; float deg = 0.f; bool flip = false; };
const Options& Opt()
{
    static const Options o = [] {
        Options r;
        wchar_t v[64]{};
        const DWORD n = GetEnvironmentVariableW(L"DBCE_OUTRUN_TRIPLE_VIEWS", v, 64);
        if (n == 0 || n >= 64) return r;
        const std::wstring s(v);
        r.on = true;
        r.flip = s.find(L"flip") != std::wstring::npos;
        if (const auto p = s.find(L"deg="); p != std::wstring::npos) r.deg = float(_wtof(s.c_str() + p + 4));
        return r;
    }();
    return o;
}

constexpr uintptr_t FrameRenderCall = 0x17D31, FrameRender = 0x49050, TwoDCall = 0x491D7, TwoD = 0x2D710,
                    CalcCameraMatrix = 0x84BD0, MainCamera = 0x39FE10;
int g_pass = -1;                       // the panel being drawn: 0 left, 1 centre, 2 right; -1 outside the spike
IDirect3DSurface9* g_base = nullptr;   // the frame's base target (render target 0 as the frame render starts)
bool g_onBase = false;
D3DVIEWPORT9 g_gameVp{};               // the viewport the game last set on the base target, in its own (unmapped) terms
UINT g_w = 0, g_h = 0;
unsigned g_frames = 0, g_threePass = 0, g_vpMaps = 0, g_rtMaps = 0, g_srMaps = 0, g_twoDSkips = 0;
float g_turnDeg = 0.f, g_panelAspect = 0.f;
dbce::triple_screen::Rig g_rig{};
bool g_rigOk = false, g_matchGameFov = true, g_measured = false;
bool g_angle = false;     // the chosen correction mode: Angle, or Original
bool g_hooksFailed = false;
bool g_patched = false;   // the frame render and 2D flush calls are redirected (apply)
// What the last race frame drew, for the F7 notice and the log (toolkit policy: report the actual mode and any fallback).
TripleViewsMath::Shown g_shown = TripleViewsMath::Shown::Original;
std::string g_why;
bool g_drawnFor = false;   // the choice that frame was drawn with

void Show(TripleViewsMath::Shown shown, const char* why, bool wanted)
{
    if (shown != g_shown || g_why != why || g_drawnFor != wanted)
        spdlog::info("TripleViews: shown: {}", TripleViewsMath::ViewLabel(wanted, wanted, shown, why));
    g_shown = shown; g_why = why; g_drawnFor = wanted;
}
float g_eyeMm = 0.f;      // the effective eye of the last angled frame (0: seamless)

// Angled views can be drawn: triple screens on and a target at least two and a half times as wide as it is high.
bool Wide()
{
    const auto* pp = Game::D3DPresentParams;
    return Settings::TripleScreens != 0 && pp && pp->BackBufferHeight && pp->BackBufferWidth * 2 >= pp->BackBufferHeight * 5;
}
SafetyHookInline g_setViewport = {}, g_getViewport = {}, g_setRenderTarget = {}, g_stretchRect = {};

RECT Panel() { return {LONG(g_w * g_pass / 3), 0, LONG(g_w * (g_pass + 1) / 3), LONG(g_h)}; }

D3DVIEWPORT9 Map(const D3DVIEWPORT9& v)
{
    const RECT p = Panel();
    const double s = double(p.right - p.left) / g_w;
    D3DVIEWPORT9 m = v;
    m.X = DWORD(p.left + v.X * s);
    m.Width = DWORD(v.Width * s);
    return m;
}

HRESULT STDMETHODCALLTYPE SetViewport_dest(IDirect3DDevice9* d, const D3DVIEWPORT9* v)
{
    if (g_pass < 0 || !g_onBase || !v) return g_setViewport.stdcall<HRESULT>(d, v);
    g_gameVp = *v;
    const D3DVIEWPORT9 m = Map(*v);
    const RECT p = Panel();
    d->SetScissorRect(&p);
    ++g_vpMaps;
    return g_setViewport.stdcall<HRESULT>(d, &m);
}

// The game saves the viewport and sets it back later (frame begin/end, effects): hand back its own viewport, or the
// panel mapping would be applied twice.
HRESULT STDMETHODCALLTYPE GetViewport_dest(IDirect3DDevice9* d, D3DVIEWPORT9* v)
{
    if (g_pass < 0 || !g_onBase || !v) return g_getViewport.stdcall<HRESULT>(d, v);
    *v = g_gameVp;
    return D3D_OK;
}

HRESULT STDMETHODCALLTYPE SetRenderTarget_dest(IDirect3DDevice9* d, DWORD i, IDirect3DSurface9* s)
{
    const HRESULT r = g_setRenderTarget.stdcall<HRESULT>(d, i, s);
    if (g_pass >= 0 && i == 0 && SUCCEEDED(r)) {
        g_onBase = s == g_base;
        if (g_onBase) {
            // Direct3D 9 has just reset the viewport to the whole target: put the panel's back.
            ++g_rtMaps;
            const D3DVIEWPORT9 full{0, 0, g_w, g_h, 0.f, 1.f};
            SetViewport_dest(d, &full);
        }
    }
    return r;
}

HRESULT STDMETHODCALLTYPE StretchRect_dest(IDirect3DDevice9* d, IDirect3DSurface9* src, const RECT* sr, IDirect3DSurface9* dst,
                                           const RECT* dr, D3DTEXTUREFILTERTYPE f)
{
    if (g_pass < 0 || dst != g_base) return g_stretchRect.stdcall<HRESULT>(d, src, sr, dst, dr, f);
    const RECT p = Panel();
    RECT m = p;
    if (dr) {
        const double s = double(p.right - p.left) / g_w;
        m = {LONG(p.left + dr->left * s), dr->top, LONG(p.left + dr->right * s), dr->bottom};
    }
    ++g_srMaps;
    return g_stretchRect.stdcall<HRESULT>(d, src, sr, dst, &m, f);
}

int __cdecl TwoD_dest(int a, int b)
{
    if (g_pass >= 0 && g_pass != 1) { ++g_twoDSkips; return 0; }
    return reinterpret_cast<int(__cdecl*)(int, int)>(Module::exe_ptr(TwoD))(a, b);
}

void CallFrameRender() { reinterpret_cast<void(__cdecl*)()>(Module::exe_ptr(FrameRender))(); }
void CallCamera(void* c) { reinterpret_cast<void(__cdecl*)(void*)>(Module::exe_ptr(CalcCameraMatrix))(c); }

bool HookDevice(IDirect3DDevice9* d)
{
    static bool tried = false, ok = false;
    if (tried) return ok;
    tried = true;
    void** vt = *reinterpret_cast<void***>(d);
    g_stretchRect = safetyhook::create_inline(vt[34], StretchRect_dest);
    g_setRenderTarget = safetyhook::create_inline(vt[37], SetRenderTarget_dest);
    g_setViewport = safetyhook::create_inline(vt[47], SetViewport_dest);
    g_getViewport = safetyhook::create_inline(vt[48], GetViewport_dest);
    ok = g_stretchRect && g_setRenderTarget && g_setViewport && g_getViewport;
    g_hooksFailed = !ok;
    spdlog::info("TripleViews: device hooks {}", ok ? "installed" : "FAILED: the spike stays off");
    return ok;
}

void __cdecl FrameRender_dest()
{
    using TripleViewsMath::Shown;
    auto* d = Game::D3DDevice();
    const bool wanted = Opt().on || g_angle;
    // Device hooks go in as soon as the target is wide, so their state is known before the first F7.
    const bool hooked = d && Wide() && HookDevice(d);
    if (!d || *Game::current_mode != GameState::STATE_GAME) { CallFrameRender(); return; }   // menus: one view, state kept
    if (!wanted) { Show(Shown::Original, "", false); CallFrameRender(); return; }
    if (!Wide()) { Show(Shown::Unavailable, "triple screens off or not wide", true); CallFrameRender(); return; }
    if (!hooked) { Show(Shown::Unavailable, "device hooks failed", true); CallFrameRender(); return; }
    IDirect3DSurface9* base = nullptr;
    D3DSURFACE_DESC desc{};
    if (FAILED(d->GetRenderTarget(0, &base)) || !base || FAILED(base->GetDesc(&desc))) {
        if (base) base->Release();
        Show(Shown::Unavailable, "no render target this frame", true);
        CallFrameRender();
        return;
    }    g_base = base; g_w = desc.Width; g_h = desc.Height;
    auto* c = reinterpret_cast<float*>(Module::exe_ptr<uint8_t>(MainCamera));
    float &baseFov = c[0xAC / 4], &aspect = c[0xB8 / 4], &shiftX = c[0xC4 / 4], &shiftY = c[0xC8 / 4];
    float &y = c[0x12C / 4], &prevY = c[0x388 / 4];
    const float baseFov0 = baseFov, aspect0 = aspect, shiftX0 = shiftX, shiftY0 = shiftY, y0 = y, prevY0 = prevY;
    g_panelAspect = float(g_w) / 3.f / float(g_h);
    auto panels = TripleViewsMath::Seamless(baseFov0, g_panelAspect);
    g_measured = false;
    if (g_rigOk) {
        auto rig = g_rig;
        if (!g_matchGameFov || TripleViewsMath::MatchGameFov(rig, baseFov0)) g_measured = TripleViewsMath::Measured(rig, panels);
        g_eyeMm = g_measured ? float(rig.eye_distance_mm) : 0.f;
    }
    if (!g_measured && Opt().deg != 0.f) { panels[0].turn = -Opt().deg * 3.14159265f / 180.f; panels[2].turn = -panels[0].turn; }
    if (Opt().flip) for (auto& p : panels) p.turn = -p.turn;
    Show(g_measured && !g_matchGameFov ? Shown::MeasuredFov : Shown::GameFov,
         g_rigOk && !g_measured ? "measured rig refused; seamless views" : "", true);
    g_turnDeg = panels[2].turn * 180.f / 3.14159265f;
    DWORD scissor = FALSE;
    d->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissor);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    unsigned passes = 0;
    for (const int i : {0, 2, 1}) {   // the centre last: its 2D flush draws over the finished panels
        g_pass = i;
        g_onBase = true;
        const auto& p = panels[i];
        baseFov = p.baseFov; aspect = p.aspect; shiftX = p.shiftX; shiftY = p.shiftY;
        y = prevY = y0 - p.turn;   // shortcut: a world-y turn, exact only while pitch and roll are small
        CallCamera(c);
        const D3DVIEWPORT9 full{0, 0, g_w, g_h, 0.f, 1.f};
        SetViewport_dest(d, &full);
        CallFrameRender();
        ++passes;
    }
    g_pass = -1;
    g_onBase = false;
    baseFov = baseFov0; aspect = aspect0; shiftX = shiftX0; shiftY = shiftY0; y = y0; prevY = prevY0;
    CallCamera(c);
    prevY = prevY0;
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, scissor);
    const D3DVIEWPORT9 full{0, 0, g_w, g_h, 0.f, 1.f};
    d->SetViewport(&full);   // frame end restored the last panel's viewport
    base->Release();
    g_base = nullptr;
    if (passes == 3) ++g_threePass;
    if (++g_frames % 600 == 0) {
        spdlog::info("TripleViews: three-projections on {} of 600 frames; target {}x{}, panel aspect {:.4f}, right turn {:.1f} deg ({}); "
                     "{} viewports, {} target switches, {} StretchRects mapped; 2D flush skipped {} times",
                     g_threePass, g_w, g_h, g_panelAspect, g_turnDeg, g_measured ? "measured rig" : "seamless", g_vpMaps, g_rtMaps, g_srMaps, g_twoDSkips);
        g_threePass = g_vpMaps = g_rtMaps = g_srMaps = g_twoDSkips = 0;
    }
}

// The [Triple] measurements Wheelkit writes (user INI over the main INI), read once at start.
void ReadRig()
{
    std::vector<std::unique_ptr<inih::INIReader>> inis;
    for (const auto& path : {Module::UserIniPath, Module::IniPath}) {
        std::error_code ec;
        if (path.empty() || !std::filesystem::exists(path, ec)) continue;
        try { inis.push_back(std::make_unique<inih::INIReader>(path)); } catch (...) { /* unreadable: skipped, as Settings::read does */ }
    }
    auto get = [&](const char* key, char* b, size_t n) {
        for (const auto& ini : inis) {
            std::string none = "\x01";   // Get takes the default by reference
            const std::string v = ini->Get<std::string>("Triple", key, none);
            if (v != "\x01") { strncpy_s(b, n, v.c_str(), _TRUNCATE); return true; }
        }
        return false;
    };
    const auto t = dbce::triple_screen::read_ini_triple(get);
    char b[16];
    g_matchGameFov = !(get("MatchGameFov", b, sizeof b) && (!_stricmp(b, "false") || !strcmp(b, "0")));
    g_rigOk = !t.refusal;
    g_rig = t.rig;
    if (g_rigOk)
        spdlog::info("TripleViews: measured rig: panels {}x{} mm, eye {} mm, sides {}/{} deg, bezel {} mm, eye height {} mm; MatchGameFov {}",
                     g_rig.panel_width_mm, g_rig.panel_height_mm, g_rig.eye_distance_mm, g_rig.left_angle_degrees,
                     g_rig.right_angle_degrees, g_rig.bezel_gap_mm, g_rig.eye_height_above_center_mm, g_matchGameFov);
    else spdlog::info("TripleViews: no usable [Triple] measurements ({}); seamless views", t.refusal);
    // Missing or unrecognized: the existing startup behaviour (Original), and nothing is rewritten.
    g_angle = get("ViewMode", b, sizeof b) && !_stricmp(b, "Angle");
    spdlog::info("TripleViews: correction mode {} at start", g_angle ? "Angle" : "Original");
}

// The call at `site` must be E8 rel32 to `target` (both RVAs) before it is redirected.
bool CallsTo(uintptr_t site, uintptr_t target)
{
    const auto* p = Module::exe_ptr<uint8_t>(site);
    return p[0] == 0xE8 && site + 5 + *reinterpret_cast<const int32_t*>(p + 1) == target;
}
}

bool Driving() { return Game::current_mode && *Game::current_mode == GameState::STATE_GAME; }
bool AngleSelected() { return g_angle; }

std::string Label()
{
    const bool wanted = Opt().on || g_angle;
    if (!g_patched) return TripleViewsMath::ViewLabel(wanted, wanted, TripleViewsMath::Shown::Unavailable, "renderer not hooked");
    return TripleViewsMath::ViewLabel(wanted, g_drawnFor, g_shown, g_why);
}

void SelectAngle(bool angle)
{
    g_angle = angle;
    const auto* c = Module::exe_ptr<uint8_t>(MainCamera);
    // What is actually drawn is logged by the next race frame ("TripleViews: shown").
    spdlog::info("TripleViews: F7 chose {}{}; camera mode {}, framing {}, effective eye {} mm", angle ? "Angle" : "Original",
                 Opt().on ? " (development switch forces Angle)" : "", int(int8_t(c[0x34A])),
                 g_rigOk ? (g_matchGameFov ? "measured rig, game fov" : "measured rig, measured fov") : "seamless, game fov", g_eyeMm);
}

class TripleViewsHook : public Hook
{
public:
    std::string_view description() override { return "TripleViews"; }

    bool validate() override { return Opt().on || Settings::TripleScreens != 0; }

    bool apply() override
    {
        if (!CallsTo(FrameRenderCall, FrameRender) || !CallsTo(TwoDCall, TwoD)) {
            spdlog::error("TripleViews: the frame render or 2D flush call is not where this executable has it; spike off");
            return false;
        }
        ReadRig();
        Memory::VP::InjectHook(Module::exe_ptr(FrameRenderCall), FrameRender_dest, Memory::HookType::Call);
        Memory::VP::InjectHook(Module::exe_ptr(TwoDCall), TwoD_dest, Memory::HookType::Call);
        g_patched = true;
        if (Opt().on) spdlog::info("TripleViews: forced on (DBCE_OUTRUN_TRIPLE_VIEWS); seamless turn {}{}", Opt().deg != 0.f ? std::to_string(Opt().deg) + " deg" : std::string("= one panel's horizontal fov"),
                     Opt().flip ? ", flipped" : "");
        return true;
    }

    static TripleViewsHook instance;
};
TripleViewsHook TripleViewsHook::instance;
}
