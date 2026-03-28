#include "app/ResonantBeatMelody.h"

#include <cmath>
#include <cstdint>

// Example 2022-04-24 Funcbeat 48000Hz
// Basic explanations about Funcbeat in comments.

// Source formula:
//   let lastSample = 0, resonanceMomentum = 0;
//   const notedata = "$$$000,,,,,,,,''";
//   return function(time, sampleRate) {
//     const pitch = 2 ** ((notedata.charCodeAt(time * 4.3 & 15) + 22) / 12);
//     const pulse = ((time * pitch % 1 > (time / 2 % 1) * .6 + .2) - .5) / 2;
//     lastSample += resonanceMomentum += (pulse - lastSample - resonanceMomentum * 3) / (cos(time / 5) * 170 + 200);
//     const kick = (sin((time * 4.3 % 2 + .01) ** .3 * 180)) / 4;
//     return lastSample + kick;
//   };

namespace {
// notedata = "$$$000,,,,,,,,''", ASCII charCodeAt values for each of 16 steps:
//   '$'=36  '0'=48  ','=44  '\''=39
constexpr int kNoteData[16] = {36, 36, 36, 48, 48, 48, 44, 44, 44, 44, 44, 44, 44, 44, 39, 39};
constexpr int kNoteDataCPhrygian[16] = {
    36, 36, 37, 36, 39, 37, 44, 43,
    41, 39, 37, 36, 46, 44, 37, 36
};
constexpr float kInvUint32 = 1.0F / 4294967295.0F;

float WhiteNoiseFromSample(std::uint32_t sampleIdx) {
    // Deterministic integer hash -> white noise in [-1, 1].
    std::uint32_t x = sampleIdx;
    x ^= x >> 16U;
    x *= 0x7feb352dU;
    x ^= x >> 15U;
    x *= 0x846ca68bU;
    x ^= x >> 16U;
    return static_cast<float>(x) * kInvUint32 * 2.0F - 1.0F;
}
/// Output gain: 0.7 ≈ 30% quieter than unity.
constexpr float kOutputGain = 0.7F;
}  // namespace

void ResonantBeatMelody::Reset() {
    lastSample_ = 0.0F;
    resonanceMomentum_ = 0.0F;
    lastNoteIdx_ = -1;
    cachedPitch_ = 0.0F;
    pendingTense_ = false;
    activeTense_ = false;
}

float ResonantBeatMelody::Synthesize(float t, bool tense) {
    pendingTense_ = tense;

    // Step through 16 notes at 4.3 steps/sec.
    // std::pow recomputed only on note change (~4.3× per second, not per sample).
    const int noteIdx = static_cast<int>(t * 4.3F) & 15;
    if (noteIdx != lastNoteIdx_) {
        lastNoteIdx_ = noteIdx;
        // Apply tonality switches only when a new note starts.
        activeTense_ = pendingTense_;
        const int note = activeTense_ ? kNoteDataCPhrygian[noteIdx] : kNoteData[noteIdx];
        cachedPitch_ = std::pow(2.0F, (static_cast<float>(note) + 22.0F) / 12.0F);
    }

    // Pulse wave with 0.5 Hz LFO-modulated width (sweeps 0.2–0.8).
    const float pulsePhase = std::fmod(t * cachedPitch_, 1.0F);
    const float pulseWidth = std::fmod(t * 0.5F, 1.0F) * 0.6F + 0.2F;
    const float pulse = ((pulsePhase > pulseWidth ? 1.0F : 0.0F) - 0.5F) * 0.5F;

    // Resonant lowpass — spring-mass IIR.
    // Original denominator range [30, 370] was tuned for 48 kHz.
    // Scaled by 16000/48000 = 1/3 → [10, 123] to preserve filter character at 16 kHz.
    const float D = std::cos(t / 5.0F) * (170.0F / 3.0F) + (200.0F / 3.0F);
    resonanceMomentum_ += (pulse - lastSample_ - resonanceMomentum_ * 3.0F) / D;
    lastSample_ += resonanceMomentum_;

    // Tense mode: kick drum (pitch-swept sine), triggered at 4.3/2 ≈ 2.15 Hz.
    const float kickPhase = std::fmod(t * 4.3F, 2.0F);
    const float kick = std::sin(std::pow(kickPhase + 0.01F, 0.3F) * 180.0F) * 0.25F;

    // Non-tense hi-hat: short "sss" burst from high-passed white noise.
    const float hatPhase = std::fmod(t * 4.3F, 1.0F);
    const bool strongBeat = (noteIdx & 1) == 0;
    const float hatGateSeconds = strongBeat ? 0.20F : 0.12F;
    const float hatDecay = strongBeat ? 80.0F : 230.0F;
    const float hatGain = strongBeat ? 0.02F : 0.1F;
    const float hatEnv = hatPhase < hatGateSeconds ? std::exp(-hatPhase * hatDecay) : 0.0F;
    const std::uint32_t sampleIdx = static_cast<std::uint32_t>(t * 16000.0F);
    const float white = WhiteNoiseFromSample(sampleIdx);
    const float whitePrev = WhiteNoiseFromSample(sampleIdx > 0U ? sampleIdx - 1U : 0U);
    const float hatNoise = white - whitePrev;
    const float hiHat = hatNoise * hatEnv * hatGain;

    const float quietKick = kick * 0.2F;
    const float percussion = activeTense_ ? kick : (quietKick + hiHat);
    return (lastSample_ + percussion) * kOutputGain;
}
