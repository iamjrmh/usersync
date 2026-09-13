#pragma once

#include "AudioLoader.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Forward-declare the whisper.cpp types at GLOBAL scope. If we forward-declare
// them inside `namespace usersync` (or rely on an elaborated `struct` use), the
// compiler creates `usersync::whisper_context`, which then refuses to convert
// to `::whisper_context` from <whisper.h> in the .cpp.
struct whisper_context;
struct whisper_state;

namespace usersync {

struct Word {
    std::string text;     // already trimmed, no leading space
    double t_start = 0;   // seconds
    double t_end   = 0;   // seconds
    float  prob    = 0;   // 0..1 model confidence
};

struct Line {
    std::vector<Word> words;
    double t_start = 0;
    double t_end   = 0;
    std::string text() const;
};

struct TranscribeOptions {
    std::string model_path = "models/ggml-large-v3-turbo.bin";
    std::string language   = "auto";   // "en", "ja", "auto", ...
    int         threads    = 0;        // 0 = auto (hardware_concurrency)
    bool        translate  = false;
    int         beam_size  = 1;        // 1 = greedy (fastest); >1 = beam search
    int         best_of    = 1;
    // Accuracy passes: 1 = fast one-shot; >1 enables overlap + consensus
    // refinement (see Aligner). Practical sweet-spot: 1..5.
    int         refine_passes = 1;
    // Iterative per-line re-decode with prompting (forced alignment via
    // biased decoding). 0 disables; 10+ for "perfection" mode. Only applied
    // when user lyrics are provided.
    int         iterative_align_passes = 0;
    bool        use_gpu      = true;
    // When use_whisperx is also on, run Demucs to isolate the vocal stem
    // first, then align against that. Massive accuracy boost on music.
    bool        split_vocals = false;
};

// Progress callback: phase ∈ ["load", "decode", "refine"], 0..1 fraction.
using ProgressFn = std::function<void(const char* phase, float fraction, const std::string& msg)>;

// Fired once for each NEW segment produced during streaming decoding. Lets the
// UI display words as soon as they're transcribed instead of waiting for the
// whole song to finish. Called from the worker thread.
using SegmentFn  = std::function<void(const Line& line)>;

class Transcriber {
public:
    Transcriber() = default;
    ~Transcriber();

    Transcriber(const Transcriber&) = delete;
    Transcriber& operator=(const Transcriber&) = delete;

    bool load_model(const std::string& path, bool use_gpu, std::string& error);
    void unload();

    // Run transcription on `audio`. Returns line-grouped, word-timestamped output.
    // `cancel` is polled between segments; set it true to abort early.
    bool transcribe(const PcmAudio&            audio,
                    const TranscribeOptions&   opts,
                    std::vector<Line>&         out_lines,
                    std::string&               error,
                    const ProgressFn&          on_progress,
                    const SegmentFn&           on_segment,
                    const std::atomic<bool>&   cancel);

    // Lower-level helper used by Aligner for window-scoped refinement passes.
    // Transcribes a sample range [begin, end) and returns words with absolute
    // timestamps (offset by begin / sample_rate).
    bool transcribe_window(const PcmAudio&          audio,
                           size_t                   begin,
                           size_t                   end,
                           const TranscribeOptions& opts,
                           const std::string&       initial_prompt,
                           std::vector<Word>&       out_words,
                           std::string&             error);

    bool is_loaded() const { return ctx_ != nullptr; }
    const std::string& loaded_path() const { return loaded_path_; }

private:
    whisper_context* ctx_ = nullptr;
    std::string loaded_path_;
};

}  // namespace usersync
