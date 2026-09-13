#include "SplitRunner.h"
#include "WhisperXRunner.h"   // we reuse whisperx_status() to find the venv

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace usersync {

namespace {

#ifdef _WIN32

// Run `cmd` with stdout+stderr streamed line-by-line into `on_line`.
// Cancel kills the subprocess. Returns the child's exit code or -1.
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
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) break;
        if (avail == 0) {
            DWORD w = WaitForSingleObject(pi.hProcess, 50);
            if (w == WAIT_OBJECT_0) {
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
        size_t nl;
        while ((nl = acc.find('\n')) != std::string::npos) {
            std::string line = acc.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (on_line) on_line(line);
            acc.erase(0, nl + 1);
        }
    }
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

std::string find_split_script() {
#ifdef _WIN32
    namespace fs = std::filesystem;
    fs::path candidates[] = {
        fs::current_path() / "scripts" / "split.py",
        []{
            char buf[MAX_PATH];
            GetModuleFileNameA(nullptr, buf, MAX_PATH);
            return fs::path(buf).parent_path() / "scripts" / "split.py";
        }(),
    };
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec)) return c.string();
    }
#endif
    return {};
}

std::string esc_json(const std::string& s) {
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
}

}  // namespace

SplitResult run_split(const SplitJob&          job,
                      const ProgressFn&        on_progress,
                      const std::atomic<bool>& cancel) {
    SplitResult res;

    const auto& st = whisperx_status();   // reuses Python venv detection
    if (!st.python_found) {
        res.error = "Python venv not found. Run setup_whisperx.bat first.";
        return res;
    }
    if (!st.whisperx_installed) {
        // Demucs is installed alongside whisperx by setup_whisperx.bat,
        // so the whisperx flag is a good proxy for "venv is ready".
        res.error = "Python env incomplete. Run setup_whisperx.bat.";
        return res;
    }

    const std::string script = find_split_script();
    if (script.empty()) {
        res.error = "scripts/split.py not found next to the exe.";
        return res;
    }

#ifndef _WIN32
    res.error = "Split runner only implemented on Windows.";
    return res;
#else

    namespace fs = std::filesystem;
    char tmp[MAX_PATH] = {0};
    GetTempPathA(MAX_PATH, tmp);
    const fs::path req_path = fs::path(tmp) / "usersync_split_req.json";
    const fs::path stat_path = fs::path(tmp) / "usersync_split_status.json";

    {
        std::ofstream f(req_path);
        f << "{\n"
          << "  \"audio\":       \"" << esc_json(job.audio_path) << "\",\n"
          << "  \"out_dir\":     \"" << esc_json(job.out_dir)    << "\",\n"
          << "  \"model\":       \"" << esc_json(job.model)      << "\",\n"
          << "  \"stems\":       \"" << esc_json(job.stems_mode) << "\",\n"
          << "  \"format\":      \"" << esc_json(job.format)     << "\",\n"
          << "  \"mp3_bitrate\": " << job.mp3_bitrate << ",\n"
          << "  \"shifts\":      " << job.shifts      << ",\n"
          << "  \"overlap\":     " << job.overlap     << ",\n"
          << "  \"use_gpu\":     " << (job.use_gpu ? "true" : "false") << "\n"
          << "}\n";
    }

    if (on_progress) on_progress("split", 0.0f, "starting Demucs");

    const std::string cmd = "\"" + st.python_exe + "\" \"" + script + "\" \""
                          + req_path.string() + "\" \"" + stat_path.string() + "\"";

    auto handle_line = [&](const std::string& line) {
        if (line.empty()) return;
        if (line.rfind("[PROG] ", 0) == 0) {
            int pct = 0;
            size_t sp = line.find(' ', 7);
            if (sp != std::string::npos) {
                pct = std::atoi(line.substr(7, sp - 7).c_str());
                if (on_progress) {
                    on_progress("split",
                                std::max(0.0f, std::min(1.0f, pct / 100.0f)),
                                line.substr(sp + 1));
                }
            }
            return;
        }
        if (on_progress) on_progress("split", -1.0f, line);
    };

    const int rc = run_stream(cmd, handle_line, cancel);

    if (cancel.load()) { res.error = "cancelled"; return res; }

    std::ifstream sf(stat_path, std::ios::binary);
    if (!sf) {
        res.error = "split produced no status file (exit=" + std::to_string(rc) + ")";
        return res;
    }
    std::stringstream ss; ss << sf.rdbuf();
    const std::string s = ss.str();

    auto find_str = [&](const char* key) -> std::string {
        const std::string n = std::string("\"") + key + "\"";
        size_t k = s.find(n); if (k == std::string::npos) return {};
        size_t c = s.find(':', k); if (c == std::string::npos) return {};
        size_t q1 = s.find('"', c); if (q1 == std::string::npos) return {};
        size_t q2 = s.find('"', q1 + 1); if (q2 == std::string::npos) return {};
        return s.substr(q1 + 1, q2 - q1 - 1);
    };
    auto find_bool = [&](const char* key, bool def) -> bool {
        const std::string n = std::string("\"") + key + "\"";
        size_t k = s.find(n); if (k == std::string::npos) return def;
        size_t c = s.find(':', k); if (c == std::string::npos) return def;
        ++c;
        while (c < s.size() && std::isspace((unsigned char)s[c])) ++c;
        return s.compare(c, 4, "true") == 0;
    };

    if (!find_bool("ok", false)) {
        res.error = find_str("error");
        if (res.error.empty()) res.error = "split failed (no error message)";
        return res;
    }

    res.ok      = true;
    res.out_dir = find_str("out_dir");

    // Parse the "stems" string array.
    size_t k = s.find("\"stems\"");
    if (k != std::string::npos) {
        size_t lb = s.find('[', k);
        size_t rb = s.find(']', lb);
        if (lb != std::string::npos && rb != std::string::npos) {
            size_t pos = lb + 1;
            while (pos < rb) {
                size_t q1 = s.find('"', pos);
                if (q1 == std::string::npos || q1 > rb) break;
                size_t q2 = s.find('"', q1 + 1);
                if (q2 == std::string::npos || q2 > rb) break;
                res.stems.push_back(s.substr(q1 + 1, q2 - q1 - 1));
                pos = q2 + 1;
            }
        }
    }

    return res;
#endif
}

}  // namespace usersync
