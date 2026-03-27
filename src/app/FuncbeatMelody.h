#pragma once

#include <cstdint>
#include "app/MelodyBase.h"

// "Funcbeat" procedural melody for the main menu.
// Combines a sweeping resonant filter, detuned square oscillators,
// LFO pulse-width modulation, and a sparse impulse trigger.
// Perceptual macro-cycle: ~31.4 s (10π), driven by the 7:5 ratio
// between the filter sweep (2π/1.4 s) and the LFO (2π s).
class FuncbeatMelody : public MelodyBase {
public:
    void Reset() override;
    float Synthesize(float t) override;

private:
    // 4-pole cascaded resonant filter state (position + velocity per stage).
    float filterP_[4] = {};
    float filterV_[4] = {};
    std::uint32_t rngState_ = 2463534242U;  // nonzero seed for xorshift32
};
