#include "FileDialog.h"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace usersync {

#ifdef _WIN32

namespace {

// Build a Win32 double-NUL-terminated filter string from a friendly list.
// We hand-roll instead of using a string literal so the embedded nulls survive.
struct FilterBuf {
    char data[256];
    size_t size = 0;

    void add(const char* label, const char* patterns) {
        const size_t lab = std::strlen(label);
        const size_t pat = std::strlen(patterns);
        if (size + lab + pat + 4 > sizeof(data)) return;
        std::memcpy(data + size, label, lab);     size += lab; data[size++] = '\0';
        std::memcpy(data + size, patterns, pat);  size += pat; data[size++] = '\0';
    }
    void terminate() { if (size < sizeof(data)) data[size++] = '\0'; }
};

}  // namespace

bool open_audio_file_dialog(std::string& out_path) {
    char buf[1024] = {0};

    FilterBuf flt;
    flt.add("Audio files", "*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.opus;*.aac;*.wma");
    flt.add("All files",   "*.*");
    flt.terminate();

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = sizeof(buf);
    ofn.lpstrFilter = flt.data;
    ofn.lpstrTitle  = "Select an audio file";
    // OFN_NOCHANGEDIR is important: without it the dialog mutates the process
    // CWD, which breaks relative paths used elsewhere in the app.
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR
              | OFN_EXPLORER      | OFN_HIDEREADONLY;

    if (GetOpenFileNameA(&ofn)) {
        out_path = buf;
        return true;
    }
    return false;
}

bool open_lyrics_file_dialog(std::string& out_path) {
    char buf[1024] = {0};

    FilterBuf flt;
    flt.add("Text files", "*.txt;*.lrc;*.srt");
    flt.add("All files",  "*.*");
    flt.terminate();

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = sizeof(buf);
    ofn.lpstrFilter = flt.data;
    ofn.lpstrTitle  = "Load lyrics from text file";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR
              | OFN_EXPLORER      | OFN_HIDEREADONLY;

    if (GetOpenFileNameA(&ofn)) {
        out_path = buf;
        return true;
    }
    return false;
}

bool save_lrc_file_dialog(std::string& out_path, const std::string& suggested_name) {
    char buf[1024] = {0};
    if (!suggested_name.empty()) {
        const size_t n = std::min(suggested_name.size(), sizeof(buf) - 1);
        std::memcpy(buf, suggested_name.data(), n);
        buf[n] = '\0';
    }

    FilterBuf flt;
    flt.add("LRC lyric files", "*.lrc");
    flt.add("All files",       "*.*");
    flt.terminate();

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = sizeof(buf);
    ofn.lpstrFilter = flt.data;
    ofn.lpstrDefExt = "lrc";
    ofn.lpstrTitle  = "Save .lrc as";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR
              | OFN_EXPLORER        | OFN_HIDEREADONLY;

    if (GetSaveFileNameA(&ofn)) {
        out_path = buf;
        return true;
    }
    return false;
}

bool save_project_dialog(std::string& out_path, const std::string& suggested_name) {
    char buf[1024] = {0};
    if (!suggested_name.empty()) {
        const size_t n = std::min(suggested_name.size(), sizeof(buf) - 1);
        std::memcpy(buf, suggested_name.data(), n);
        buf[n] = '\0';
    }

    FilterBuf flt;
    flt.add("usersync-forked project", "*.usfp");
    flt.add("All files",                "*.*");
    flt.terminate();

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = sizeof(buf);
    ofn.lpstrFilter = flt.data;
    ofn.lpstrDefExt = "usfp";
    ofn.lpstrTitle  = "Save usersync-forked project";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR
              | OFN_EXPLORER        | OFN_HIDEREADONLY;

    if (GetSaveFileNameA(&ofn)) {
        out_path = buf;
        return true;
    }
    return false;
}

bool open_project_dialog(std::string& out_path) {
    char buf[1024] = {0};

    FilterBuf flt;
    flt.add("usersync-forked project", "*.usfp");
    flt.add("All files",                "*.*");
    flt.terminate();

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = sizeof(buf);
    ofn.lpstrFilter = flt.data;
    ofn.lpstrTitle  = "Open usersync-forked project";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR
              | OFN_EXPLORER      | OFN_HIDEREADONLY;

    if (GetOpenFileNameA(&ofn)) {
        out_path = buf;
        return true;
    }
    return false;
}

#else

bool open_audio_file_dialog(std::string&) { return false; }
bool save_lrc_file_dialog(std::string&, const std::string&) { return false; }
bool open_lyrics_file_dialog(std::string&) { return false; }
bool save_project_dialog(std::string&, const std::string&) { return false; }
bool open_project_dialog(std::string&) { return false; }

#endif

}  // namespace usersync
