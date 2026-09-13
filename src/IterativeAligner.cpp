#include "IterativeAligner.h"

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

// Per-line Needleman-Wunsch: align user-words to re-decoded whisper words.
// Returns for each user-word index its matched whisper-word index, or -1.
std::vector<int> nw_match(const std::vector<Word>& u,
                          const std::vector<Word>& w) {
    const int N = (int)u.size();
    const int M = (int)w.size();
    std::vector<int> match(N, -1);
    if (N == 0 || M == 0) return match;

    constexpr int MATCH    =  2;
    constexpr int MISMATCH = -1;
    constexpr int GAP      = -1;

    std::vector<std::string> un(N), wn(M);
    for (int i = 0; i < N; ++i) un[i] = normalize(u[i].text);
    for (int j = 0; j < M; ++j) wn[j] = normalize(w[j].text);

    std::vector<std::vector<int>> dp(N + 1, std::vector<int>(M + 1, 0));
    for (int i = 0; i <= N; ++i) dp[i][0] = i * GAP;
    for (int j = 0; j <= M; ++j) dp[0][j] = j * GAP;

    for (int i = 1; i <= N; ++i) {
        for (int j = 1; j <= M; ++j) {
            const int sc = (!un[i - 1].empty() && un[i - 1] == wn[j - 1]) ? MATCH : MISMATCH;
            dp[i][j] = std::max({ dp[i - 1][j - 1] + sc,
                                  dp[i - 1][j]     + GAP,
                                  dp[i][j - 1]     + GAP });
        }
    }
    int i = N, j = M;
    while (i > 0 && j > 0) {
        const int sc = (!un[i - 1].empty() && un[i - 1] == wn[j - 1]) ? MATCH : MISMATCH;
        if (dp[i][j] == dp[i - 1][j - 1] + sc) {
            if (sc == MATCH) match[i - 1] = j - 1;
            --i; --j;
        } else if (dp[i][j] == dp[i - 1][j] + GAP) {
            --i;
        } else {
            --j;
        }
    }
    return match;
}

// Refine one line in place. Returns the number of words whose timestamps
// were updated from a re-decoded match (-1 on transcribe failure).
int refine_line(Transcriber&             t,
                const PcmAudio&          audio,
                const TranscribeOptions& opts,
                Line&                    line,
                double                   pad_seconds) {
    if (line.words.empty()) return 0;

    const double dur = audio.duration_seconds();
    const double t0  = std::max(0.0, line.t_start - pad_seconds);
    const double t1  = std::min(dur, line.t_end   + pad_seconds);
    if (t1 - t0 < 0.5) return 0;

    // Build the prompt -- whisper takes this as a strong hint about what
    // text it should expect to find in the audio.
    std::string prompt;
    for (size_t i = 0; i < line.words.size(); ++i) {
        if (i) prompt.push_back(' ');
        prompt += line.words[i].text;
    }

    const size_t b = static_cast<size_t>(t0 * audio.sample_rate);
    const size_t e = std::min<size_t>(static_cast<size_t>(t1 * audio.sample_rate),
                                      audio.samples.size());

    std::vector<Word> ww;
    std::string err;
    if (!t.transcribe_window(audio, b, e, opts, prompt, ww, err)) return -1;
    if (ww.empty()) return 0;

    const auto match = nw_match(line.words, ww);

    int hits = 0;
    for (int m : match) if (m >= 0) ++hits;

    // SAFETY: if re-decode didn't match most of the prompt words, whisper
    // probably hallucinated text from the prompt without seeing it in the
    // audio. Bail out -- keep the existing timestamps instead of replacing
    // them with garbage.
    const double match_ratio = (double)hits / line.words.size();
    if (match_ratio < 0.6) return 0;

    std::vector<Word> refined = line.words;
    for (size_t i = 0; i < refined.size(); ++i) {
        if (match[i] >= 0) {
            refined[i].t_start = ww[match[i]].t_start;
            refined[i].t_end   = ww[match[i]].t_end;
            refined[i].prob    = std::max(refined[i].prob, ww[match[i]].prob);
        }
    }

    // SAFETY: if the refined timestamps drift wildly from the originals,
    // the alignment probably landed in the wrong audio region. Reject.
    double total_drift = 0.0;
    for (size_t i = 0; i < refined.size(); ++i) {
        total_drift += std::fabs(refined[i].t_start - line.words[i].t_start);
    }
    const double avg_drift = total_drift / refined.size();
    if (avg_drift > 1.5) return 0;  // average word moved >1.5s -- suspicious

    // Linearly interpolate unmatched words between matched anchors so we
    // don't blow up the line with one missed word.
    int prev = -1;
    for (size_t i = 0; i < refined.size(); ++i) {
        if (match[i] < 0) continue;
        if (prev < 0 && i > 0) {
            const double a = std::max(t0, refined[i].t_start - 1.0);
            const double b2 = refined[i].t_start;
            const double step = std::max(0.05, b2 - a) / (i + 1);
            for (size_t k = 0; k < i; ++k) {
                refined[k].t_start = a + step * (k + 0);
                refined[k].t_end   = a + step * (k + 1);
            }
        } else if (prev >= 0 && (int)i - prev > 1) {
            const double a   = refined[prev].t_end;
            const double b2  = refined[i].t_start;
            const int    gap = (int)i - prev;
            const double step = std::max(0.05, b2 - a) / gap;
            for (int k = prev + 1; k < (int)i; ++k) {
                refined[k].t_start = a + step * (k - prev - 1);
                refined[k].t_end   = a + step * (k - prev);
            }
        }
        prev = (int)i;
    }
    if (prev >= 0 && prev < (int)refined.size() - 1) {
        const double a   = refined[prev].t_end;
        const int    rem = (int)refined.size() - 1 - prev;
        const double step = std::max(0.05, t1 - a) / (rem + 1);
        for (int k = prev + 1; k < (int)refined.size(); ++k) {
            refined[k].t_start = a + step * (k - prev - 1);
            refined[k].t_end   = a + step * (k - prev);
        }
    }

    // Monotonize within the line.
    for (size_t i = 0; i < refined.size(); ++i) {
        if (refined[i].t_end < refined[i].t_start) refined[i].t_end = refined[i].t_start;
        if (i > 0 && refined[i].t_start < refined[i - 1].t_end) {
            refined[i].t_start = refined[i - 1].t_end;
            if (refined[i].t_end < refined[i].t_start) refined[i].t_end = refined[i].t_start;
        }
    }

    line.words   = std::move(refined);
    line.t_start = line.words.front().t_start;
    line.t_end   = line.words.back().t_end;
    return hits;
}

}  // namespace

