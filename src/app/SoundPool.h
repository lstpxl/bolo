#pragma once

#include <array>
#include <string>

#include "raylib.h"

template <std::size_t N>
class SoundPool {
public:
    static_assert(N > 0, "SoundPool requires at least one slot");

    bool Load(const std::string& path) {
        if (!FileExists(path.c_str())) {
            return false;
        }

        Unload();

        sounds_[0] = LoadSound(path.c_str());
        if (sounds_[0].frameCount <= 0) {
            sounds_[0] = Sound{};
            return false;
        }

        loadedCount_ = 1;
        for (; loadedCount_ < N; ++loadedCount_) {
            sounds_[loadedCount_] = LoadSoundAlias(sounds_[0]);
            if (sounds_[loadedCount_].frameCount <= 0) {
                break;
            }
        }

        if (loadedCount_ < N) {
            // Partial pool load failed; release anything that was created.
            Unload();
            return false;
        }

        cursor_ = 0;
        loaded_ = true;
        return true;
    }

    void Unload() {
        if (!loaded_) {
            return;
        }

        for (std::size_t i = 1; i < loadedCount_; ++i) {
            UnloadSoundAlias(sounds_[i]);
            sounds_[i] = Sound{};
        }
        if (loadedCount_ > 0) {
            UnloadSound(sounds_[0]);
            sounds_[0] = Sound{};
        }

        loaded_ = false;
        loadedCount_ = 0;
        cursor_ = 0;
    }

    void Play(float volume) {
        if (!loaded_) {
            return;
        }

        Sound& sound = sounds_[cursor_];
        SetSoundVolume(sound, volume);
        PlaySound(sound);
        cursor_ = (cursor_ + 1) % N;
    }

private:
    std::array<Sound, N> sounds_{};
    bool loaded_ = false;
    std::size_t loadedCount_ = 0;
    std::size_t cursor_ = 0;
};
