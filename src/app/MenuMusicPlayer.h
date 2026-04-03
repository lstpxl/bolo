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
    // Set a short label used in log output (e.g. "menu", "gameplay"). Not owned.
    void SetLabel(const char* label);
    void Update();
    void Shutdown();

private:
    void FillBuffer();
    void ReportWindowIfDue(std::uint64_t frameIndex);
    void ResetWindowStats();

    // Raylib/miniaudio upload cost is per buffer; larger chunks mean fewer UpdateAudioStream calls
    // (fewer spike opportunities) but more work per call in FillBuffer and worse worst-case stalls.
    // Handheld profiling showed 1024 as noisy, 2048/4096 better for spike rate but heavier on CPU;
    // 1536 is a middle ground tuned for RG353V-style targets.
    static constexpr std::uint32_t kSampleBufferSamples = 1536;
    // Uploads taking longer than this are logged immediately to bolt.log.
    static constexpr std::uint64_t kSlowUploadThresholdUs = 5000;

    MelodyBase* melody_ = nullptr;
    AudioStream stream_{};
    const char* label_ = "music";
    bool initialized_ = false;
    bool enabled_ = false;
    bool tense_ = false;
    std::uint32_t sampleCursor_ = 0;
    std::uint32_t sampleRate_ = 16000;
    float invSampleRate_ = 1.0F / 16000.0F;
    std::array<short, kSampleBufferSamples> sampleBuffer_{};

    // Per-instance rolling-window stats (reset each periodic report).
    std::uint64_t windowUpdateCalls_ = 0;
    std::uint64_t windowActiveUpdateCalls_ = 0;
    std::uint64_t windowProcessedBuffers_ = 0;
    std::uint64_t windowMaxBuffersPerUpdate_ = 0;
    std::uint64_t windowSlowUploadCount_ = 0;  // uploads > kSlowUploadThresholdUs
    std::uint64_t windowMaxUploadUs_ = 0;
    std::uint64_t windowTotalUploadUs_ = 0;
    std::uint64_t lastReportFrame_ = 0;
};
