#include "WhisperXRunner.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace usersync {

namespace {

#ifdef _WIN32

// Run `cmd` with stdout+stderr captured into `out`. Returns exit code or -1.
int run_capture(const std::string& cmd, std::string& out) {
    out.clear();
    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    PROCESS_INFORMATION pi{};
    STARTUPINFOA si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError  = wr;
    si.hStdInput  = nullptr;
    std::string cmdline = cmd;
    if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(rd); CloseHandle(wr);
        return -1;
    }
    CloseHandle(wr);
    std::array<char, 4096> buf;
    DWORD got = 0;
    while (ReadFile(rd, buf.data(), (DWORD)buf.size(), &got, nullptr) && got > 0) {
        out.append(buf.data(), got);
    }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

// Run `cmd` with stderr streamed line-by-line into `on_line` while it runs.
// `cancel` is polled between reads; if set, the child is killed.
int run_stream(const std::string&             cmd,
               std::function<void(const std::string&)> on_line,
               const std::atomic<bool>&       cancel) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    PROCESS_INFORMATION pi{};
    STARTUPINFOA si{};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput  = wr;
    si.hStdError   = wr;
    si.hStdInput   = nullptr;
    std::string cmdline = cmd;
    if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(rd); CloseHandle(wr);
        return -1;
    }
    CloseHandle(wr);

    std::string acc;
    std::array<char, 4096> buf;
    DWORD got = 0;
    while (true) {
        if (cancel.load()) {
            TerminateProcess(pi.hProcess, 1);
            break;
        }
        // Peek so we don't block forever if the child stalls. If nothing
        // available yet, sleep briefly to avoid spinning.
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) break;
        if (avail == 0) {
            DWORD wait = WaitForSingleObject(pi.hProcess, 50);
            if (wait == WAIT_OBJECT_0) {
                // Process exited -- drain remaining output then break.
                while (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                    if (!ReadFile(rd, buf.data(), (DWORD)buf.size(), &got, nullptr) || got == 0) break;
                    acc.append(buf.data(), got);
                }
                break;
            }
            continue;
        }
        if (!ReadFile(rd, buf.data(), (DWORD)buf.size(), &got, nullptr) || got == 0) break;
        acc.append(buf.data(), got);
        // Emit complete lines.
        size_t nl;
        while ((nl = acc.find('\n')) != std::string::npos) {
            std::string line = acc.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (on_line) on_line(line);
            acc.erase(0, nl + 1);
        }
    }
    // Flush trailing (no-newline) text.
    if (!acc.empty() && on_line) on_line(acc);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
    return (int)code;
}

#endif  // _WIN32

// Find the Python interpreter to use. Prefer the venv created by
// setup_whisperx.bat -- that's the one guaranteed to have whisperx + the
// right torch. Fall back to system Python so the user can still run things
// after a manual install.
std::string find_python_exe() {
#ifdef _WIN32
    namespace fs = std::filesystem;
    fs::path candidates[] = {
        fs::current_path() / "python_env" / "Scripts" / "python.exe",
        // exe-relative (in case CWD is somewhere else)
        []{
            char buf[MAX_PATH];
            GetModuleFileNameA(nullptr, buf, MAX_PATH);
            fs::path p(buf);
            return p.parent_path() / "python_env" / "Scripts" / "python.exe";
        }(),
    };
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec)) return c.string();
    }
    // Fall back: system python on PATH.
    std::string out;
    if (run_capture("where python", out) == 0) {
        const size_t nl = out.find_first_of("\r\n");
        return out.substr(0, nl == std::string::npos ? out.size() : nl);
    }
    return {};
#else
    return "python3";
#endif
}

std::mutex            g_mu;
WhisperXStatus        g_cached;
bool                  g_have_cached = false;

