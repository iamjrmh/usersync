#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace usersync {

// Newline-separated list of physical GPUs reported by the OS, with VRAM
// shown next to each. Detected once on first call and cached.
const std::string& detected_gpus();

// Dedicated VRAM (in bytes) of the discrete GPU with the most memory.
// Returns 0 if no GPU was detected. Cached.
uint64_t primary_gpu_vram_bytes();

// Friendly name of the primary GPU (e.g. "NVIDIA GeForce RTX 3070"). Cached.
const std::string& primary_gpu_name();

// Friendly summary of which GPU backends the build was compiled with
// ("CUDA", "Vulkan", "CPU only (no GPU backend compiled in)", ...).
const std::string& compiled_backends();

// Given the user's available GGML models (paths under "models/") and the
// detected primary GPU's VRAM, return the path of the BIGGEST model that
// will fit comfortably. Returns an empty string if `available` is empty.
std::string best_model_for_vram(uint64_t vram_bytes,
                                const std::vector<std::string>& available);

}  // namespace usersync
