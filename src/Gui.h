#pragma once

#include "App.h"
#include "AudioPlayer.h"
#include "ModelDownloader.h"
#include "SplitRunner.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct GLFWwindow;

namespace usersync {

// Owns ImGui state and the per-frame UI for the app. Visual language blends
// After Effects' dark, panel-based chrome with Instagram's signature gradient
// applied to brand surfaces (title bar, primary actions, progress fill).
class Gui {
public:
    explicit Gui(App& app);

    void init(GLFWwindow* window);
    void shutdown();
    void draw();          // call once per frame between NewFrame and Render

private:
    void apply_style();
    void draw_title_bar();
    void draw_preset_bar();
    void draw_footer();

    // Top-level tabs (full window width each).
    void draw_setup_tab();
    void draw_split_tab();
    void draw_quality_tab();
    void draw_lyrics_tab();
    void draw_preview_tab();
    void draw_editor_tab();
    void draw_output_tab();
    void draw_models_tab();
    void draw_log_tab();

    void start_split_job();

    void apply_preset(int index);
    void scan_models();
    void sync_player_to_audio_path();

    void poll_log();              // each frame, copy new status messages into log_
    void editor_pull_from_app();  // load current alignment into editor_lines_
    bool editor_save_to_disk(std::string& err);
    void editor_play_at(double t_sec);

    // Project save/load -- captures audio path + lyrics + edited line
    // timings so a song can be reopened later for further finetuning.
    bool save_project(const std::string& path, std::string& err) const;
    bool load_project(const std::string& path, std::string& err);

    App& app_;

    // Form state
    char audio_path_buf_[1024]{};
    char out_path_buf_[1024]{};
    char model_path_buf_[1024]{"models/ggml-large-v3-turbo.bin"};
    char language_buf_[16]{"auto"};
    char title_buf_[256]{};
    char artist_buf_[256]{};
    char album_buf_[256]{};

    int  threads_                = 0;
    int  beam_size_              = 1;
    int  best_of_                = 1;
    int  refine_passes_          = 1;
    int  iterative_align_passes_ = 0;   // per-line forced alignment iterations
    bool use_gpu_                = true;
    bool translate_              = false;

    // Detected ggml-*.bin files under models/.
    std::vector<std::string> available_models_;
    int                      selected_model_idx_ = -1;

    // User-supplied lyrics (required -- whisper-only path was removed).
    // Resizable so a whole song fits.
    std::string user_lyrics_;
    // Route through Python WhisperX (wav2vec2 forced alignment) instead of
    // whisper.cpp -- much more accurate word timing. Requires Python + the
    // `whisperx` pip package installed.
    bool        use_whisperx_    = false;
    // (was: bool split_vocals_ -- replaced by the dedicated Split tab)

    // Sync-preview audio playback.
    AudioPlayer player_;
    std::string player_load_error_;
    bool        autoscroll_lyrics_ = true;

    // Background model downloader.
    ModelDownloader downloader_;

    // Log history -- accumulated phase/message/error lines, shown in the
    // Log tab so long error text isn't truncated by the footer.
    struct LogLine { std::string ts; std::string level; std::string text; };
    std::vector<LogLine> log_;
    std::string          log_last_phase_;
    std::string          log_last_message_;
    std::string          log_last_error_;

    // Manual editor state.
    std::vector<Line>  editor_lines_;
    std::vector<bool>  editor_line_expanded_;   // per-line collapse state
    int                editor_loaded_seq_ = -1;
    char               editor_out_path_[1024]{};

    // Last-used project file path -- pre-fills the save dialog so re-saving
    // doesn't make the user re-pick a location every time.
    std::string        project_path_;

    // Split tab state.
    char        split_audio_buf_[1024]{};
    char        split_outdir_buf_[1024]{};
    int         split_model_idx_      = 0;     // index into kSplitModels
    int         split_stems_idx_      = 0;     // 0 = all, 1 = vocals (2-stem)
    int         split_format_idx_     = 0;     // 0 = wav, 1 = mp3, 2 = flac
    int         split_shifts_         = 1;
    float       split_overlap_        = 0.25f;
    int         split_mp3_br_         = 320;
    bool        split_use_gpu_        = true;
    bool        split_auto_promote_   = true;   // auto-set vocals stem as Setup audio

    // Split worker (separate from the App's main worker so the user can
    // pre-split a song before kicking off alignment, or split while the
    // alignment job has already finished).
    std::thread        split_worker_;
    std::atomic<bool>  split_running_{false};
    std::atomic<bool>  split_cancel_{false};
    mutable std::mutex split_mu_;
    std::string        split_phase_;
    float              split_fraction_   = 0.0f;
    std::string        split_message_;
    std::string        split_last_error_;
    std::string        split_last_outdir_;
    std::vector<std::string> split_last_stems_;
    // Worker stashes the vocals stem path here on success; the main thread
    // copies it into audio_path_buf_ and clears it (avoids racing on the
    // text buffer ImGui reads during draw).
    std::string        split_promote_audio_;
};

}  // namespace usersync
