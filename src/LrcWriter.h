#pragma once

#include "App.h"

#include <string>
#include <vector>

namespace usersync {

// Build a standard per-line LRC string: [mm:ss.xx]lyric text
// Lines that haven't been tapped yet (t_start < 0), and blank source lines
// (which are never tappable -- see App::tap()), are skipped entirely. Call
// App::tapped_count() beforehand if you want to warn the user before export.
std::string format_line_lrc(const std::vector<SyncLine>& lines, const LrcMetadata& meta);

bool write_file(const std::string& path, const std::string& contents, std::string& error);

}  // namespace usersync
