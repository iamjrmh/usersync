#pragma once

#include <string>

// Forward-declare at GLOBAL scope. Inside `namespace usersync`, an elaborated
// `struct ma_engine` would otherwise create `usersync::ma_engine`, which then
// won't convert to the real `::ma_engine` from <miniaudio.h>.
struct ma_engine;
struct ma_sound;

namespace usersync {

// Wraps miniaudio's high-level ma_engine + ma_sound for tap-sync playback.
// One AudioPlayer plays one file at a time at its native rate / channel
// count.
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    // Loads and immediately decodes the file (so seeking is instant). Returns
    // false and fills `error` on failure. Replaces any previously loaded sound.
    bool load(const std::string& path, std::string& error);
    void unload();
    bool is_loaded() const { return sound_inited_; }
    const std::string& loaded_path() const { return loaded_path_; }

    void play();
    void pause();
    void toggle();
    void stop();

    // Seek to an absolute position in seconds.
    void   seek_seconds(double s);
    double position_seconds() const;
    double duration_seconds() const;
    bool   is_playing() const;

private:
    bool ensure_engine(std::string& error);

    ma_engine* engine_ = nullptr;  // owned (heap-allocated to keep header light)
    ma_sound*  sound_  = nullptr;
    bool              engine_inited_ = false;
    bool              sound_inited_  = false;
    std::string       loaded_path_;
};

}  // namespace usersync
