#include "Aligner.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace usersync {

namespace {

std::string normalize(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

// For each existing word in `lines`, find the best-matching refined word in
// `refined` (within a small time window) and update boundaries by a
// confidence-weighted average. This makes boundaries tighter while preserving
// the original transcript wording.
void merge_refined(std::vector<Line>& lines, const std::vector<Word>& refined) {
    if (refined.empty()) return;

    // Index refined words by normalized text for quick lookup.
    size_t r_cursor = 0;
    for (auto& line : lines) {
        for (auto& w : line.words) {
            const std::string wn = normalize(w.text);
            if (wn.empty()) continue;

            // Walk refined cursor forward to roughly w's region.
            while (r_cursor + 1 < refined.size() &&
                   refined[r_cursor].t_end < w.t_start - 0.50) {
                ++r_cursor;
            }

            // Scan a window of refined words for the best textual+temporal match.
            size_t best = refined.size();
            double best_score = -1.0;
            for (size_t k = r_cursor;
                 k < refined.size() && refined[k].t_start < w.t_end + 0.50;
                 ++k) {
                if (normalize(refined[k].text) != wn) continue;
                const double dt = std::fabs(refined[k].t_start - w.t_start);
                const double score = refined[k].prob / (1.0 + dt);
                if (score > best_score) { best_score = score; best = k; }
            }
            if (best == refined.size()) continue;

            const auto& rw = refined[best];
            const float weight = std::clamp(rw.prob, 0.05f, 1.0f);
            w.t_start = (1.0 - weight) * w.t_start + weight * rw.t_start;
            w.t_end   = (1.0 - weight) * w.t_end   + weight * rw.t_end;
            w.prob    = std::max(w.prob, rw.prob);
        }

        // Re-monotonize: each word must end >= start and not overlap the next.
        for (size_t i = 0; i < line.words.size(); ++i) {
            auto& w = line.words[i];
            if (w.t_end < w.t_start) w.t_end = w.t_start;
            if (i + 1 < line.words.size()) {
                auto& n = line.words[i + 1];
                if (n.t_start < w.t_end) n.t_start = w.t_end;
            }
        }
        if (!line.words.empty()) {
            line.t_start = line.words.front().t_start;
            line.t_end   = line.words.back().t_end;
        }
    }
}

}  // namespace

bool refine_alignment(Transcriber&             t,
                      const PcmAudio&          audio,
                      const TranscribeOptions& opts,
                      int                      passes,
                      std::vector<Line>&       lines,
                      const ProgressFn&        on_progress,
                      const std::atomic<bool>& cancel) {
    if (passes <= 0 || lines.empty()) return true;

    // Pass schedule: alternating window sizes and stride offsets so that word
    // boundaries are observed from multiple receptive fields and aren't all
    // pinned to the same chunk edges.
    struct Sched { double window_s; double stride_s; double offset_s; };
    const Sched schedule[] = {
        {15.0, 12.0,  0.0},
        {10.0,  8.0,  3.0},
        {20.0, 15.0,  0.0},
        { 8.0,  6.0,  2.0},
        {12.0, 10.0,  5.0},
    };

    const double total_s = audio.duration_seconds();
    const uint32_t sr    = audio.sample_rate;

    for (int pass = 0; pass < passes; ++pass) {
        const Sched s = schedule[pass % (sizeof(schedule) / sizeof(schedule[0]))];

        // Build the list of windows for this pass.
        std::vector<std::pair<double, double>> windows;
        for (double t0 = s.offset_s; t0 < total_s; t0 += s.stride_s) {
            const double t1 = std::min(t0 + s.window_s, total_s);
            if (t1 - t0 < 1.0) break;
            windows.emplace_back(t0, t1);
        }

        std::vector<Word> refined_all;
        refined_all.reserve(windows.size() * 16);

        for (size_t wi = 0; wi < windows.size(); ++wi) {
            if (cancel.load()) return false;
            const auto [t0, t1] = windows[wi];

            const size_t b = static_cast<size_t>(t0 * sr);
            const size_t e = std::min<size_t>(static_cast<size_t>(t1 * sr),
                                              audio.samples.size());
            if (e <= b) continue;

            // Bias decoding with the words we already think live in this window.
            std::string prompt;
            for (const auto& line : lines) {
                if (line.t_end < t0 || line.t_start > t1) continue;
                for (const auto& w : line.words) {
                    if (w.t_end < t0 || w.t_start > t1) continue;
                    if (!prompt.empty()) prompt.push_back(' ');
                    prompt += w.text;
                }
            }

            std::vector<Word> window_words;
            std::string err;
            if (!t.transcribe_window(audio, b, e, opts, prompt, window_words, err)) {
                continue;  // best-effort; one bad window shouldn't kill the pass
            }
            for (auto& w : window_words) refined_all.push_back(std::move(w));

            if (on_progress) {
                const float frac = (pass + (wi + 1.0f) / windows.size()) / passes;
                on_progress("refine", frac,
                            "pass " + std::to_string(pass + 1) + "/" +
                            std::to_string(passes));
            }
        }

        std::sort(refined_all.begin(), refined_all.end(),
                  [](const Word& a, const Word& b) { return a.t_start < b.t_start; });
        merge_refined(lines, refined_all);
    }

    return true;
}

void tighten_last_word_end(std::vector<Line>& lines) {
    // Find the last non-empty line / last word.
    Line* last_line = nullptr;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        if (!it->words.empty()) { last_line = &*it; break; }
    }
    if (!last_line) return;
    Word& last = last_line->words.back();

    // Estimate a sane upper bound for the last word's duration from the
    // median of all other words in the song. Skip degenerate ones.
    std::vector<double> durs;
    durs.reserve(64);
    for (const auto& l : lines) {
        for (size_t i = 0; i < l.words.size(); ++i) {
            // Skip the very last word -- that's the one we're capping.
            if (&l == last_line && i + 1 == l.words.size()) continue;
            const double d = l.words[i].t_end - l.words[i].t_start;
            if (d > 0.04 && d < 4.0) durs.push_back(d);
        }
    }
    double median_dur = 0.40;
    if (!durs.empty()) {
        std::sort(durs.begin(), durs.end());
        median_dur = durs[durs.size() / 2];
    }

    // Cap to 2.5x median. Generous enough for sustained final notes,
    // tight enough to not stretch through outro silence.
    const double max_dur  = std::max(0.30, median_dur * 2.5);
    const double cur_dur  = last.t_end - last.t_start;
    if (cur_dur > max_dur) {
        last.t_end       = last.t_start + max_dur;
        last_line->t_end = last.t_end;
    }
    // Guard against negative / inverted durations as well.
    if (last.t_end < last.t_start) last.t_end = last.t_start + median_dur;
    if (last_line->t_end < last.t_end) last_line->t_end = last.t_end;
}

}  // namespace usersync