WhisperXStatus detect() {
    WhisperXStatus s;
    s.python_exe = find_python_exe();
    s.python_found = !s.python_exe.empty();
    if (!s.python_found) { s.error = "python not found"; return s; }

#ifdef _WIN32
    std::string out;
    const std::string probe =
        "\"" + s.python_exe + "\" -c \"import whisperx, sys; print(getattr(whisperx, '__version__', 'installed'))\"";
    if (run_capture(probe, out) == 0 && !out.empty()) {
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
        s.whisperx_version   = out;
        s.whisperx_installed = !out.empty() && out.find("Error") == std::string::npos;
        if (!s.whisperx_installed) s.error = out;
    } else {
        s.error = "whisperx import failed -- run setup_whisperx.bat";
    }
#endif
    return s;
}

// Find the align.py script path relative to the exe or CWD.
std::string find_align_script() {
#ifdef _WIN32
    namespace fs = std::filesystem;
    fs::path candidates[] = {
        fs::current_path() / "scripts" / "align.py",
        []{
            char buf[MAX_PATH];
            GetModuleFileNameA(nullptr, buf, MAX_PATH);
            return fs::path(buf).parent_path() / "scripts" / "align.py";
        }(),
    };
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec)) return c.string();
    }
#endif
    return {};
}

}  // namespace

const WhisperXStatus& whisperx_status() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_have_cached) { g_cached = detect(); g_have_cached = true; }
    return g_cached;
}

void refresh_whisperx_status() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_cached = detect();
    g_have_cached = true;
}

