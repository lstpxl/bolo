#pragma once

#include <cstdint>
#include "app/MelodyBase.h"

// "Funcbeat" procedural melody for the main menu.
// Combines a sweeping resonant filter, detuned square oscillators,
// LFO pulse-width modulation, and a sparse impulse trigger.
// Grand cycle: exactly 30 s — all components realign simultaneously:
//   5 × LFO (6 s, ω = π/3) = 7 × filter sweep (30/7 s, ω = 7π/15) = 6 × beat (5 s).
// The 7:5 ratio between sweep and LFO is preserved from the original.
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
