#include "core/Time.h"

FixedStepTimer::FixedStepTimer(float stepSeconds) : stepSeconds_(stepSeconds) {}

void FixedStepTimer::Accumulate(float frameSeconds) {
    accumulator_ += frameSeconds;
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
