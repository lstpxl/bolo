#pragma once

#include <array>
#include <cassert>
#include <cmath>

namespace core::math {

// Precomputed lookup: y = 1.0 * exp(-0.7 * x) for integer x in [0, 59].
static constexpr int kExpDecayA1K07MinX = 0;
static constexpr int kExpDecayA1K07MaxX = 59;
static constexpr int kExpDecayA1K07Count = kExpDecayA1K07MaxX - kExpDecayA1K07MinX + 1;

static inline const std::array<double, kExpDecayA1K07Count> kExpDecayA1K07Lookup = [] {
    std::array<double, kExpDecayA1K07Count> table{};
    for (int i = 0; i < kExpDecayA1K07Count; ++i) {
        table[static_cast<std::size_t>(i)] = std::exp(-0.7 * static_cast<double>(i));
    }
    return table;
}();

inline double ExpDecayA1K07(int x) {
    assert(x >= kExpDecayA1K07MinX && x <= kExpDecayA1K07MaxX);
    return kExpDecayA1K07Lookup[static_cast<std::size_t>(x - kExpDecayA1K07MinX)];
}

}  // namespace core::math
