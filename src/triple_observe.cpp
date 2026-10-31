// Dev-only, read-only observation for the three-view spike (docs/2026-10-30-triple-views-and-clean-launch-plan.md,
// step 2). With DBCE_OUTRUN_TRIPLE_OBSERVE set at process start, every 600 frames the log says where the camera's
// matrices are computed (CalcCameraMatrix 0x484BD0, seen through FixZBufferPrecision's hook) relative to the frame
// render 0x449050, its 3D layer loop (frame begin 0x448DB0 .. frame end 0x448E40) and the 2D flush 0x42D710, with the
// main camera's projection fields and the viewports around the layers. Nothing is written to game memory.
#include "hook_mgr.hpp"
#include "plugin.hpp"
#include "game_addrs.hpp"
#include "triple_observe.hpp"
#include <map>
#include <mutex>
#include <cstdio>
#include <string>
#include <tuple>

namespace TripleObserve {
namespace {
enum Phase : int { Outside, Render, Layers, AfterLayers, TwoD };
const char* const PhaseName[] = {"before the first frame", "render, before the layers", "3D layers", "after the layers",
                                 "after the 2D flush, until the next frame"};
constexpr uintptr_t MainCamera = 0x39FE10, LayerIndex = 0x3D25F0;   // VA 0x7D25F0: the layer 449050 is drawing
constexpr unsigned ReportFrames = 600;
std::mutex g_mutex;
Phase g_phase = Outside;
unsigned g_frames = 0;
std::map<std::tuple<int, uintptr_t, bool, int>, unsigned> g_calls;   // phase, caller RVA (0: the mod), main camera, layer
D3DVIEWPORT9 g_begin{}, g_end{};

uintptr_t Rva(const void* p)
{
    const auto base = reinterpret_cast<uintptr_t>(Module::exe_ptr(0));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto a = reinterpret_cast<uintptr_t>(p);
    return a >= base && a < base + nt->OptionalHeader.SizeOfImage ? a - base : 0;
}

std::string Hex(uintptr_t v) { char b[24]; std::snprintf(b, sizeof b, "RVA 0x%X", unsigned(v)); return b; }

void Report()
{
    const float* c = reinterpret_cast<const float*>(Module::exe_ptr(MainCamera));
    spdlog::info("TripleObserve: {} frames; main camera fov +A0 {:.4f}, aspect +B8 {:.4f}, near/far +BC/+C0 {:.3f}/{:.1f}, "
                 "shift +C4/+C8 {:.4f}/{:.4f}; viewport at frame begin {}x{}+{}+{}, at frame end {}x{}+{}+{}",
                 ReportFrames, c[0xA0 / 4], c[0xB8 / 4], c[0xBC / 4], c[0xC0 / 4], c[0xC4 / 4], c[0xC8 / 4],
                 g_begin.Width, g_begin.Height, g_begin.X, g_begin.Y, g_end.Width, g_end.Height, g_end.X, g_end.Y);
    for (const auto& [k, n] : g_calls) {
        const auto& [phase, rva, main, layer] = k;
        spdlog::info("TripleObserve:   CalcCameraMatrix {:.2f}/frame, {}, caller {}, {}{}", double(n) / ReportFrames,
                     PhaseName[phase], rva ? Hex(rva) : std::string("the mod"),
                     main ? "main camera" : "other camera", layer >= 0 ? ", layer " + std::to_string(layer) : std::string());
    }
    g_calls.clear();
}

void OnRender(SafetyHookContext&)
{
    std::lock_guard<std::mutex> g(g_mutex);
    if (++g_frames % ReportFrames == 0) Report();
    g_phase = Render;
}
void OnBegin(SafetyHookContext&)
{
    std::lock_guard<std::mutex> g(g_mutex);
    g_phase = Layers;
    if (auto* d = Game::D3DDevice()) d->GetViewport(&g_begin);
}
void OnEnd(SafetyHookContext&)
{
    std::lock_guard<std::mutex> g(g_mutex);
    g_phase = AfterLayers;
    if (auto* d = Game::D3DDevice()) d->GetViewport(&g_end);
}
void OnTwoD(SafetyHookContext&)
{
    std::lock_guard<std::mutex> g(g_mutex);
    if (g_phase == AfterLayers) g_phase = TwoD;   // 42D710 runs elsewhere too; only the one after the layers counts
}
}

bool On()
{
    static const bool on = GetEnvironmentVariableW(L"DBCE_OUTRUN_TRIPLE_OBSERVE", nullptr, 0) > 0;
    return on;
}

void Camera(const void* camera, const void* returnAddress)
{
    std::lock_guard<std::mutex> g(g_mutex);
    const int layer = g_phase == Layers ? int(*Module::exe_ptr<uint32_t>(LayerIndex)) : -1;
    ++g_calls[{g_phase, Rva(returnAddress), camera == Module::exe_ptr(MainCamera), layer}];
}
}

class TripleObserveHook : public Hook
{
    inline static SafetyHookMid render = {}, begin = {}, end = {}, twoD = {};

public:
    std::string_view description() override { return "TripleObserve"; }

    bool validate() override { return TripleObserve::On(); }

    bool apply() override
    {
        render = safetyhook::create_mid(Module::exe_ptr(0x49050), TripleObserve::OnRender);
        begin = safetyhook::create_mid(Module::exe_ptr(0x48DB0), TripleObserve::OnBegin);
        end = safetyhook::create_mid(Module::exe_ptr(0x48E40), TripleObserve::OnEnd);
        twoD = safetyhook::create_mid(Module::exe_ptr(0x2D710), TripleObserve::OnTwoD);
        spdlog::info("TripleObserve: on (read-only); camera calls are seen only while FixZBufferPrecision is on ({})",
                     Settings::FixZBufferPrecision ? "on" : "OFF: none will be counted");
        return render && begin && end && twoD;
    }

    static TripleObserveHook instance;
};
TripleObserveHook TripleObserveHook::instance;
