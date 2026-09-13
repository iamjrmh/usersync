#pragma once

#include <string>
#include <vector>

namespace usersync {

// One lyric line. `t_start` is the timestamp (seconds) of the line's FIRST
// word -- that's all this fork tracks, unlike upstream usersync's per-word
// timing. -1 means "not tapped yet".
struct SyncLine {
    std::string text;
    double      t_start = -1.0;

    bool tapped() const { return t_start >= 0.0; }
};

struct LrcMetadata {
    std::string title;
    std::string artist;
    std::string album;
    std::string by = "usersync-forked";
};

// Owns the in-memory sync session: the lyric lines, their tapped timestamps,
// and which line is "next up" for tapping. No worker thread, no model, no
// transcription -- everything here runs synchronously on the UI thread.
class App {
public:
    // Splits `raw_text` on newlines into fresh SyncLines (all untapped) and
    // resets the tap cursor to the first line. Blank lines are kept (a blank
    // line in the source lyrics becomes a blank line in the output, e.g. for
    // verse gaps) but are auto-skipped by tap()/advance() since there's no
    // "first word" to tap.
    void set_lyrics(const std::string& raw_text);

    std::vector<SyncLine>&       lines()       { return lines_; }
    const std::vector<SyncLine>& lines() const { return lines_; }

    int  cursor() const { return cursor_; }
    bool finished() const;

    // Records `t_sec` as the current line's timestamp and advances the
    // cursor to the next (non-blank) line. No-ops if already finished.
    void tap(double t_sec);
    // Clears the previous line's timestamp and moves the cursor back to it.
    // No-ops if the cursor is already at the first line.
    void untap_last();
    // Clears every timestamp and rewinds the cursor to the first line.
    void reset_taps();
    // Jump the cursor to an arbitrary line (used by the Editor tab's
    // "tap from here" button).
    void set_cursor(int idx);

    int  tapped_count() const;

    LrcMetadata& meta() { return meta_; }
    const LrcMetadata& meta() const { return meta_; }

    // .usfp project file: plain text, one directive per line. Captures the
    // audio path, metadata, raw lyrics, and tapped timestamps so a session
    // can be resumed later.
    bool save_project(const std::string& path, const std::string& audio_path,
                       std::string& error) const;
    // On success, fills `out_audio_path` and replaces this App's lines/meta.
    bool load_project(const std::string& path, std::string& out_audio_path,
                       std::string& error);

private:
    void advance_cursor_from(int start);

    std::vector<SyncLine> lines_;
    int                   cursor_ = 0;
    LrcMetadata           meta_;
};

}  // namespace usersync
