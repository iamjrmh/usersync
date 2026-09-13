#include "App.h"

#include "Aligner.h"
#include "ForcedAligner.h"
#include "IterativeAligner.h"
#include "WhisperXRunner.h"

#include <fstream>
#include <sstream>
#include <utility>

namespace usersync {

App::App() = default;

App::~App() {
    cancel_job();
    if (worker_.joinable()) worker_.join();
}

JobStatus App::status() const {
    std::lock_guard<std::mutex> lk(mu_);
    return status_;
}

std::vector<Line> App::snapshot_lines() const {
    std::lock_guard<std::mutex> lk(mu_);
    return lines_;
}

std::string App::last_lrc_path() const {
    std::lock_guard<std::mutex> lk(mu_);
    return last_lrc_path_;
}

void App::apply_edited_lines(std::vector<Line> edited) {
    std::lock_guard<std::mutex> lk(mu_);
    lines_    = std::move(edited);
    // Clearing forces last_lrc_preview() to re-render from lines_ live,
    // which is what makes the Output tab's LRC text update as the user
    // nudges words in the Editor.
    last_lrc_.clear();
}

void App::append_log(std::string msg) {
    std::lock_guard<std::mutex> lk(mu_);
    log_buffer_.push_back(std::move(msg));
    // Cap so a runaway script doesn't OOM us.
    if (log_buffer_.size() > 5000) {
        log_buffer_.erase(log_buffer_.begin(),
                          log_buffer_.begin() + (log_buffer_.size() - 5000));
    }
}

std::vector<std::string> App::drain_log_buffer() {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> out;
    out.swap(log_buffer_);
    return out;
}

std::string App::last_lrc_preview(size_t max_chars) const {
    std::lock_guard<std::mutex> lk(mu_);
    // Job finished -> show the saved file.
    if (!last_lrc_.empty()) {
        if (last_lrc_.size() <= max_chars) return last_lrc_;
        return last_lrc_.substr(0, max_chars) + "\n... [truncated]";
    }
    // Job in flight -> render whatever lines we've received so far so the
    // user can watch the LRC build up in real time.
    if (lines_.empty()) return {};
    const std::string live = format_enhanced_lrc(lines_, LrcMetadata{});
    if (live.size() <= max_chars) return live;
    return live.substr(0, max_chars) + "\n... [streaming]";
}

bool App::job_in_flight() const { return running_.load(); }

void App::cancel_job() {
    if (running_.load()) cancel_.store(true);
}

void App::start_job(const std::string&       audio_path,
                    const std::string&       lrc_out_path,
                    const TranscribeOptions& opts,
                    const LrcMetadata&       meta,
                    const std::string&       user_lyrics,
                    bool                     use_whisperx) {
    if (running_.load()) return;
    if (worker_.joinable()) worker_.join();

    cancel_.store(false);
    running_.store(true);
    {
        std::lock_guard<std::mutex> lk(mu_);
        status_ = {};
        status_.state    = JobState::Running;
        status_.phase    = "queued";
        status_.fraction = 0.0f;
        lines_.clear();
        last_lrc_.clear();
        last_lrc_path_.clear();
    }

    worker_ = std::thread(&App::worker, this,
                          audio_path, lrc_out_path, opts, meta, user_lyrics, use_whisperx);
}

void App::worker(std::string audio_path,
                 std::string lrc_out_path,
                 TranscribeOptions opts,
                 LrcMetadata meta,
                 std::string user_lyrics,
                 bool        use_whisperx) {
    const auto t_start = std::chrono::steady_clock::now();

    auto set_phase = [&](const char* phase, float frac, const std::string& msg) {
        std::lock_guard<std::mutex> lk(mu_);
        status_.phase    = phase;
        status_.fraction = frac;
        // Don't clobber the streaming "N lines decoded" message with the
        // empty string the progress callback keeps sending.
        if (!msg.empty()) status_.message = msg;
        status_.elapsed_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
    };

    auto fail = [&](const std::string& err) {
        std::lock_guard<std::mutex> lk(mu_);
        status_.state = JobState::Error;
        status_.error = err;
        status_.elapsed_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
        running_.store(false);
    };

    // ------------------------------------------------------------------
    // WhisperX path (Python sidecar): skips whisper.cpp entirely, hands
    // the audio + lyrics to wav2vec2 forced alignment for ~20ms word
    // accuracy. Only useful when the user actually provided lyrics.
    // ------------------------------------------------------------------
    if (use_whisperx && !user_lyrics.empty()) {
        set_phase("whisperx", 0.0f, "running WhisperX (wav2vec2 forced alignment)");
        std::string wx_err;
        auto on_prog0 = [&](const char* phase, float f, const std::string& m) {
            std::lock_guard<std::mutex> lk(mu_);
            status_.phase = phase;
            if (f >= 0.0f) status_.fraction = f;
            if (!m.empty()) {
                status_.message = m;
                // Also queue to the drain buffer so the Log tab catches
                // every message, not just the latest-per-frame.
                log_buffer_.push_back(m);
            }
            status_.elapsed_seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
        };
        WhisperXJob wjob;
        wjob.audio_path   = audio_path;
        wjob.user_lyrics  = user_lyrics;
        wjob.language     = (opts.language == "auto" || opts.language.empty())
                              ? "en" : opts.language;
        wjob.output_path  = lrc_out_path;
        wjob.title        = meta.title;
        wjob.artist       = meta.artist;
        wjob.album        = meta.album;
        wjob.use_gpu      = opts.use_gpu;
        wjob.split_vocals = opts.split_vocals;
        // Stream each [SEG] event into lines_ so the Preview tab shows
        // whisper hearing the song in real time.
        auto on_seg0 = [&](const Line& line) {
            std::lock_guard<std::mutex> lk(mu_);
            lines_.push_back(line);
            status_.message = std::to_string(lines_.size()) + " segments transcribed";
        };
        auto wx_lines = run_whisperx(wjob, on_prog0, on_seg0, cancel_, wx_err);
        if (cancel_.load()) {
            std::lock_guard<std::mutex> lk(mu_);
            status_.state = JobState::Cancelled;
            running_.store(false);
            return;
        }
        if (wx_lines.empty()) {
            fail("WhisperX failed: " + wx_err);
            return;
        }
        // Trim the final word's end so trailing silence/outro doesn't drag
        // the closing tag deep into the song. Re-emit the LRC so the saved
        // file matches the in-memory representation.
        tighten_last_word_end(wx_lines);
        const std::string lrc_text = format_enhanced_lrc(wx_lines, meta);
        write_file(lrc_out_path, lrc_text, /*error*/ wx_err);
        {
            std::lock_guard<std::mutex> lk(mu_);
            lines_         = std::move(wx_lines);
            last_lrc_      = lrc_text;
            last_lrc_path_ = lrc_out_path;
            status_.state    = JobState::Done;
            status_.phase    = "done (WhisperX)";
            status_.fraction = 1.0f;
            status_.message  = "wrote " + lrc_out_path + " via WhisperX";
            status_.elapsed_seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
        }
        running_.store(false);
        return;
    }

    set_phase("load", 0.0f, "decoding audio");
    PcmAudio audio;
    std::string err;
    if (!load_audio_16k_mono(audio_path, audio, err)) { fail("audio: " + err); return; }
    if (cancel_.load()) {
        std::lock_guard<std::mutex> lk(mu_);
        status_.state = JobState::Cancelled;
        running_.store(false);
        return;
    }

    set_phase("load", 0.5f, "loading model");
    if (!transcriber_.load_model(opts.model_path, opts.use_gpu, err)) {
        fail("model: " + err);
        return;
    }

    set_phase("decode", 0.0f, "transcribing");
    auto on_prog = [&](const char* phase, float f, const std::string& m) {
        set_phase(phase, f, m);
    };
    // Push each new segment into the shared lines_ buffer so the GUI sees
    // lyrics appear as they're decoded, instead of all-at-once at the end.
    auto on_seg = [&](const Line& line) {
        std::lock_guard<std::mutex> lk(mu_);
        lines_.push_back(line);
        status_.message = std::to_string(lines_.size()) + " lines decoded";
    };

    std::vector<Line> lines;
    if (!transcriber_.transcribe(audio, opts, lines, err, on_prog, on_seg, cancel_)) {
        if (cancel_.load()) {
            std::lock_guard<std::mutex> lk(mu_);
            status_.state = JobState::Cancelled;
            running_.store(false);
            return;
        }
        fail("transcribe: " + err);
        return;
    }

    if (opts.refine_passes > 1) {
        set_phase("refine", 0.0f, "alignment refinement");
        if (!refine_alignment(transcriber_, audio, opts,
                              opts.refine_passes - 1, lines, on_prog, cancel_)) {
            if (cancel_.load()) {
                std::lock_guard<std::mutex> lk(mu_);
                status_.state = JobState::Cancelled;
                running_.store(false);
                return;
            }
        }
    }

    // Count how many anchor words whisper actually produced. Without
    // anchors the aligner has nothing to work with, so we have to fail loud
    // instead of writing a .lrc with garbage timestamps.
    size_t whisper_word_count = 0;
    for (const auto& l : lines) whisper_word_count += l.words.size();
    set_phase(opts.refine_passes > 1 ? "refine" : "decode", 1.0f,
              "whisper found " + std::to_string(whisper_word_count) +
              " words across " + std::to_string(lines.size()) + " segments");

    // User-supplied lyrics override whisper's transcription. We keep
    // whisper's word-level timestamps as the anchor grid and snap the user's
    // words onto them via Needleman-Wunsch alignment.
    if (!user_lyrics.empty()) {
        if (whisper_word_count == 0) {
            fail("Forced alignment failed: whisper produced 0 word timestamps "
                 "from this audio, so there's nothing to align your lyrics "
                 "against.\n\n"
                 "Common causes:\n"
                 "  - The audio is mostly silent / music-only (no vocals "
                 "detected).\n"
                 "  - Wrong language. Try setting Language explicitly "
                 "(e.g. en, ja, es) instead of 'auto'.\n"
                 "  - The selected model is too small for music. Use "
                 "ggml-large-v3-turbo.bin or larger.\n"
                 "  - GPU offload silently failed -- try unchecking 'GPU' "
                 "to force CPU and see if that produces output.");
            return;
        }
        set_phase("align", 0.0f,
                  "aligning user lyrics to " +
                  std::to_string(whisper_word_count) + " whisper anchors");
        auto aligned = align_text_to_whisper(user_lyrics, lines,
                                             audio.duration_seconds());
        if (aligned.empty()) {
            fail("Forced alignment returned no lines. Check the lyrics text "
                 "is not empty.");
            return;
        }
        lines = std::move(aligned);

        // The HUGE accuracy win: per-line re-decode with the line's text as
        // a prompt. Whisper stops fighting with "what words" and only solves
        // "when". Each iteration shrinks the audio window around the
        // previous best timestamps. 10+ iterations typically converges to
        // sub-100ms word accuracy.
        if (opts.iterative_align_passes > 0) {
            set_phase("iter", 0.0f,
                      "iterative refinement (" +
                      std::to_string(opts.iterative_align_passes) + " passes)");
            auto refined = iterative_align(transcriber_, audio, opts, lines,
                                           opts.iterative_align_passes,
                                           on_prog, cancel_);
            if (cancel_.load()) {
                std::lock_guard<std::mutex> lk(mu_);
                status_.state = JobState::Cancelled;
                running_.store(false);
                return;
            }
            if (!refined.empty()) lines = std::move(refined);
        }
    } else if (whisper_word_count == 0) {
        fail("Whisper produced 0 word timestamps from this audio.\n\n"
             "Likely the model couldn't transcribe the vocals. Try:\n"
             "  - A bigger model (ggml-large-v3-turbo.bin)\n"
             "  - Setting Language explicitly instead of 'auto'\n"
             "  - Toggling GPU off to test on CPU\n"
             "  - Checking the audio actually has audible vocals");
        return;
    }

    // Cap the last word's t_end so the closing tag isn't dragged into
    // post-vocal silence (the most visible artifact of whisper's
    // segment-end timestamps on the final segment).
    tighten_last_word_end(lines);
    const std::string lrc = format_enhanced_lrc(lines, meta);
    if (!write_file(lrc_out_path, lrc, err)) { fail("write: " + err); return; }

    {
        std::lock_guard<std::mutex> lk(mu_);
        lines_         = std::move(lines);
        last_lrc_      = lrc;
        last_lrc_path_ = lrc_out_path;
        status_.state    = JobState::Done;
        status_.phase    = "done";
        status_.fraction = 1.0f;
        status_.message  = "wrote " + lrc_out_path;
        status_.elapsed_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
    }
    running_.store(false);
}

}  // namespace usersync
