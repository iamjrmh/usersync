#pragma once

#include <string>

namespace usersync {

// Returns true and fills `out_path` if the user picked a file; false on cancel.
bool open_audio_file_dialog(std::string& out_path);

// Save-as dialog. `suggested_name` pre-fills the filename field.
bool save_lrc_file_dialog(std::string& out_path, const std::string& suggested_name = "");

// Open dialog for plain-text lyric files.
bool open_lyrics_file_dialog(std::string& out_path);

// Save / load a usersync-forked project file (.usfp). Captures the audio
// path, lyrics, and tapped line timestamps so a song can be reopened later
// and finetuned without re-tapping from scratch.
bool save_project_dialog(std::string& out_path, const std::string& suggested_name = "");
bool open_project_dialog(std::string& out_path);

}  // namespace usersync
