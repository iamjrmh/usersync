#include "Transcriber.h"

#include "whisper.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <thread>

namespace usersync {

std::string Line::text() const {
    std::string s;
    for (size_t i = 0; i < words.size(); ++i) {
        if (i) s.push_back(' ');
        s += words[i].text;
    }
    return s;
}

namespace {

// Trim leading/trailing ASCII whitespace.
std::string trim(std::string s) {
    auto issp = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && issp(s.front())) s.erase(s.begin());
    while (!s.empty() && issp(s.back()))  s.pop_back();
    return s;
}

// Convert per-token Whisper output for one segment into discrete words by
// merging BPE pieces; a piece beginning with ' ' starts a new word.
void words_from_segment(struct whisper_context* ctx,
                        int segment_index,
                        double time_offset_seconds,
                        std::vector<Word>& out_words) {
    const int n_tokens = whisper_full_n_tokens(ctx, segment_index);
    const whisper_token id_eot = whisper_token_eot(ctx);

    Word cur;
    bool have_cur = false;

    auto flush = [&]() {
        if (!have_cur) return;
        cur.text = trim(cur.text);
        if (!cur.text.empty()) out_words.push_back(cur);
        cur = {};
        have_cur = false;
    };

    for (int j = 0; j < n_tokens; ++j) {
        const auto td = whisper_full_get_token_data(ctx, segment_index, j);
        if (td.id >= id_eot) continue;  // skip special tokens (SOT, EOT, lang, ...)

        const char* piece = whisper_token_to_str(ctx, td.id);
        if (!piece || !*piece) continue;

        // Whisper emits some non-textual markers like "[_TT_..]" etc.; skip them.
        if (piece[0] == '[' && piece[std::strlen(piece) - 1] == ']') continue;

        const bool starts_word = (piece[0] == ' ');
        if (starts_word) flush();

        const double t0 = time_offset_seconds + td.t0 * 0.01;
        const double t1 = time_offset_seconds + td.t1 * 0.01;

        if (!have_cur) {
            cur.t_start = t0;
            cur.t_end   = t1;
            cur.prob    = td.p;
            cur.text    = (starts_word ? piece + 1 : piece);
            have_cur    = true;
        } else {
            cur.text += piece;
            cur.t_end = std::max(cur.t_end, t1);
            cur.prob  = std::min(cur.prob, td.p);  // weakest-link confidence
        }
    }
    flush();
}

whisper_full_params make_params(const TranscribeOptions& opts,
                                const std::atomic<bool>& cancel,
                                const ProgressFn& on_progress,
                                const SegmentFn&  on_segment) {
    whisper_full_params p = whisper_full_default_params(
        opts.beam_size > 1 ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY);

    p.print_realtime   = false;
    p.print_progress   = false;
    p.print_timestamps = false;
    p.print_special    = false;
    p.translate        = opts.translate;
    p.language         = (opts.language == "auto" || opts.language.empty())
                             ? nullptr : opts.language.c_str();
    p.detect_language  = (p.language == nullptr);
    p.n_threads        = opts.threads > 0
                             ? opts.threads
                             : std::max(1u, std::thread::hardware_concurrency());
    p.token_timestamps = true;
    p.thold_pt         = 0.01f;
    p.max_len          = 0;
    p.split_on_word    = true;
    p.suppress_blank   = true;
    // no_context = false is the whisper default; rolling context across
    // segments improves long-form transcription consistency. transcribe_window
    // overrides this to true for prompted per-line re-decodes.
    p.no_context       = false;
    p.single_segment   = false;
    p.temperature      = 0.0f;
    p.temperature_inc  = 0.2f;

    if (opts.beam_size > 1) p.beam_search.beam_size = opts.beam_size;
    if (opts.best_of  > 1) p.greedy.best_of        = opts.best_of;

    // Cancel/progress/segment are reported via callbacks. Lambdas with captures
    // can't become C function pointers, so we route through user_data.
    struct CbState {
        const std::atomic<bool>* cancel;
        const ProgressFn*        on_progress;
        const SegmentFn*         on_segment;
    };
    static thread_local CbState s_state;
    s_state = { &cancel, &on_progress, &on_segment };

    p.encoder_begin_callback = [](whisper_context*, whisper_state*,
                                  void* ud) -> bool {
        auto* st = static_cast<CbState*>(ud);
        return !(st && st->cancel && st->cancel->load());
    };
    p.encoder_begin_callback_user_data = &s_state;

    p.progress_callback = [](whisper_context*, whisper_state*,
                             int progress, void* ud) {
        auto* st = static_cast<CbState*>(ud);
        if (st && st->on_progress && *st->on_progress) {
            (*st->on_progress)("decode", progress / 100.0f, "");
        }
    };
    p.progress_callback_user_data = &s_state;

    // Stream each newly-decoded segment to the UI so the user sees lyrics
    // appearing live. Whisper doesn't emit progress during the encoder pass,
    // which can take 10-30s on CPU before the bar moves -- this callback is
    // the user's first signal that something's actually happening.
    p.new_segment_callback = [](whisper_context* ctx, whisper_state*,
                                int n_new, void* ud) {
        auto* st = static_cast<CbState*>(ud);
        if (!st || !st->on_segment || !*st->on_segment) return;
        const int n_total = whisper_full_n_segments(ctx);
        for (int i = n_total - n_new; i < n_total; ++i) {
            Line line;
            line.t_start = whisper_full_get_segment_t0(ctx, i) * 0.01;
            line.t_end   = whisper_full_get_segment_t1(ctx, i) * 0.01;
            words_from_segment(ctx, i, /*offset=*/0.0, line.words);
            if (!line.words.empty()) (*st->on_segment)(line);
        }
    };
    p.new_segment_callback_user_data = &s_state;

    return p;
}

}  // namespace

