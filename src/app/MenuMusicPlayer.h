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
    void SetTense(bool tense);
    void Update();
    void Shutdown();

private:
    void FillBuffer();

    static constexpr std::uint32_t kSampleBufferSamples = 1024;

    MelodyBase* melody_ = nullptr;
    AudioStream stream_{};
    bool initialized_ = false;
    bool enabled_ = false;
    bool tense_ = false;
    std::uint32_t sampleCursor_ = 0;
    std::uint32_t sampleRate_ = 16000;
    float invSampleRate_ = 1.0F / 16000.0F;
    std::array<short, kSampleBufferSamples> sampleBuffer_{};
};
