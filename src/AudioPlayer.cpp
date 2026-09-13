#include "AudioPlayer.h"

#include "miniaudio.h"

#include <new>

namespace usersync {

AudioPlayer::AudioPlayer() = default;

AudioPlayer::~AudioPlayer() {
    unload();
    if (engine_inited_ && engine_) {
        ma_engine_uninit(engine_);
        engine_inited_ = false;
    }
    delete engine_;
    delete sound_;
}

bool AudioPlayer::ensure_engine(std::string& error) {
    if (engine_inited_) return true;
    if (!engine_) engine_ = new (std::nothrow) ma_engine{};
    if (!engine_) { error = "out of memory"; return false; }
    if (ma_engine_init(nullptr, engine_) != MA_SUCCESS) {
        error = "ma_engine_init failed (no audio device?)";
        return false;
    }
    engine_inited_ = true;
    return true;
}

bool AudioPlayer::load(const std::string& path, std::string& error) {
    unload();
    if (!ensure_engine(error)) return false;
    if (!sound_) sound_ = new (std::nothrow) ma_sound{};
    if (!sound_) { error = "out of memory"; return false; }

    // MA_SOUND_FLAG_DECODE = fully decode up front so seeking is instant.
    // MA_SOUND_FLAG_NO_SPATIALIZATION skips 3D math we don't need.
    const ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_file(engine_, path.c_str(), flags,
                                nullptr, nullptr, sound_) != MA_SUCCESS) {
        error = "ma_sound_init_from_file failed for " + path;
        return false;
    }
    sound_inited_ = true;
    loaded_path_  = path;
    return true;
}

void AudioPlayer::unload() {
    if (sound_inited_ && sound_) {
        ma_sound_stop(sound_);
        ma_sound_uninit(sound_);
        sound_inited_ = false;
    }
    loaded_path_.clear();
}

void AudioPlayer::play()  { if (sound_inited_) ma_sound_start(sound_); }
void AudioPlayer::pause() { if (sound_inited_) ma_sound_stop (sound_); }
void AudioPlayer::stop() {
    if (!sound_inited_) return;
    ma_sound_stop(sound_);
    ma_sound_seek_to_pcm_frame(sound_, 0);
}
void AudioPlayer::toggle() { is_playing() ? pause() : play(); }

void AudioPlayer::seek_seconds(double s) {
    if (!sound_inited_) return;
    ma_uint32 sr = 0;
    ma_sound_get_data_format(sound_, nullptr, nullptr, &sr, nullptr, 0);
    if (sr == 0) return;
    if (s < 0) s = 0;
    const ma_uint64 frame = static_cast<ma_uint64>(s * sr);
    ma_sound_seek_to_pcm_frame(sound_, frame);
}

double AudioPlayer::position_seconds() const {
    if (!sound_inited_) return 0.0;
    float pos = 0.0f;
    ma_sound_get_cursor_in_seconds(const_cast<ma_sound*>(sound_), &pos);
    return static_cast<double>(pos);
}

double AudioPlayer::duration_seconds() const {
    if (!sound_inited_) return 0.0;
    float len = 0.0f;
    ma_sound_get_length_in_seconds(const_cast<ma_sound*>(sound_), &len);
    return static_cast<double>(len);
}

bool AudioPlayer::is_playing() const {
    if (!sound_inited_) return false;
    return ma_sound_is_playing(const_cast<ma_sound*>(sound_)) == MA_TRUE;
}

}  // namespace usersync
