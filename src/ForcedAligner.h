#pragma once

#include "Transcriber.h"

#include <string>
#include <vector>

namespace usersync {

// Forced alignment: take user-provided lyric text (one line per LRC line),
// snap it onto the word timestamps produced by whisper.
//
// Algorithm: Needleman-Wunsch word-level sequence alignment between the
// user's words (normalized: lowercase, alphanumeric only) and whisper's
// transcribed words. Matched user words inherit the whisper word's
// timestamps directly. Unmatched user words have their timestamps linearly
// interpolated between their nearest matched neighbours.
//
// Lines in `user_text` are split on '\n'. Blank lines are skipped.
// Words within a line are split on whitespace; original casing and
// punctuation are preserved in the output.
//
// `total_duration_seconds` is used as a fallback boundary if the user text
// has unmatched tail words and no neighbours to interpolate from.
std::vector<Line> align_text_to_whisper(const std::string&         user_text,
                                        const std::vector<Line>&   whisper_lines,
                                        double                     total_duration_seconds);

}  // namespace usersync
