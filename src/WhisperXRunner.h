#pragma once

#include "Transcriber.h"

#include <atomic>
#include <string>
#include <vector>

namespace usersync {

struct WhisperXStatus {
    bool        python_found        = false;
    bool        whisperx_installed  = false;
    std::string python_exe;            // path to python.exe if found
    std::string whisperx_version;      // populated if whisperx_installed
    std::string error;                 // populated on detection failure
};

// One-time-cached detection of Python + WhisperX. Reads `python --version`
// and `python -c "import whisperx; print(whisperx.__version__)"`.
const WhisperXStatus& whisperx_status();

// Re-runs detection (e.g. after the user installs WhisperX). Updates cache.
void refresh_whisperx_status();

// Run WhisperX (wav2vec2 forced alignment) on `audio_path` with the user's
// lyrics. Returns line-grouped, word-timestamped output -- typically
// 20-50ms word accuracy. Returns empty on failure (see `error_out`).
//
// `audio_path` is the ORIGINAL audio file (not the resampled PcmAudio) --
// WhisperX handles its own decoding.
struct WhisperXJob {
    std::string audio_path;
    std::string user_lyrics;
    std::string language;       // "en", "auto", ...
    std::string output_path;    // where Python writes the .lrc
    std::string title;
    std::string artist;
    std::string album;
    bool        use_gpu      = true;
    bool        split_vocals = false;  // run Demucs first, align against vocals stem
};

std::vector<Line> run_whisperx(const WhisperXJob&        job,
                               const ProgressFn&         on_progress,
                               const SegmentFn&          on_segment,   // streams [SEG] live
                               const std::atomic<bool>&  cancel,
                               std::string&              error_out);

}  // namespace usersync
