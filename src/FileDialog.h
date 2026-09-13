#pragma once

#include <string>

namespace usersync {

// Returns true and fills `out_path` if the user picked a file; false on cancel.
bool open_audio_file_dialog(std::string& out_path);

// Save-as dialog. `suggested_name` pre-fills the filename field.
bool save_lrc_file_dialog(std::string& out_path, const std::string& suggested_name = "");

// Open dialog for plain-text lyric files.
bool open_lyrics_file_dialog(std::string& out_path);

// Pick a folder (uses SHBrowseForFolder; no COM init required).
bool pick_folder_dialog(std::string& out_path);

// Save / load a usersync project file (.uspj). Captures the audio path,
// lyrics, and any manual edits so the user can re-open a song later and
// keep finetuning without re-running alignment from scratch.
bool save_project_dialog(std::string& out_path, const std::string& suggested_name = "");
bool open_project_dialog(std::string& out_path);

}  // namespace usersync
