#pragma once

#include "Aligner.h"
#include "AudioLoader.h"
#include "LrcWriter.h"
#include "Transcriber.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace usersync {

enum class JobState { Idle, Running, Done, Error, Cancelled };

struct JobStatus {
    JobState   state    = JobState::Idle;
    std::string phase   = "idle";
    float       fraction = 0.0f;
    std::string message;
    std::string error;
    double      elapsed_seconds = 0.0;
};

// Owns long-lived state (the Whisper model + worker thread). UI reads
// snapshots via thread-safe accessors; the worker writes via the same mutex.
class App {
public:
    App();
    ~App();

    void start_job(const std::string&        audio_path,
                   const std::string&        lrc_out_path,
                   const TranscribeOptions&  opts,
                   const LrcMetadata&        meta,
                   const std::string&        user_lyrics = {},
                   bool                      use_whisperx = false);
    void cancel_job();

    JobStatus       status() const;
    std::vector<Line> snapshot_lines() const;
    std::string     last_lrc_path() const;
    // Replace the in-memory lines with whatever the manual Editor currently
    // shows, and clear the cached saved-LRC so last_lrc_preview() re-renders
    // from the edited lines on the next call. This is what lets the Preview
    // and Output tabs reflect editor nudges in real time.
    void            apply_edited_lines(std::vector<Line> edited);
    // Returns the saved LRC if the job is finished, or an on-the-fly LRC
    // rendered from the live `lines_` buffer while the job is still running.
    std::string     last_lrc_preview(size_t max_chars = 4096) const;
    // Append a free-form log line from anywhere (worker thread, callbacks).
    // The Gui drains this each frame so messages firing faster than the UI
    // refresh rate don't get lost.
    void                     append_log(std::string msg);
    std::vector<std::string> drain_log_buffer();

    bool job_in_flight() const;

private:
    void worker(std::string audio_path,
                std::string lrc_out_path,
                TranscribeOptions opts,
                LrcMetadata meta,
                std::string user_lyrics,
                bool        use_whisperx);

    mutable std::mutex       mu_;
    JobStatus                status_;
    std::vector<Line>        lines_;
    std::string              last_lrc_path_;
    std::string              last_lrc_;
    std::vector<std::string> log_buffer_;
    std::atomic<bool>       cancel_{false};
    std::atomic<bool>       running_{false};
    std::thread             worker_;
    Transcriber             transcriber_;
};

}  // namespace usersync