Transcriber::~Transcriber() { unload(); }

void Transcriber::unload() {
    if (ctx_) {
        whisper_free(ctx_);
        ctx_ = nullptr;
    }
    loaded_path_.clear();
}

bool Transcriber::load_model(const std::string& path, bool use_gpu, std::string& error) {
    if (ctx_ && loaded_path_ == path) return true;
    unload();

    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu    = use_gpu;
    // DTW + flash_attn were experiments that empirically WORSEN word timing
    // on music. Restoring whisper.cpp's stock defaults: standard token-edge
    // timestamps and no flash attention. This matches the "pre-WhisperX"
    // state the user reported as best.
    cparams.flash_attn           = false;
    cparams.dtw_token_timestamps = false;

    ctx_ = whisper_init_from_file_with_params(path.c_str(), cparams);
    if (!ctx_) {
        error = "failed to load whisper model from " + path;
        return false;
    }
    loaded_path_ = path;
    return true;
}

bool Transcriber::transcribe(const PcmAudio&          audio,
                             const TranscribeOptions& opts,
                             std::vector<Line>&       out_lines,
                             std::string&             error,
                             const ProgressFn&        on_progress,
                             const SegmentFn&         on_segment,
                             const std::atomic<bool>& cancel) {
    if (!ctx_) {
        if (!load_model(opts.model_path, opts.use_gpu, error)) return false;
    }
    if (audio.samples.empty()) { error = "empty audio"; return false; }

    if (on_progress) on_progress("decode", 0.0f, "running whisper");

    auto p = make_params(opts, cancel, on_progress, on_segment);

    int rc = whisper_full(ctx_, p,
                          audio.samples.data(),
                          static_cast<int>(audio.samples.size()));
    if (rc != 0) {
        error = "whisper_full failed (rc=" + std::to_string(rc) + ")";
        return false;
    }

    out_lines.clear();
    const int n_segments = whisper_full_n_segments(ctx_);
    out_lines.reserve(n_segments);
    for (int i = 0; i < n_segments; ++i) {
        Line line;
        line.t_start = whisper_full_get_segment_t0(ctx_, i) * 0.01;
        line.t_end   = whisper_full_get_segment_t1(ctx_, i) * 0.01;
        words_from_segment(ctx_, i, /*offset=*/0.0, line.words);
        if (!line.words.empty()) out_lines.push_back(std::move(line));
    }
    return true;
}

bool Transcriber::transcribe_window(const PcmAudio&          audio,
                                    size_t                   begin,
                                    size_t                   end,
                                    const TranscribeOptions& opts,
                                    const std::string&       initial_prompt,
                                    std::vector<Word>&       out_words,
                                    std::string&             error) {
    if (!ctx_) { error = "model not loaded"; return false; }
    if (end <= begin || end > audio.samples.size()) { error = "invalid window"; return false; }

    static const std::atomic<bool> never_cancel{false};
    static const ProgressFn no_progress;
    static const SegmentFn  no_segment;
    auto p = make_params(opts, never_cancel, no_progress, no_segment);
    p.no_context = true;
    if (!initial_prompt.empty()) p.initial_prompt = initial_prompt.c_str();

    int rc = whisper_full(ctx_, p,
                          audio.samples.data() + begin,
                          static_cast<int>(end - begin));
    if (rc != 0) {
        error = "whisper_full (window) failed (rc=" + std::to_string(rc) + ")";
        return false;
    }

    const double offset = static_cast<double>(begin) / audio.sample_rate;
    out_words.clear();
    const int n_segments = whisper_full_n_segments(ctx_);
    for (int i = 0; i < n_segments; ++i) {
        words_from_segment(ctx_, i, offset, out_words);
    }
    return true;
}

}  // namespace usersync