std::vector<Line> iterative_align(
    Transcriber&             transcriber,
    const PcmAudio&          audio,
    const TranscribeOptions& opts,
    const std::vector<Line>& initial,
    int                      num_passes,
    const ProgressFn&        on_progress,
    const std::atomic<bool>& cancel) {

    std::vector<Line> lines = initial;
    if (lines.empty() || num_passes <= 0) return lines;

    // Padding schedule: start wide (so a misaligned line can find its
    // correct audio region), shrink progressively to zoom in.
    static constexpr double pad_schedule[] = {
        3.0, 2.5, 2.0, 1.5, 1.5, 1.2, 1.0, 0.8, 0.6, 0.5,
        0.5, 0.4, 0.4, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3,
    };
    constexpr int kPadScheduleLen = sizeof(pad_schedule) / sizeof(pad_schedule[0]);

    for (int pass = 0; pass < num_passes; ++pass) {
        if (cancel.load()) return lines;

        const double pad = pad_schedule[std::min(pass, kPadScheduleLen - 1)];
        int total_hits = 0;

        for (size_t li = 0; li < lines.size(); ++li) {
            if (cancel.load()) return lines;

            const int hits = refine_line(transcriber, audio, opts, lines[li], pad);
            if (hits > 0) total_hits += hits;

            if (on_progress) {
                const float frac = (pass + (li + 1.0f) / lines.size())
                                 / static_cast<float>(num_passes);
                on_progress("iter", frac,
                            "pass " + std::to_string(pass + 1) + "/" +
                            std::to_string(num_passes) + ", line " +
                            std::to_string(li + 1) + "/" +
                            std::to_string(lines.size()));
            }
        }

        // Cross-line monotonization: a later line can't start before the
        // previous line ended.
        for (size_t li = 1; li < lines.size(); ++li) {
            if (lines[li].words.empty()) continue;
            if (lines[li].t_start < lines[li - 1].t_end) {
                const double shift = lines[li - 1].t_end - lines[li].t_start;
                lines[li].t_start += shift;
                for (auto& w : lines[li].words) {
                    w.t_start += shift;
                    w.t_end   += shift;
                }
                lines[li].t_end = lines[li].words.back().t_end;
            }
        }

        if (on_progress) {
            on_progress("iter", (pass + 1.0f) / num_passes,
                        "pass " + std::to_string(pass + 1) + "/" +
                        std::to_string(num_passes) + " :: " +
                        std::to_string(total_hits) + " words refined");
        }
    }

    return lines;
}

}  // namespace usersync
