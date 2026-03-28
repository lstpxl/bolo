#pragma once

#include "app/MelodyBase.h"

// Gameplay

// Resonant-bass + kick melody for the main menu.
// Architecture: PWM pulse wave → resonant spring-mass lowpass → mix with
// pitch-swept kick sine. Note sequence cycles 16 steps at 4.3 steps/sec.
//
// Filter denominator is scaled from the original 48 kHz formula by 1/3
// (= 16000/48000), preserving identical filter character at 16 kHz.
class ResonantBeatMelody : public MelodyBase {
public:
    void Reset() override;
    float Synthesize(float t, bool tense) override;

private:
    // Resonant lowpass state (spring-mass: position + velocity).
    float lastSample_ = 0.0F;
    float resonanceMomentum_ = 0.0F;
    // Cached pitch to avoid std::pow on every sample (note changes at ~4.3 Hz).
    int lastNoteIdx_ = -1;
    float cachedPitch_ = 0.0F;
    // Tonality changes are quantized to note boundaries.
    bool pendingTense_ = false;
    bool activeTense_ = false;
};
