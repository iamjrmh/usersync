#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace usersync {

// Decoded audio in the exact shape whisper.cpp expects: 16 kHz, mono, f32 [-1, 1].
struct PcmAudio {
    std::vector<float> samples;
    uint32_t sample_rate = 16000;
    double duration_seconds() const {
        return sample_rate ? static_cast<double>(samples.size()) / sample_rate : 0.0;
    }
};

// Decode any miniaudio-supported file (mp3/wav/flac/ogg) and resample/downmix
// to 16 kHz mono f32. Returns false and fills `error` on failure.
bool load_audio_16k_mono(const std::string& path, PcmAudio& out, std::string& error);

}  // namespace usersync
