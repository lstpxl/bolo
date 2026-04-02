#include "core/Time.h"

#include <algorithm>

FixedStepTimer::FixedStepTimer(float stepSeconds, int maxStepsPerFrame)
    : stepSeconds_(stepSeconds), maxStepsPerFrame_(maxStepsPerFrame) {}

void FixedStepTimer::Accumulate(float frameSeconds) {
    // Clamp overly long frames to avoid huge catch-up bursts.
    const float cap = stepSeconds_ * static_cast<float>(maxStepsPerFrame_);
    const float clampedFrame = std::min(frameSeconds, cap);
    accumulator_ += clampedFrame;
    accumulator_ = std::min(accumulator_, cap);
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