std::vector<Line> run_whisperx(const WhisperXJob&        job,
                               const ProgressFn&         on_progress,
                               const SegmentFn&          on_segment,
                               const std::atomic<bool>&  cancel,
                               std::string&              error_out) {
    const std::string& audio_path  = job.audio_path;
    const std::string& user_lyrics = job.user_lyrics;
    const std::string& language    = job.language;
    const bool         use_gpu     = job.use_gpu;
    const auto& st = whisperx_status();
    if (!st.python_found) {
        error_out = "Python venv not found. Run setup_whisperx.bat first.";
        return {};
    }
    if (!st.whisperx_installed) {
        error_out = "WhisperX not installed in the venv. Run setup_whisperx.bat.";
        return {};
    }

    const std::string script = find_align_script();
    if (script.empty()) {
        error_out = "scripts/align.py not found next to the exe.";
        return {};
    }

#ifndef _WIN32
    error_out = "WhisperX runner only implemented on Windows.";
    return {};
#else

    namespace fs = std::filesystem;
    char tmp[MAX_PATH] = {0};
    GetTempPathA(MAX_PATH, tmp);
    const fs::path req_path = fs::path(tmp) / "usersync_wx_req.json";
    const fs::path stat_path = fs::path(tmp) / "usersync_wx_status.json";

    // Honor the user's chosen output path; fall back to <audio>.lrc next to
    // the audio file if they didn't specify one.
    fs::path out_path = job.output_path.empty()
                      ? (fs::path(audio_path).replace_extension(".lrc"))
                      : fs::path(job.output_path);

    // Build request JSON (hand-rolled to avoid pulling in a JSON dep).
    auto esc = [](const std::string& s) {
        std::string o;
        o.reserve(s.size() + 16);
        for (char c : s) {
            switch (c) {
                case '\\': o += "\\\\"; break;
                case '"':  o += "\\\""; break;
                case '\n': o += "\\n";  break;
                case '\r':              break;
                case '\t': o += "\\t";  break;
                default:   o.push_back(c);
            }
        }
        return o;
    };

    {
        std::ofstream f(req_path);
        f << "{\n"
          << "  \"audio\":        \"" << esc(audio_path)      << "\",\n"
          << "  \"lyrics\":       \"" << esc(user_lyrics)     << "\",\n"
          << "  \"language\":     \"" << esc(language)        << "\",\n"
          << "  \"use_gpu\":      " << (use_gpu ? "true" : "false") << ",\n"
          << "  \"split_vocals\": " << (job.split_vocals ? "true" : "false") << ",\n"
          << "  \"output\":       \"" << esc(out_path.string()) << "\",\n"
          << "  \"tags\": {\n"
          << "    \"title\":  \"" << esc(job.title)  << "\",\n"
          << "    \"artist\": \"" << esc(job.artist) << "\",\n"
          << "    \"album\":  \"" << esc(job.album)  << "\"\n"
          << "  }\n"
          << "}\n";
    }

    if (on_progress) on_progress("whisperx", 0.0f,
        std::string("starting WhisperX  (") + st.python_exe + ")");

    const std::string cmd = "\"" + st.python_exe + "\" \"" + script + "\" \""
                          + req_path.string() + "\" \"" + stat_path.string() + "\"";

    // Stream progress lines from the Python script live.
    auto handle_line = [&](const std::string& line) {
        if (line.empty()) return;
        if (line.rfind("[PROG] ", 0) == 0) {
            int pct = 0;
            size_t sp = line.find(' ', 7);
            if (sp != std::string::npos) {
                pct = std::atoi(line.substr(7, sp - 7).c_str());
                if (on_progress) {
                    on_progress("whisperx", std::max(0.0f, std::min(1.0f, pct / 100.0f)),
                                line.substr(sp + 1));
                }
            }
            return;
        }
        if (line.rfind("[SEG] ", 0) == 0 && on_segment) {
            // [SEG] <t0> <t1> <text>
            std::istringstream iss(line.substr(6));
            double t0 = 0.0, t1 = 0.0;
            iss >> t0 >> t1;
            std::string text;
            std::getline(iss, text);
            while (!text.empty() && text.front() == ' ') text.erase(text.begin());

            Line ln;
            ln.t_start = t0;
            ln.t_end   = t1;
            std::istringstream witer(text);
            std::vector<std::string> words;
            std::string w;
            while (witer >> w) words.push_back(w);
            if (!words.empty()) {
                const double dur  = std::max(0.05, t1 - t0);
                const double step = dur / words.size();
                for (size_t i = 0; i < words.size(); ++i) {
                    Word word;
                    word.text    = words[i];
                    word.t_start = t0 + step * i;
                    word.t_end   = t0 + step * (i + 1);
                    word.prob    = 0.5f;
                    ln.words.push_back(std::move(word));
                }
            }
            on_segment(ln);
        }
        // ALWAYS also forward the raw line to the log/progress sink so the
        // user sees every event scrolling by in the Log tab.
        if (on_progress) on_progress("whisperx", -1.0f, line);
    };
    const int rc = run_stream(cmd, handle_line, cancel);

    if (cancel.load()) {
        error_out = "cancelled";
        return {};
    }

    // Read the status JSON the Python script wrote.
    std::ifstream sf(stat_path, std::ios::binary);
    if (!sf) {
        error_out = "WhisperX produced no status file (exit=" + std::to_string(rc) + ")";
        return {};
    }
    std::stringstream ss; ss << sf.rdbuf();
    const std::string s = ss.str();

    auto find_str = [&](const char* key) -> std::string {
        const std::string needle = std::string("\"") + key + "\"";
        size_t k = s.find(needle);
        if (k == std::string::npos) return {};
        size_t c = s.find(':', k); if (c == std::string::npos) return {};
        size_t q1 = s.find('"', c); if (q1 == std::string::npos) return {};
        size_t q2 = s.find('"', q1 + 1); if (q2 == std::string::npos) return {};
        return s.substr(q1 + 1, q2 - q1 - 1);
    };
    auto find_bool = [&](const char* key, bool def) -> bool {
        const std::string needle = std::string("\"") + key + "\"";
        size_t k = s.find(needle);
        if (k == std::string::npos) return def;
        size_t c = s.find(':', k);
        if (c == std::string::npos) return def;
        // skip whitespace
        ++c;
        while (c < s.size() && std::isspace((unsigned char)s[c])) ++c;
        return s.compare(c, 4, "true") == 0;
    };

    if (!find_bool("ok", false)) {
        error_out = "WhisperX: " + find_str("error");
        if (error_out == "WhisperX: ") error_out = "WhisperX failed (no error message).";
        return {};
    }

    // Output LRC file was written by Python. Parse it back into Line[]
    // so the UI can show it in Preview/Editor/Output tabs.
    const std::string lrc_path = find_str("output");
    std::ifstream lf(lrc_path, std::ios::binary);
    if (!lf) {
        error_out = "WhisperX reported OK but couldn't read " + lrc_path;
        return {};
    }
    std::stringstream lss; lss << lf.rdbuf();
    const std::string lrc = lss.str();

    // Minimal enhanced-LRC parser: [mm:ss.xx]<mm:ss.xx>word ...
    auto parse_ts = [](const std::string& body) -> double {
        // body is mm:ss.xx
        size_t colon = body.find(':');
        if (colon == std::string::npos) return -1.0;
        const double mm = std::atof(body.substr(0, colon).c_str());
        const double ss = std::atof(body.substr(colon + 1).c_str());
        return mm * 60.0 + ss;
    };

    std::vector<Line> out_lines;
    std::stringstream lines_stream(lrc);
    std::string line_text;
    while (std::getline(lines_stream, line_text)) {
        if (line_text.empty() || line_text[0] != '[') continue;
        // Skip metadata tags like [ti:...] [ar:...] -- they don't have digits before ':'.
        size_t close = line_text.find(']');
        if (close == std::string::npos) continue;
        const std::string head = line_text.substr(1, close - 1);
        if (head.empty() || !std::isdigit((unsigned char)head[0])) continue;
        const double line_t = parse_ts(head);
        if (line_t < 0) continue;

        Line ln;
        ln.t_start = line_t;
        size_t pos = close + 1;
        // Each word: <ts>word  (next <ts> starts the next word, or end-of-line)
        while (true) {
            size_t lt = line_text.find('<', pos);
            if (lt == std::string::npos) break;
            size_t gt = line_text.find('>', lt);
            if (gt == std::string::npos) break;
            const double t = parse_ts(line_text.substr(lt + 1, gt - lt - 1));
            // Word text is everything between '>' and the next '<' (or end).
            size_t next_lt = line_text.find('<', gt);
            std::string word_text = (next_lt == std::string::npos)
                                    ? line_text.substr(gt + 1)
                                    : line_text.substr(gt + 1, next_lt - gt - 1);
            // Trim.
            while (!word_text.empty() && std::isspace((unsigned char)word_text.back())) word_text.pop_back();
            size_t lead = 0;
            while (lead < word_text.size() && std::isspace((unsigned char)word_text[lead])) ++lead;
            word_text.erase(0, lead);

            if (!word_text.empty()) {
                Word w;
                w.text    = word_text;
                w.t_start = t;
                w.t_end   = t;  // populated below from the next ts
                w.prob    = 1.0f;
                ln.words.push_back(std::move(w));
            } else if (!ln.words.empty()) {
                // No text after this timestamp -- it's the line-end marker.
                ln.words.back().t_end = t;
            }
            pos = (next_lt == std::string::npos) ? line_text.size() : next_lt;
        }
        // Fill in word t_end from the next word's t_start.
        for (size_t i = 0; i + 1 < ln.words.size(); ++i) {
            if (ln.words[i].t_end <= ln.words[i].t_start) {
                ln.words[i].t_end = ln.words[i + 1].t_start;
            }
        }
        if (!ln.words.empty()) {
            ln.t_end = ln.words.back().t_end;
            if (ln.t_end < ln.t_start) ln.t_end = ln.t_start;
            out_lines.push_back(std::move(ln));
        }
    }

    if (out_lines.empty()) {
        error_out = "WhisperX produced 0 lines after parsing " + lrc_path;
    }
    return out_lines;
#endif
}

}  // namespace usersync
