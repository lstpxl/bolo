#include "app/ResonantBeatMelody.h"

#include <cmath>

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
/// Output gain: 0.7 ≈ 30% quieter than unity.
constexpr float kOutputGain = 0.7F;
}  // namespace

void ResonantBeatMelody::Reset() {
    lastSample_ = 0.0F;
    resonanceMomentum_ = 0.0F;
    lastNoteIdx_ = -1;
    cachedPitch_ = 0.0F;
}

float ResonantBeatMelody::Synthesize(float t) {
    // Step through 16 notes at 4.3 steps/sec.
    // std::pow recomputed only on note change (~4.3× per second, not per sample).
    const int noteIdx = static_cast<int>(t * 4.3F) & 15;
    if (noteIdx != lastNoteIdx_) {
        lastNoteIdx_ = noteIdx;
        cachedPitch_ = std::pow(2.0F, (static_cast<float>(kNoteData[noteIdx]) + 22.0F) / 12.0F);
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

    // Kick drum: pitch-swept sine triggered at 4.3/2 ≈ 2.15 Hz.
    // The (x+0.01)^0.3 envelope gives a fast high-to-low frequency sweep.
    const float kickPhase = std::fmod(t * 4.3F, 2.0F);
    const float kick = std::sin(std::pow(kickPhase + 0.01F, 0.3F) * 180.0F) * 0.25F;

    return (lastSample_ + kick) * kOutputGain;
}
