#pragma once

#include <array>
#include <cassert>
#include <cmath>

namespace core::math {

/// y = a * exp(-k * x)
inline double ExpDecay(double x, double a, double k) {
    return a * std::exp(-k * x);
}

// Precomputed lookup: y = ExpDecay(x, 1.0, 0.7) for integer x in [0, 59].
static constexpr int kExpDecayA1K07MinX = 0;
static constexpr int kExpDecayA1K07MaxX = 59;
static constexpr int kExpDecayA1K07Count = kExpDecayA1K07MaxX - kExpDecayA1K07MinX + 1;

static inline const std::array<double, kExpDecayA1K07Count> kExpDecayA1K07Lookup = [] {
    std::array<double, kExpDecayA1K07Count> table{};
    for (int i = 0; i < kExpDecayA1K07Count; ++i) {
        table[static_cast<std::size_t>(i)] = ExpDecay(static_cast<double>(i), 1.0, 0.7);
    }
    return table;
}();

inline double ExpDecayA1K07(int x) {
    assert(x >= kExpDecayA1K07MinX && x <= kExpDecayA1K07MaxX);
    return kExpDecayA1K07Lookup[static_cast<std::size_t>(x - kExpDecayA1K07MinX)];
}

inline void InitializeLookupTables() {
    (void)kExpDecayA1K07Lookup;
}

}  // namespace core::math
