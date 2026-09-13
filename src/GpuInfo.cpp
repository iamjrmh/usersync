#include "GpuInfo.h"

#ifdef _WIN32
#include <windows.h>
#include <dxgi.h>
#pragma comment(lib, "dxgi.lib")
#endif

#include <vector>

namespace usersync {

namespace {

struct GpuRow { std::string name; uint64_t vram = 0; };

std::vector<GpuRow> enum_gpus_impl() {
    std::vector<GpuRow> out;
#ifdef _WIN32
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory) return out;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))
            && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            char name[256] = {0};
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                                name, sizeof(name), nullptr, nullptr);
            GpuRow row;
            row.name = name;
            row.vram = static_cast<uint64_t>(desc.DedicatedVideoMemory);
            out.push_back(std::move(row));
        }
        adapter->Release();
    }
    factory->Release();
#endif
    return out;
}

const std::vector<GpuRow>& enum_gpus_cached() {
    static const std::vector<GpuRow> s = enum_gpus_impl();
    return s;
}

std::string detect_gpus_impl() {
    const auto& rows = enum_gpus_cached();
    if (rows.empty()) {
#ifdef _WIN32
        return "(none detected)";
#else
        return "(GPU detection only implemented on Windows)";
#endif
    }
    std::string out;
    for (const auto& r : rows) {
        if (!out.empty()) out.push_back('\n');
        char vram[32];
        std::snprintf(vram, sizeof(vram), "%.1f GB", r.vram / 1073741824.0);
        out += "  ";
        out += r.name;
        out += "  (";
        out += vram;
        out += " VRAM)";
    }
    return out;
}

std::string compiled_backends_impl() {
    // whisper.cpp v1.7.x's whisper_print_system_info() doesn't surface GPU
    // backend availability in its string. We rely on build-time defines
    // injected by CMakeLists.txt (USERSYNC_BUILT_WITH_CUDA / _VULKAN / _METAL)
    // so the answer is always correct for what was actually compiled in.
    std::vector<const char*> active;
#ifdef USERSYNC_BUILT_WITH_CUDA
    active.push_back("CUDA");
#endif
#ifdef USERSYNC_BUILT_WITH_VULKAN
    active.push_back("Vulkan");
#endif
#ifdef USERSYNC_BUILT_WITH_METAL
    active.push_back("Metal");
#endif

    if (active.empty()) return "CPU only (no GPU backend compiled in)";
    std::string out;
    for (size_t i = 0; i < active.size(); ++i) {
        if (i) out += ", ";
        out += active[i];
    }
    return out;
}

}  // namespace

const std::string& detected_gpus() {
    static const std::string s = detect_gpus_impl();
    return s;
}

uint64_t primary_gpu_vram_bytes() {
    const auto& rows = enum_gpus_cached();
    uint64_t best = 0;
    for (const auto& r : rows) if (r.vram > best) best = r.vram;
    return best;
}

const std::string& primary_gpu_name() {
    static const std::string s = [&]{
        const auto& rows = enum_gpus_cached();
        const GpuRow* best = nullptr;
        for (const auto& r : rows) if (!best || r.vram > best->vram) best = &r;
        return best ? best->name : std::string{};
    }();
    return s;
}

const std::string& compiled_backends() {
    static const std::string s = compiled_backends_impl();
    return s;
}

std::string best_model_for_vram(uint64_t vram_bytes,
                                const std::vector<std::string>& available) {
    if (available.empty()) return {};

    // Tiers: each entry says "if the GPU has at least min_gb VRAM, you can
    // run this model comfortably (model size + activations + DTW memory +
    // GPU overhead)." Walk biggest -> smallest, pick the first that both
    // fits AND is downloaded.
    // usersync now only blesses one model: full-precision large-v3.
    // It empirically produces the only consistently good lyric sync.
    struct Tier { double min_gb; const char* model; };
    static const Tier tiers[] = {
        { 0.0, "models/ggml-large-v3.bin" },
    };
    const double gb = vram_bytes / 1073741824.0;
    for (const auto& t : tiers) {
        if (gb < t.min_gb) continue;
        for (const auto& a : available) {
            if (a == t.model) return a;
        }
    }
    // Nothing in the tier list matched what's downloaded -- fall back to
    // whatever the user has (alphabetical first, which sorts roughly by
    // size for the ggml-* names).
    return available.front();
}

}  // namespace usersync
