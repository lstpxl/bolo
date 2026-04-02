#pragma once

class FixedStepTimer {
public:
    FixedStepTimer(float stepSeconds, int maxStepsPerFrame);

    void Accumulate(float frameSeconds);
    bool ShouldStep() const;
    void ConsumeStep();
    float StepSeconds() const;

private:
    float stepSeconds_;
    int maxStepsPerFrame_;
    float accumulator_ = 0.0F;
};
