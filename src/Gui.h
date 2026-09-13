#pragma once

#include "App.h"
#include "AudioPlayer.h"

#include <string>
#include <vector>

struct GLFWwindow;

namespace usersync {

// Owns ImGui state and the per-frame UI. Same visual language as upstream
// usersync (dark purple-black chrome, pink/purple gradient accents) but the
// whole workflow is: paste lyrics -> play the song -> tap Enter on the
// downbeat of each line's first word -> save a standard per-line .lrc.
class Gui {
public:
    explicit Gui(App& app);

    void init(GLFWwindow* window);
    void shutdown();
    void draw();          // call once per frame between NewFrame and Render

private:
    void apply_style();
    void draw_title_bar();
    void draw_footer();

    void draw_setup_tab();
    void draw_sync_tab();
    void draw_editor_tab();

    void sync_player_to_audio_path();
    void do_tap();               // records a tap at the player's current position
    void set_status(const std::string& msg, bool is_error);

    bool do_save_lrc(std::string& err);
    bool do_save_project(std::string& err);
    bool do_load_project(std::string& err);

    App& app_;

    // Setup tab form state.
    char audio_path_buf_[1024]{};
    char out_path_buf_[1024]{};
    char title_buf_[256]{};
    char artist_buf_[256]{};
    char album_buf_[256]{};
    std::string lyrics_buf_;   // resizable buffer for the multi-line lyrics box

    // Tap-sync playback.
    AudioPlayer player_;
    std::string player_load_error_;

    // Editor tab: per-line nudge amount is fixed (buttons for 100ms/500ms),
    // no extra state needed beyond app_.lines().

    std::string project_path_;   // last used .usfp path, pre-fills future saves

    // Transient footer status line ("Saved to ...", error text, etc).
    std::string status_msg_;
    bool        status_is_error_ = false;
};

}  // namespace usersync
