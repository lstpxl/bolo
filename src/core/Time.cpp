#include "core/Time.h"

#include <algorithm>

FixedStepTimer::FixedStepTimer(float stepSeconds) : stepSeconds_(stepSeconds) {}

void FixedStepTimer::Accumulate(float frameSeconds) {
    // Clamp overly long frames to avoid huge catch-up bursts.
    const float clampedFrame = std::min(frameSeconds, stepSeconds_ * 4.0F);
    accumulator_ += clampedFrame;
    accumulator_ = std::min(accumulator_, stepSeconds_ * 8.0F);
}

bool FixedStepTimer::ShouldStep() const {
    return accumulator_ >= stepSeconds_;
}

void FixedStepTimer::ConsumeStep() {
    accumulator_ -= stepSeconds_;
}

float FixedStepTimer::StepSeconds() const {
    return stepSeconds_;
}
