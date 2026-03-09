#pragma once

#include <cmath>
#include "core/Types.h"

namespace core::angle {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kEightDirectionStep = kPi / 4.0F;

inline float NormalizeAngle(float angleRadians) {
    const float twoPi = kPi * 2.0F;
    float normalized = std::fmod(angleRadians, twoPi);
    if (normalized < 0.0F) {
        normalized += twoPi;
    }
    return normalized;
}

inline float SignedAngleDelta(float fromRadians, float toRadians) {
    float delta = NormalizeAngle(toRadians) - NormalizeAngle(fromRadians);
    if (delta > kPi) {
        delta -= kPi * 2.0F;
    } else if (delta < -kPi) {
        delta += kPi * 2.0F;
    }
    return delta;
}

inline float AngleDistance(float aRadians, float bRadians) {
    const float twoPi = kPi * 2.0F;
    const float a = NormalizeAngle(aRadians);
    const float b = NormalizeAngle(bRadians);
    const float diff = std::fabs(a - b);
    return std::fmin(diff, twoPi - diff);
}

inline float QuantizeToEightDirections(float angleRadians) {
    const float normalized = NormalizeAngle(angleRadians);
    const int stepIndex = static_cast<int>(std::round(normalized / kEightDirectionStep));
    return NormalizeAngle(static_cast<float>(stepIndex) * kEightDirectionStep);
}

inline Vec2f DirectionFromHeading(float headingRadians) {
    return Vec2f{
        .x = std::sin(headingRadians),
        .y = -std::cos(headingRadians),
    };
}
}  // namespace core::angle
