#include "ForcedAligner.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <vector>

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

struct UserWord { std::string text; std::string norm; };
struct UserLine { std::vector<UserWord> words; };

std::vector<UserLine> parse_user_text(const std::string& text) {
    std::vector<UserLine> lines;
    UserLine    cur_line;
    std::string cur_word;

    auto push_word = [&]() {
        if (cur_word.empty()) return;
        UserWord w{ cur_word, normalize(cur_word) };
        cur_line.words.push_back(std::move(w));
        cur_word.clear();
    };
    auto push_line = [&]() {
        push_word();
        if (!cur_line.words.empty()) lines.push_back(std::move(cur_line));
        cur_line = {};
    };

    for (char c : text) {
        if (c == '\n') {
            push_line();
        } else if (c == '\r') {
            // ignore -- handled with the paired \n
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            push_word();
        } else {
            cur_word.push_back(c);
        }
    }
    push_line();
    return lines;
}

}  // namespace

std::vector<Line> align_text_to_whisper(const std::string&        user_text,
                                        const std::vector<Line>&  whisper_lines,
                                        double                    total_duration) {
    auto u_lines = parse_user_text(user_text);
    if (u_lines.empty()) return {};

    // Flatten user words and remember which line each came from.
    std::vector<UserWord*> u_words;
    std::vector<size_t>    u_to_line;
    for (size_t i = 0; i < u_lines.size(); ++i) {
        for (auto& w : u_lines[i].words) {
            u_words.push_back(&w);
            u_to_line.push_back(i);
        }
    }
    if (u_words.empty()) return {};

    // Flatten whisper words.
    struct WW { const Word* w; std::string norm; };
    std::vector<WW> w_words;
    w_words.reserve(256);
    for (const auto& line : whisper_lines) {
        for (const auto& w : line.words) {
            w_words.push_back({ &w, normalize(w.text) });
        }
    }

    const int N = static_cast<int>(u_words.size());
    const int M = static_cast<int>(w_words.size());

    std::vector<Word> timed(N);
    for (int i = 0; i < N; ++i) timed[i].text = u_words[i]->text;

    if (M == 0) {
        // No whisper anchors at all. Return empty so the caller can
        // surface a real error instead of writing uniformly-spaced
        // garbage timestamps to the .lrc.
        return {};
    }

    {
        // Needleman-Wunsch alignment. Tables are O(N*M) which is fine for
        // typical lyric lengths (a few hundred words on each side).
        const int MATCH    =  2;
        const int MISMATCH = -1;
        const int GAP      = -1;

        std::vector<std::vector<int>> dp(N + 1, std::vector<int>(M + 1, 0));
        for (int i = 0; i <= N; ++i) dp[i][0] = i * GAP;
        for (int j = 0; j <= M; ++j) dp[0][j] = j * GAP;
        for (int i = 1; i <= N; ++i) {
            for (int j = 1; j <= M; ++j) {
                const std::string& un = u_words[i - 1]->norm;
                const std::string& wn = w_words[j - 1].norm;
                const int sc = (!un.empty() && un == wn) ? MATCH : MISMATCH;
                const int diag = dp[i - 1][j - 1] + sc;
                const int up   = dp[i - 1][j]     + GAP;
                const int lt   = dp[i][j - 1]     + GAP;
                dp[i][j] = std::max({ diag, up, lt });
            }
        }

        // Traceback: record the matched whisper-word index for each user word.
        std::vector<int> match_idx(N, -1);
        int i = N, j = M;
        while (i > 0 && j > 0) {
            const std::string& un = u_words[i - 1]->norm;
            const std::string& wn = w_words[j - 1].norm;
            const int sc = (!un.empty() && un == wn) ? MATCH : MISMATCH;
            if (dp[i][j] == dp[i - 1][j - 1] + sc) {
                if (sc == MATCH) match_idx[i - 1] = j - 1;
                --i; --j;
            } else if (dp[i][j] == dp[i - 1][j] + GAP) {
                --i;
            } else {
                --j;
            }
        }

        // Transfer timestamps for matched words; mark unmatched with NaN-ish
        // sentinel so we can find them in the interpolation pass.
        constexpr double kUnset = -1.0;
        for (int k = 0; k < N; ++k) {
            timed[k].t_start = kUnset;
            timed[k].t_end   = kUnset;
            if (match_idx[k] >= 0) {
                const Word* ww = w_words[match_idx[k]].w;
                timed[k].t_start = ww->t_start;
                timed[k].t_end   = ww->t_end;
                timed[k].prob    = ww->prob;
            }
        }

        // Interpolate runs of unmatched words between matched anchors.
        int prev = -1;
        for (int k = 0; k < N; ++k) {
            if (timed[k].t_start < 0.0) continue;
            if (prev < 0) {
                // Head fill: interpolate 0..prev's start across leading unmatched.
                if (k > 0) {
                    const double t0 = 0.0;
                    const double t1 = timed[k].t_start;
                    const double span = std::max(0.001, t1 - t0);
                    const double step = span / (k + 1);
                    for (int m = 0; m < k; ++m) {
                        timed[m].t_start = t0 + step * (m + 0);
                        timed[m].t_end   = t0 + step * (m + 1);
                    }
                }
            } else if (k - prev > 1) {
                // Interior gap: linearly distribute timestamps between prev and k.
                const double t0   = timed[prev].t_end;
                const double t1   = timed[k].t_start;
                const int    gap  = k - prev;
                const double step = std::max(0.001, t1 - t0) / gap;
                for (int m = prev + 1; m < k; ++m) {
                    timed[m].t_start = t0 + step * (m - prev - 1);
                    timed[m].t_end   = t0 + step * (m - prev);
                }
            }
            prev = k;
        }
        // Tail fill: distribute trailing unmatched words. Crucially, DON'T
        // stretch them to total_duration -- songs typically end with seconds
        // (or minutes) of trailing instrumental/silence, which would smear
        // the final user words across that silence. Pace them at the song's
        // own matched-word tempo instead, capped by total_duration.
        if (prev < 0) {
            // No matches at all -- fall back to uniform distribution.
            const double step = total_duration / N;
            for (int k = 0; k < N; ++k) {
                timed[k].t_start = k * step;
                timed[k].t_end   = (k + 1) * step;
            }
        } else if (prev < N - 1) {
            // Estimate the song's per-word pace from matched anchors.
            std::vector<double> durs;
            durs.reserve(64);
            for (int k = 0; k < N; ++k) {
                if (timed[k].t_start < 0.0) continue;
                const double d = timed[k].t_end - timed[k].t_start;
                if (d > 0.04 && d < 4.0) durs.push_back(d);
            }
            double pace = 0.45;  // ~135 wpm default
            if (!durs.empty()) {
                std::sort(durs.begin(), durs.end());
                pace = std::max(0.10, durs[durs.size() / 2] * 1.3);
            }

            const double t0   = timed[prev].t_end;
            const int    rem  = N - 1 - prev;
            const double t1   = std::min(total_duration, t0 + pace * (rem + 1));
            const double step = std::max(0.001, t1 - t0) / (rem + 1);
            for (int m = prev + 1; m < N; ++m) {
                timed[m].t_start = t0 + step * (m - prev - 1);
                timed[m].t_end   = t0 + step * (m - prev);
            }
        }

        // Re-monotonize: never let a word end before it starts or before the
        // previous word's end. This shouldn't normally happen but the
        // interpolation can produce minor inversions on bad matches.
        for (int k = 0; k < N; ++k) {
            if (timed[k].t_end < timed[k].t_start) timed[k].t_end = timed[k].t_start;
            if (k > 0 && timed[k].t_start < timed[k - 1].t_end) {
                timed[k].t_start = timed[k - 1].t_end;
                if (timed[k].t_end < timed[k].t_start) timed[k].t_end = timed[k].t_start;
            }
        }
    }

    // Group the timed words back into lines per the original user_text layout.
    std::vector<Line> result(u_lines.size());
    size_t k = 0;
    for (size_t li = 0; li < u_lines.size(); ++li) {
        for (size_t wi = 0; wi < u_lines[li].words.size(); ++wi) {
            result[li].words.push_back(timed[k++]);
        }
        if (!result[li].words.empty()) {
            result[li].t_start = result[li].words.front().t_start;
            result[li].t_end   = result[li].words.back().t_end;
        }
    }
    // Drop empty lines that resulted from blank input lines.
    result.erase(std::remove_if(result.begin(), result.end(),
                                [](const Line& l) { return l.words.empty(); }),
                 result.end());
    return result;
}

}  // namespace usersync
