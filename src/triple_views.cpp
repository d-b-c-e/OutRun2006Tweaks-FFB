// Dev-only spike for three projected views (docs/2026-10-30-triple-views-and-clean-launch-plan.md, step 3), off unless
// DBCE_OUTRUN_TRIPLE_VIEWS is set at process start. During a race (STATE_GAME) each frame render (0x449050) runs once per
// panel: the main camera gets the panel's aspect (+B8) and turn (y angle +12C, and +388 which 0x482F20 blends from), its
// matrices and culling planes are recomputed (0x484BD0), and every viewport (read back unmapped), render-target switch onto the frame's base
// target and StretchRect into it is mapped into the panel's third, with a scissor so screen-space draws cannot spill. The
// 2D flush (0x42D710) runs on the centre pass only. Value: "1" (turn = one panel's horizontal field of view, seamless),
// "deg=<n>" to set the turn, "flip" to reverse its sign. The log reports three-projections only from three observed passes.
#include "hook_mgr.hpp"
#include "plugin.hpp"
#include "game_addrs.hpp"
#include <cmath>
#include <cstdlib>
#include <string>

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
    spdlog::info("TripleViews: device hooks {}", ok ? "installed" : "FAILED: the spike stays off");
    return ok;
}

void __cdecl FrameRender_dest()
{
    auto* d = Game::D3DDevice();
    if (!d || *Game::current_mode != GameState::STATE_GAME || !HookDevice(d)) { CallFrameRender(); return; }
    IDirect3DSurface9* base = nullptr;
    D3DSURFACE_DESC desc{};
    if (FAILED(d->GetRenderTarget(0, &base)) || !base || FAILED(base->GetDesc(&desc))) {
        if (base) base->Release();
        CallFrameRender();
        return;
    }
    g_base = base; g_w = desc.Width; g_h = desc.Height;
    auto* c = Module::exe_ptr<uint8_t>(MainCamera);
    float& aspect = *reinterpret_cast<float*>(c + 0xB8);
    float& y = *reinterpret_cast<float*>(c + 0x12C);
    float& prevY = *reinterpret_cast<float*>(c + 0x388);
    const float aspect0 = aspect, y0 = y, prevY0 = prevY;
    g_panelAspect = float(g_w) / 3.f / float(g_h);
    aspect = g_panelAspect;
    CallCamera(c);   // the vertical field of view +A0 comes from the base fov +AC, not from the aspect
    const float vfov = *reinterpret_cast<float*>(c + 0xA0);
    const float turn = (Opt().deg != 0.f ? Opt().deg * 3.14159265f / 180.f : 2.f * std::atan(g_panelAspect * std::tan(vfov * 0.5f))) *
                       (Opt().flip ? -1.f : 1.f);
    g_turnDeg = turn * 180.f / 3.14159265f;
    DWORD scissor = FALSE;
    d->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissor);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    unsigned passes = 0;
    for (const int i : {0, 2, 1}) {   // the centre last: its 2D flush draws over the finished panels
        g_pass = i;
        g_onBase = true;
        y = prevY = y0 + turn * float(1 - i);   // shortcut: a world-y turn, exact only while pitch and roll are small
        aspect = g_panelAspect;
        CallCamera(c);
        const D3DVIEWPORT9 full{0, 0, g_w, g_h, 0.f, 1.f};
        SetViewport_dest(d, &full);
        CallFrameRender();
        ++passes;
    }
    g_pass = -1;
    g_onBase = false;
    aspect = aspect0; y = y0; prevY = prevY0;
    CallCamera(c);
    prevY = prevY0;
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, scissor);
    const D3DVIEWPORT9 full{0, 0, g_w, g_h, 0.f, 1.f};
    d->SetViewport(&full);   // frame end restored the last panel's viewport
    base->Release();
    g_base = nullptr;
    if (passes == 3) ++g_threePass;
    if (++g_frames % 600 == 0) {
        spdlog::info("TripleViews: three-projections on {} of 600 frames; target {}x{}, panel aspect {:.4f}, side turn {:.1f} deg; "
                     "{} viewports, {} target switches, {} StretchRects mapped; 2D flush skipped {} times",
                     g_threePass, g_w, g_h, g_panelAspect, g_turnDeg, g_vpMaps, g_rtMaps, g_srMaps, g_twoDSkips);
        g_threePass = g_vpMaps = g_rtMaps = g_srMaps = g_twoDSkips = 0;
    }
}

// The call at `site` must be E8 rel32 to `target` (both RVAs) before it is redirected.
bool CallsTo(uintptr_t site, uintptr_t target)
{
    const auto* p = Module::exe_ptr<uint8_t>(site);
    return p[0] == 0xE8 && site + 5 + *reinterpret_cast<const int32_t*>(p + 1) == target;
}
}

class TripleViewsHook : public Hook
{
public:
    std::string_view description() override { return "TripleViews"; }

    bool validate() override { return Opt().on; }

    bool apply() override
    {
        if (!CallsTo(FrameRenderCall, FrameRender) || !CallsTo(TwoDCall, TwoD)) {
            spdlog::error("TripleViews: the frame render or 2D flush call is not where this executable has it; spike off");
            return false;
        }
        Memory::VP::InjectHook(Module::exe_ptr(FrameRenderCall), FrameRender_dest, Memory::HookType::Call);
        Memory::VP::InjectHook(Module::exe_ptr(TwoDCall), TwoD_dest, Memory::HookType::Call);
        spdlog::info("TripleViews: on (development spike): race frames drawn once per panel; turn {}{}", Opt().deg != 0.f ? std::to_string(Opt().deg) + " deg" : std::string("= one panel's horizontal fov"),
                     Opt().flip ? ", flipped" : "");
        return true;
    }

    static TripleViewsHook instance;
};
TripleViewsHook TripleViewsHook::instance;
}
