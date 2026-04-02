#include "app/MenuMusicPlayer.h"

#include <algorithm>
#include <chrono>
#include <cstdint>

#include "core/Log.h"
#include "core/Profiling.h"

void MenuMusicPlayer::SetLabel(const char* label) {
    label_ = label;
}

void MenuMusicPlayer::ResetWindowStats() {
    windowUpdateCalls_ = 0;
    windowActiveUpdateCalls_ = 0;
    windowProcessedBuffers_ = 0;
    windowMaxBuffersPerUpdate_ = 0;
    windowSlowUploadCount_ = 0;
    windowMaxUploadUs_ = 0;
    windowTotalUploadUs_ = 0;
}

void MenuMusicPlayer::ReportWindowIfDue(std::uint64_t frameIndex) {
    const auto& profiler = profiling::Profiler::Instance();
    if (!profiler.ShouldEmitPeriodicReport() || frameIndex == lastReportFrame_) {
        return;
    }
    lastReportFrame_ = frameIndex;

    const float avgBufPerCall = windowUpdateCalls_ > 0
        ? static_cast<float>(windowProcessedBuffers_) / static_cast<float>(windowUpdateCalls_)
        : 0.0F;
    const float avgBufPerActive = windowActiveUpdateCalls_ > 0
        ? static_cast<float>(windowProcessedBuffers_) / static_cast<float>(windowActiveUpdateCalls_)
        : 0.0F;
    const float avgUploadMs = windowProcessedBuffers_ > 0
        ? static_cast<float>(windowTotalUploadUs_) / static_cast<float>(windowProcessedBuffers_) / 1000.0F
        : 0.0F;
    const float maxUploadMs = static_cast<float>(windowMaxUploadUs_) / 1000.0F;
    const float totalUploadMs = static_cast<float>(windowTotalUploadUs_) / 1000.0F;

    bolt::log::Profile(
        "[MUSIC_PLAYER_WINDOW] label=%s calls=%llu activeCalls=%llu buffers=%llu"
        " avg(buf/call=%.2f active=%.2f) max(buf/call=%llu)"
        " upload(total=%.2fms avg=%.3fms max=%.3fms slow=%llu)\n",
        label_,
        static_cast<unsigned long long>(windowUpdateCalls_),
        static_cast<unsigned long long>(windowActiveUpdateCalls_),
        static_cast<unsigned long long>(windowProcessedBuffers_),
        avgBufPerCall,
        avgBufPerActive,
        static_cast<unsigned long long>(windowMaxBuffersPerUpdate_),
        totalUploadMs,
        avgUploadMs,
        maxUploadMs,
        static_cast<unsigned long long>(windowSlowUploadCount_));

    ResetWindowStats();
}

bool MenuMusicPlayer::Initialize(MelodyBase& melody) {
    if (initialized_) {
        return true;
    }

    melody_ = &melody;
    melody_->Reset();
    sampleRate_ = melody.SampleRate();
    invSampleRate_ = 1.0F / static_cast<float>(sampleRate_);

    SetAudioStreamBufferSizeDefault(static_cast<int>(kSampleBufferSamples));
    stream_ = LoadAudioStream(sampleRate_, 16, 1);
    if (stream_.buffer == nullptr) {
        return false;
    }

    SetAudioStreamVolume(stream_, 1.0F);
    PlayAudioStream(stream_);

    for (int i = 0; i < 8 && IsAudioStreamProcessed(stream_); ++i) {
        FillBuffer();
        UpdateAudioStream(stream_, sampleBuffer_.data(), static_cast<int>(kSampleBufferSamples));
    }
    PauseAudioStream(stream_);

    sampleCursor_ = 0;
    initialized_ = true;
    enabled_ = false;
    tense_ = false;
    return true;
}

void MenuMusicPlayer::SetEnabled(bool enabled) {
    if (!initialized_ || enabled_ == enabled) {
        return;
    }

    enabled_ = enabled;
    if (enabled_) {
        ResumeAudioStream(stream_);
        for (int i = 0; i < 8 && IsAudioStreamProcessed(stream_); ++i) {
            FillBuffer();
            UpdateAudioStream(stream_, sampleBuffer_.data(), static_cast<int>(kSampleBufferSamples));
        }
    } else {
        PauseAudioStream(stream_);
    }
}

void MenuMusicPlayer::SetTense(bool tense) {
    tense_ = tense;
}

void MenuMusicPlayer::Update() {
    profiling::ScopedProfile scope(profiling::Scope::MenuMusicUpdate);
    const auto& profiler = profiling::Profiler::Instance();
    const std::uint64_t frameIndex = profiler.FrameIndex();

    ++windowUpdateCalls_;
    if (!initialized_ || !enabled_) {
        ReportWindowIfDue(frameIndex);
        return;
    }

    ++windowActiveUpdateCalls_;
    std::uint64_t processedBuffersThisUpdate = 0;
    while (IsAudioStreamProcessed(stream_)) {
        {
            profiling::ScopedProfile fillScope(profiling::Scope::MusicFillBuffer);
            FillBuffer();
        }
        {
            profiling::ScopedProfile uploadScope(profiling::Scope::MusicUpdateStream);
            const auto uploadStart = std::chrono::steady_clock::now();
            UpdateAudioStream(stream_, sampleBuffer_.data(), static_cast<int>(kSampleBufferSamples));
            const std::uint64_t uploadUs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - uploadStart)
                .count());
            windowTotalUploadUs_ += uploadUs;
            windowMaxUploadUs_ = std::max(windowMaxUploadUs_, uploadUs);
            if (uploadUs > kSlowUploadThresholdUs) {
                ++windowSlowUploadCount_;
                bolt::log::Debug(
                    "[MUSIC_UPLOAD_SPIKE] label=%s frame=%llu upload=%.3fms\n",
                    label_,
                    static_cast<unsigned long long>(frameIndex),
                    static_cast<float>(uploadUs) / 1000.0F);
            }
        }
        ++processedBuffersThisUpdate;
    }
    windowProcessedBuffers_ += processedBuffersThisUpdate;
    windowMaxBuffersPerUpdate_ = std::max(windowMaxBuffersPerUpdate_, processedBuffersThisUpdate);

    ReportWindowIfDue(frameIndex);
}

void MenuMusicPlayer::Shutdown() {
    if (!initialized_) {
        return;
    }

    StopAudioStream(stream_);
    UnloadAudioStream(stream_);
    stream_ = AudioStream{};
    melody_ = nullptr;
    initialized_ = false;
    enabled_ = false;
    tense_ = false;
    sampleCursor_ = 0;
}

void MenuMusicPlayer::FillBuffer() {
    for (std::uint32_t i = 0; i < kSampleBufferSamples; ++i) {
        const float t = static_cast<float>(sampleCursor_) * invSampleRate_;
        const float sample = std::clamp(melody_->Synthesize(t, tense_), -1.0F, 1.0F);
        sampleBuffer_[i] = static_cast<short>(sample * 32767.0F);
        ++sampleCursor_;
    }
}
