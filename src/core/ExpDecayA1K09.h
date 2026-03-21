#pragma once

#include <array>
#include <cassert>
#include <cmath>

namespace core::math {

// Precomputed lookup: y = 0.9 * exp(-0.7 * x) for integer x in [0, 59].
static constexpr int kExpDecayA1K09MinX = 0;
static constexpr int kExpDecayA1K09MaxX = 59;
static constexpr int kExpDecayA1K09Count = kExpDecayA1K09MaxX - kExpDecayA1K09MinX + 1;

static inline const std::array<double, kExpDecayA1K09Count> kExpDecayA1K09Lookup = [] {
    std::array<double, kExpDecayA1K09Count> table{};
    for (int i = 0; i < kExpDecayA1K09Count; ++i) {
        table[static_cast<std::size_t>(i)] =
            0.9 * std::exp(-0.7 * static_cast<double>(i));
    }
    return table;
}();

inline double ExpDecayA1K09(int x) {
    assert(x >= kExpDecayA1K09MinX && x <= kExpDecayA1K09MaxX);
    return kExpDecayA1K09Lookup[static_cast<std::size_t>(x - kExpDecayA1K09MinX)];
}

}  // namespace core::math
