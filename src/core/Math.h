#pragma once

#include <cmath>
#include "core/Types.h"
#include "core/ExpDecayA1K02.h"
#include "core/ExpDecayA1K07.h"
#include "core/ExpDecayA1K09.h"

namespace core::math {

/// y = a * exp(-k * x)
inline double ExpDecay(double x, double a, double k) {
    return a * std::exp(-k * x);
}

// Octile approximation of Euclidean distance: max(dx,dy) + (sqrt(2)-1)*min(dx,dy).
inline float ApproximateEuclideanDistanceOctile(const Vec2f& a, const Vec2f& b) {
    constexpr float kOctileMinWeight = 0.4142F;
    const float dx = std::fabs(a.x - b.x);
    const float dy = std::fabs(a.y - b.y);
    const float maxD = (dx >= dy) ? dx : dy;
    const float minD = (dx >= dy) ? dy : dx;
    return maxD + kOctileMinWeight * minD;
}

inline void InitializeLookupTables() {
    (void)kExpDecayA1K02Lookup;
    (void)kExpDecayA1K07Lookup;
    (void)kExpDecayA1K09Lookup;
}

}  // namespace core::math
