#pragma once

#include <array>
#include <cassert>
#include <cmath>

namespace core::math {

// Precomputed lookup: y = 1.0 * exp(-0.2 * x) for integer x in [0, 59].
static constexpr int kExpDecayA1K02MinX = 0;
static constexpr int kExpDecayA1K02MaxX = 59;
static constexpr int kExpDecayA1K02Count = kExpDecayA1K02MaxX - kExpDecayA1K02MinX + 1;

static inline const std::array<double, kExpDecayA1K02Count> kExpDecayA1K02Lookup = [] {
    std::array<double, kExpDecayA1K02Count> table{};
    for (int i = 0; i < kExpDecayA1K02Count; ++i) {
        table[static_cast<std::size_t>(i)] = std::exp(-0.2 * static_cast<double>(i));
    }
    return table;
}();

inline double ExpDecayA1K02(int x) {
    assert(x >= kExpDecayA1K02MinX && x <= kExpDecayA1K02MaxX);
    return kExpDecayA1K02Lookup[static_cast<std::size_t>(x - kExpDecayA1K02MinX)];
}

}  // namespace core::math
