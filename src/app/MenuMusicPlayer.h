#pragma once

#include <array>
#include <cstdint>
#include "app/MelodyBase.h"
#include "raylib.h"

// Drives an AudioStream from any MelodyBase implementation.
// Owns the stream lifecycle; the melody is held by pointer (caller owns it).
class MenuMusicPlayer {
public:
    bool Initialize(MelodyBase& melody);
    void SetEnabled(bool enabled);
    void Update();
    void Shutdown();

    static constexpr std::uint32_t kSampleRate = 16000;

private:
    void FillBuffer();

    static constexpr std::uint32_t kSampleBufferSamples = 1024;

    MelodyBase* melody_ = nullptr;
    AudioStream stream_{};
    bool initialized_ = false;
    bool enabled_ = false;
    std::uint32_t sampleCursor_ = 0;
    std::array<short, kSampleBufferSamples> sampleBuffer_{};
};
