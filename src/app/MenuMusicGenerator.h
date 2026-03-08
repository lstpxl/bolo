#pragma once

#include <array>
#include <cstdint>
#include "raylib.h"

class MenuMusicGenerator {
public:
    bool Initialize();
    void SetEnabled(bool enabled);
    void Update();
    void Shutdown();

private:
    void FillBuffer();
    static float SynthesizeBytebeatSample(std::uint32_t t);

    static constexpr std::uint32_t kSampleRate = 22050;
    static constexpr std::uint32_t kSampleBufferSamples = 1024;

    AudioStream stream_{};
    bool initialized_ = false;
    bool enabled_ = false;
    std::uint32_t sampleCursor_ = 0;
    float lowPassState_ = 0.0F;
    std::array<short, kSampleBufferSamples> sampleBuffer_{};
};
