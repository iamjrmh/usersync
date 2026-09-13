#include "App.h"

#include <fstream>
#include <sstream>

namespace usersync {

void App::set_lyrics(const std::string& raw_text) {
    lines_.clear();
    std::istringstream in(raw_text);
    std::string line;
    while (std::getline(in, line)) {
        // Strip a trailing '\r' left over from CRLF-pasted text.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines_.push_back(SyncLine{line, -1.0});
    }
    cursor_ = 0;
    advance_cursor_from(0);
}

bool App::finished() const {
    return cursor_ >= static_cast<int>(lines_.size());
}

void App::advance_cursor_from(int start) {
    cursor_ = start;
    // Blank lines have no "first word" to tap -- skip straight past them.
    while (cursor_ < static_cast<int>(lines_.size()) && lines_[cursor_].text.empty()) {
        ++cursor_;
    }
}

void App::tap(double t_sec) {
    if (finished()) return;
    lines_[cursor_].t_start = t_sec;
    advance_cursor_from(cursor_ + 1);
}

void App::untap_last() {
    // Walk back to the most recent tapped, non-blank line.
    int i = cursor_ - 1;
    while (i >= 0 && lines_[i].text.empty()) --i;
    if (i < 0) return;
    lines_[i].t_start = -1.0;
    cursor_ = i;
}

void App::reset_taps() {
    for (auto& l : lines_) l.t_start = -1.0;
    advance_cursor_from(0);
}

void App::set_cursor(int idx) {
    if (idx < 0) idx = 0;
    if (idx > static_cast<int>(lines_.size())) idx = static_cast<int>(lines_.size());
    advance_cursor_from(idx);
}

int App::tapped_count() const {
    int n = 0;
    for (const auto& l : lines_) if (l.tapped()) ++n;
    return n;
}

namespace {
// AUDIO=/TITLE=/etc. values are single-line and never contain '\t', so a
// tab is a safe, simple delimiter between a LINE directive's timestamp and
// its (otherwise arbitrary) text.
std::string escape_crlf(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) if (c != '\r') out.push_back(c);
    return out;
}
}  // namespace

bool App::save_project(const std::string& path, const std::string& audio_path,
                       std::string& error) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) { error = "could not open " + path + " for writing"; return false; }

    f << "USFP1\n";
    f << "AUDIO=" << escape_crlf(audio_path) << '\n';
    f << "TITLE=" << escape_crlf(meta_.title) << '\n';
    f << "ARTIST=" << escape_crlf(meta_.artist) << '\n';
    f << "ALBUM=" << escape_crlf(meta_.album) << '\n';
    for (const auto& l : lines_) {
        f << "LINE=" << l.t_start << '\t' << escape_crlf(l.text) << '\n';
    }
    if (!f) { error = "write to " + path + " failed"; return false; }
    return true;
}

bool App::load_project(const std::string& path, std::string& out_audio_path,
                       std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { error = "could not open " + path; return false; }

    std::string header;
    std::getline(f, header);
    if (!header.empty() && header.back() == '\r') header.pop_back();
    if (header != "USFP1") { error = path + " is not a usersync-forked project file"; return false; }

    std::vector<SyncLine> new_lines;
    LrcMetadata            new_meta;
    std::string            new_audio;

    std::string raw;
    while (std::getline(f, raw)) {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        const auto eq = raw.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = raw.substr(0, eq);
        const std::string val = raw.substr(eq + 1);

        if (key == "AUDIO")       new_audio       = val;
        else if (key == "TITLE")  new_meta.title  = val;
        else if (key == "ARTIST") new_meta.artist = val;
        else if (key == "ALBUM")  new_meta.album  = val;
        else if (key == "LINE") {
            const auto tab = val.find('\t');
            SyncLine line;
            if (tab == std::string::npos) {
                line.t_start = -1.0;
                line.text    = val;
            } else {
                try { line.t_start = std::stod(val.substr(0, tab)); }
                catch (...) { line.t_start = -1.0; }
                line.text = val.substr(tab + 1);
            }
            new_lines.push_back(std::move(line));
        }
    }

    lines_        = std::move(new_lines);
    meta_         = new_meta;
    out_audio_path = new_audio;

    // Resume where the session left off: the first blank-free line that
    // hasn't been tapped yet, not just the first line overall.
    cursor_ = 0;
    while (cursor_ < static_cast<int>(lines_.size())
           && (lines_[cursor_].text.empty() || lines_[cursor_].tapped())) {
        ++cursor_;
    }
    return true;
}

}  // namespace usersync
