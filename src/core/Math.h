#pragma once

#include <cmath>
#include "core/ExpDecayA1K07.h"
#include "core/ExpDecayA1K09.h"

namespace core::math {

/// y = a * exp(-k * x)
inline double ExpDecay(double x, double a, double k) {
    return a * std::exp(-k * x);
}

inline void InitializeLookupTables() {
    (void)kExpDecayA1K07Lookup;
    (void)kExpDecayA1K09Lookup;
}

}  // namespace core::math
