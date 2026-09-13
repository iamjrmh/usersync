#pragma once

#include "Transcriber.h"

#include <atomic>
#include <vector>

namespace usersync {

// "Forced alignment via biased decoding": for each user line, re-run whisper
// on a TIGHT audio window with the line's exact text as the initial_prompt.
// Whisper then only has to figure out WHEN each word occurs, not WHAT the
// words are -- timing snaps massively tighter than a single full-song pass.
//
// Repeats over `num_passes` iterations, shrinking the per-line audio window
// each pass so successive passes refine word boundaries within the previous
// pass's brackets. Convergence is typically reached in 5-10 passes.
//
// `initial_aligned_lines` is the output of `align_text_to_whisper` -- the
// starting point with rough timestamps.
std::vector<Line> iterative_align(
    Transcriber&             transcriber,
    const PcmAudio&          audio,
    const TranscribeOptions& opts,
    const std::vector<Line>& initial_aligned_lines,
    int                      num_passes,
    const ProgressFn&        on_progress,
    const std::atomic<bool>& cancel);

}  // namespace usersync
