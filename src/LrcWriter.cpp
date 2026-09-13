#include "LrcWriter.h"

#include <cmath>
#include <cstdio>
#include <fstream>

namespace usersync {

namespace {

// Format seconds as [mm:ss.xx] (centiseconds; LRC standard).
std::string fmt_ts(double t) {
    if (t < 0) t = 0;
    const int total_cs = static_cast<int>(std::lround(t * 100.0));
    const int mm = total_cs / 6000;
    const int ss = (total_cs / 100) % 60;
    const int cs = total_cs % 100;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "[%02d:%02d.%02d]", mm, ss, cs);
    return buf;
}

}  // namespace

std::string format_line_lrc(const std::vector<SyncLine>& lines, const LrcMetadata& meta) {
    std::string out;
    if (!meta.title.empty())  out += "[ti:" + meta.title  + "]\n";
    if (!meta.artist.empty()) out += "[ar:" + meta.artist + "]\n";
    if (!meta.album.empty())  out += "[al:" + meta.album  + "]\n";
    if (!meta.by.empty())     out += "[by:" + meta.by     + "]\n";
    out += "[re:usersync-forked]\n\n";

    for (const auto& line : lines) {
        if (!line.tapped() || line.text.empty()) continue;
        out += fmt_ts(line.t_start);
        out += line.text;
        out += '\n';
    }
    return out;
}

bool write_file(const std::string& path, const std::string& contents, std::string& error) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { error = "could not open " + path + " for writing"; return false; }
    f.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!f) { error = "write to " + path + " failed"; return false; }
    return true;
}

}  // namespace usersync
