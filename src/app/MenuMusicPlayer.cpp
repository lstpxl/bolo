#include "app/MenuMusicPlayer.h"

#include <algorithm>
#include <cstdint>

#include "core/Log.h"
#include "core/Profiling.h"

namespace {
struct MusicPlayerWindowStats {
    std::uint64_t updateCalls = 0;
    std::uint64_t activeUpdateCalls = 0;
    std::uint64_t processedBuffers = 0;
    std::uint64_t maxBuffersPerUpdate = 0;
};

MusicPlayerWindowStats gMusicPlayerWindowStats{};
std::uint64_t gLastMusicPlayerWindowPrintedFrame = 0;
}  // namespace

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
    gMusicPlayerWindowStats.updateCalls += 1;
    if (!initialized_ || !enabled_) {
        const auto& profiler = profiling::Profiler::Instance();
        const std::uint64_t frameIndex = profiler.FrameIndex();
        if (profiler.ShouldEmitPeriodicReport() && frameIndex != gLastMusicPlayerWindowPrintedFrame) {
            gLastMusicPlayerWindowPrintedFrame = frameIndex;
            const float avgBuffersPerUpdate = gMusicPlayerWindowStats.updateCalls > 0
                ? static_cast<float>(gMusicPlayerWindowStats.processedBuffers) /
                    static_cast<float>(gMusicPlayerWindowStats.updateCalls)
                : 0.0F;
            const float avgBuffersPerActiveUpdate = gMusicPlayerWindowStats.activeUpdateCalls > 0
                ? static_cast<float>(gMusicPlayerWindowStats.processedBuffers) /
                    static_cast<float>(gMusicPlayerWindowStats.activeUpdateCalls)
                : 0.0F;
            bolt::log::Profile(
                "[MUSIC_PLAYER_WINDOW] calls=%llu activeCalls=%llu buffers=%llu avg(buf/call=%.2f active=%.2f) max(buf/call=%llu)\n",
                static_cast<unsigned long long>(gMusicPlayerWindowStats.updateCalls),
                static_cast<unsigned long long>(gMusicPlayerWindowStats.activeUpdateCalls),
                static_cast<unsigned long long>(gMusicPlayerWindowStats.processedBuffers),
                avgBuffersPerUpdate,
                avgBuffersPerActiveUpdate,
                static_cast<unsigned long long>(gMusicPlayerWindowStats.maxBuffersPerUpdate));
            gMusicPlayerWindowStats = MusicPlayerWindowStats{};
        }
        return;
    }

    gMusicPlayerWindowStats.activeUpdateCalls += 1;
    std::uint64_t processedBuffersThisUpdate = 0;
    while (IsAudioStreamProcessed(stream_)) {
        {
            profiling::ScopedProfile fillScope(profiling::Scope::MusicFillBuffer);
            FillBuffer();
        }
        {
            profiling::ScopedProfile uploadScope(profiling::Scope::MusicUpdateStream);
            UpdateAudioStream(stream_, sampleBuffer_.data(), static_cast<int>(kSampleBufferSamples));
        }
        processedBuffersThisUpdate += 1;
    }
    gMusicPlayerWindowStats.processedBuffers += processedBuffersThisUpdate;
    gMusicPlayerWindowStats.maxBuffersPerUpdate =
        std::max<std::uint64_t>(gMusicPlayerWindowStats.maxBuffersPerUpdate, processedBuffersThisUpdate);

    const auto& profiler = profiling::Profiler::Instance();
    const std::uint64_t frameIndex = profiler.FrameIndex();
    if (profiler.ShouldEmitPeriodicReport() && frameIndex != gLastMusicPlayerWindowPrintedFrame) {
        gLastMusicPlayerWindowPrintedFrame = frameIndex;
        const float avgBuffersPerUpdate = gMusicPlayerWindowStats.updateCalls > 0
            ? static_cast<float>(gMusicPlayerWindowStats.processedBuffers) /
                static_cast<float>(gMusicPlayerWindowStats.updateCalls)
            : 0.0F;
        const float avgBuffersPerActiveUpdate = gMusicPlayerWindowStats.activeUpdateCalls > 0
            ? static_cast<float>(gMusicPlayerWindowStats.processedBuffers) /
                static_cast<float>(gMusicPlayerWindowStats.activeUpdateCalls)
            : 0.0F;
        bolt::log::Profile(
            "[MUSIC_PLAYER_WINDOW] calls=%llu activeCalls=%llu buffers=%llu avg(buf/call=%.2f active=%.2f) max(buf/call=%llu)\n",
            static_cast<unsigned long long>(gMusicPlayerWindowStats.updateCalls),
            static_cast<unsigned long long>(gMusicPlayerWindowStats.activeUpdateCalls),
            static_cast<unsigned long long>(gMusicPlayerWindowStats.processedBuffers),
            avgBuffersPerUpdate,
            avgBuffersPerActiveUpdate,
            static_cast<unsigned long long>(gMusicPlayerWindowStats.maxBuffersPerUpdate));
        gMusicPlayerWindowStats = MusicPlayerWindowStats{};
    }
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
