#include "app/MenuMusicPlayer.h"

#include <algorithm>
#include <cstdint>

#include "core/Profiling.h"

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

void MenuMusicPlayer::Update() {
    profiling::ScopedProfile scope(profiling::Scope::MenuMusicUpdate);
    if (!initialized_ || !enabled_) {
        return;
    }

    while (IsAudioStreamProcessed(stream_)) {
        FillBuffer();
        UpdateAudioStream(stream_, sampleBuffer_.data(), static_cast<int>(kSampleBufferSamples));
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
    sampleCursor_ = 0;
}

void MenuMusicPlayer::FillBuffer() {
    for (std::uint32_t i = 0; i < kSampleBufferSamples; ++i) {
        const float t = static_cast<float>(sampleCursor_) * invSampleRate_;
        const float sample = std::clamp(melody_->Synthesize(t), -1.0F, 1.0F);
        sampleBuffer_[i] = static_cast<short>(sample * 32767.0F);
        ++sampleCursor_;
    }
}
