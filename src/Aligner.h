#pragma once

#include "Transcriber.h"

#include <atomic>
#include <vector>

namespace usersync {

// Iteratively refines word-level timestamps by running additional Whisper
// passes over overlapping windows of the audio, then merging results with a
// confidence-weighted consensus. More passes ≈ tighter word boundaries at the
// cost of linear extra compute time.
//
// `passes` = number of refinement passes on top of the initial transcription.
// Each pass uses a different window size / stride so boundaries get voted on
// from multiple receptive fields.
bool refine_alignment(Transcriber&             t,
                      const PcmAudio&          audio,
                      const TranscribeOptions& opts,
                      int                      passes,
                      std::vector<Line>&       lines,
                      const ProgressFn&        on_progress,
                      const std::atomic<bool>& cancel);

// Whisper's final-segment timestamp often runs to the end of the audio chunk
// (or to a trailing instrumental section), which makes the last word's t_end
// land tens of seconds after the actual vocal ends. This trims the last
// word's end (and the last line's end with it) to a sensible duration based
// on the median word length elsewhere in the song.
void tighten_last_word_end(std::vector<Line>& lines);

}  // namespace usersync
