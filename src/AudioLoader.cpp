#include "AudioLoader.h"

#include "miniaudio.h"

namespace usersync {

bool load_audio_16k_mono(const std::string& path, PcmAudio& out, std::string& error) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 16000);
    cfg.resampling.algorithm = ma_resample_algorithm_linear;

    ma_decoder decoder;
    ma_result res = ma_decoder_init_file(path.c_str(), &cfg, &decoder);
    if (res != MA_SUCCESS) {
        error = "ma_decoder_init_file failed: ";
        error += ma_result_description(res);
        return false;
    }

    ma_uint64 total_frames = 0;
    res = ma_decoder_get_length_in_pcm_frames(&decoder, &total_frames);
    if (res != MA_SUCCESS || total_frames == 0) {
        // Some streams don't know their length up front; fall back to chunked read.
        out.samples.clear();
        std::vector<float> chunk(1u << 15);
        ma_uint64 read = 0;
        while (true) {
            res = ma_decoder_read_pcm_frames(&decoder, chunk.data(), chunk.size(), &read);
            if (read == 0) break;
            out.samples.insert(out.samples.end(), chunk.begin(), chunk.begin() + read);
            if (res != MA_SUCCESS) break;
        }
    } else {
        out.samples.assign(static_cast<size_t>(total_frames), 0.0f);
        ma_uint64 frames_read = 0;
        res = ma_decoder_read_pcm_frames(&decoder, out.samples.data(), total_frames, &frames_read);
        if (res != MA_SUCCESS && res != MA_AT_END) {
            ma_decoder_uninit(&decoder);
            error = "ma_decoder_read_pcm_frames failed: ";
            error += ma_result_description(res);
            return false;
        }
        out.samples.resize(static_cast<size_t>(frames_read));
    }

    ma_decoder_uninit(&decoder);
    out.sample_rate = 16000;
    if (out.samples.empty()) {
        error = "decoded zero samples";
        return false;
    }
    return true;
}

}  // namespace usersync
