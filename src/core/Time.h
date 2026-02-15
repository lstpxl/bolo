#pragma once

class FixedStepTimer {
public:
    explicit FixedStepTimer(float stepSeconds);

    void Accumulate(float frameSeconds);
    bool ShouldStep() const;
    void ConsumeStep();
    float StepSeconds() const;

private:
    float stepSeconds_;
    float accumulator_ = 0.0F;
};
