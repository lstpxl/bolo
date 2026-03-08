#include "app/MenuMusicGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core/Profiling.h"

namespace {
constexpr float kOutputGain = 0.30F;
constexpr float kLowPassAlpha = 0.18F;
constexpr float kPi = 3.14159265358979323846F;
constexpr std::uint32_t kTimeScale = 2U;
constexpr char kMenuBytebeatFormula[] = R"(f=(x,w=3,y=4,z=2,q=128,a=k=>sin(k*PI/4))=>(i=x/q,k=abs(i-(i|0))**z*y,t||(l=0),l=a(k*w)*(i%1-1)),f(w=t/4*[1,2,1,2,1,3,2.7,2.4,1,2,1,2,1,3,2.7,3,3.2,2.4,1.2,2.4,3.6,1.8,3.2,1.8,3,1.5,3,1.5,3,1.5,3,1.5][(t>>13)%32]*1.4,(.7/PI*t/1024%32-16)*2,3,1,256)*64)";
constexpr float kPitchPattern[32] = {
    1.0F, 2.0F, 1.0F, 2.0F, 1.0F, 3.0F, 2.7F, 2.4F, 1.0F, 2.0F, 1.0F, 2.0F, 1.0F, 3.0F, 2.7F, 3.0F,
    3.2F, 2.4F, 1.2F, 2.4F, 3.6F, 1.8F, 3.2F, 1.8F, 3.0F, 1.5F, 3.0F, 1.5F, 3.0F, 1.5F, 3.0F, 1.5F,
};
}  // namespace

bool MenuMusicGenerator::Initialize() {
    if (initialized_) {
        return true;
    }

    // Keep stream internal buffer aligned with our update chunk size to avoid
    // partially filled (mostly zero) sub-buffers that sound like burst+silence cycles.
    SetAudioStreamBufferSizeDefault(static_cast<int>(kSampleBufferSamples));
    stream_ = LoadAudioStream(kSampleRate, 16, 1);
    if (stream_.buffer == nullptr) {
        return false;
    }

    SetAudioStreamVolume(stream_, 1.0F);
    PlayAudioStream(stream_);

    // Prime any available processed buffers once, then pause until menu is active.
    for (int i = 0; i < 8 && IsAudioStreamProcessed(stream_); ++i) {
        FillBuffer();
        UpdateAudioStream(stream_, sampleBuffer_.data(), static_cast<int>(kSampleBufferSamples));
    }
    PauseAudioStream(stream_);

    sampleCursor_ = 0;
    lowPassState_ = 0.0F;
    initialized_ = true;
    enabled_ = false;
    return true;
}

void MenuMusicGenerator::SetEnabled(bool enabled) {
    if (!initialized_ || enabled_ == enabled) {
        return;
    }

    enabled_ = enabled;
    if (enabled_) {
        ResumeAudioStream(stream_);
        // Re-prime immediately on resume to prevent stale/zero buffer gaps.
        for (int i = 0; i < 8 && IsAudioStreamProcessed(stream_); ++i) {
            FillBuffer();
            UpdateAudioStream(stream_, sampleBuffer_.data(), static_cast<int>(kSampleBufferSamples));
        }
    } else {
        PauseAudioStream(stream_);
    }
}

void MenuMusicGenerator::Update() {
    profiling::ScopedProfile scope(profiling::Scope::MenuMusicUpdate);
    if (!initialized_ || !enabled_) {
        return;
    }

    while (IsAudioStreamProcessed(stream_)) {
        FillBuffer();
        UpdateAudioStream(stream_, sampleBuffer_.data(), static_cast<int>(kSampleBufferSamples));
    }
}

void MenuMusicGenerator::Shutdown() {
    if (!initialized_) {
        return;
    }

    StopAudioStream(stream_);
    UnloadAudioStream(stream_);
    stream_ = AudioStream{};
    initialized_ = false;
    enabled_ = false;
    sampleCursor_ = 0;
    lowPassState_ = 0.0F;
}

void MenuMusicGenerator::FillBuffer() {
    for (std::uint32_t i = 0; i < kSampleBufferSamples; ++i) {
        const float target = SynthesizeBytebeatSample(sampleCursor_ * kTimeScale);
        lowPassState_ += (target - lowPassState_) * kLowPassAlpha;
        const float sample = std::clamp(lowPassState_ * kOutputGain, -1.0F, 1.0F);
        sampleBuffer_[i] = static_cast<short>(sample * 32767.0F);
        ++sampleCursor_;
    }
}

float MenuMusicGenerator::SynthesizeBytebeatSample(std::uint32_t t) {
    // Source formula kept verbatim for quick retuning/reference:
    // kMenuBytebeatFormula
    static_cast<void>(kMenuBytebeatFormula);

    static float l = 0.0F;
    if (t == 0U) {
        l = 0.0F;
    }

    const float tf = static_cast<float>(t);
    const float x = (tf / 4.0F) * kPitchPattern[(t >> 13U) & 31U] * 1.4F;
    const float w = (std::fmod((0.7F / kPi) * tf / 1024.0F, 32.0F) - 16.0F) * 2.0F;
    const float y = 3.0F;
    const float z = 1.0F;
    const float q = 256.0F;

    const float i = x / q;
    const float truncI = static_cast<float>(static_cast<int>(i));
    const float k = std::pow(std::fabs(i - truncI), z) * y;
    const float fractI = i - std::floor(i);
    l = std::sin(k * w * (kPi / 4.0F)) * (fractI - 1.0F);

    const float expressionValue = l * 64.0F;
    return std::clamp(expressionValue / 64.0F, -1.0F, 1.0F);
}
