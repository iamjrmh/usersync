#pragma once

#include "Transcriber.h"

#include <string>
#include <vector>

namespace usersync {

struct LrcMetadata {
    std::string title;
    std::string artist;
    std::string album;
    std::string by = "usersync";
};

// Build an Enhanced LRC string with per-word A2-style timestamps:
//   [mm:ss.xx]<mm:ss.xx>word <mm:ss.xx>word ...
// Also emits a line-end <mm:ss.xx> marker so players can highlight the final
// word for its full duration.
std::string format_enhanced_lrc(const std::vector<Line>& lines,
                                const LrcMetadata&       meta);

bool write_file(const std::string& path, const std::string& contents, std::string& error);

}  // namespace usersync
