#pragma once

#include "Transcriber.h"   // for ProgressFn

#include <atomic>
#include <string>
#include <vector>

namespace usersync {

struct SplitJob {
    std::string audio_path;
    std::string out_dir;
    std::string model       = "htdemucs";   // htdemucs / htdemucs_ft / htdemucs_6s / mdx_extra / mdx
    std::string stems_mode  = "all";        // "all" or "vocals" (= 2-stem vocals + no_vocals)
    std::string format      = "wav";        // wav / mp3 / flac
    int         mp3_bitrate = 320;
    int         shifts      = 1;            // 0..10 -- more = higher SDR, slower
    float       overlap     = 0.25f;        // 0.0..0.5
    bool        use_gpu     = true;
};

struct SplitResult {
    bool                     ok = false;
    std::string              out_dir;       // where the stems landed
    std::vector<std::string> stems;         // absolute paths of produced stem files
    std::string              error;
};

// Synchronous; spawns the venv's Python to run scripts/split.py and streams
// progress / log events back through `on_progress`. `cancel` is polled
// between subprocess output reads.
SplitResult run_split(const SplitJob&          job,
                      const ProgressFn&        on_progress,
                      const std::atomic<bool>& cancel);

}  // namespace usersync
