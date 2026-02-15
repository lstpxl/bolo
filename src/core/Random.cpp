#include "core/Random.h"

Random::Random(std::uint32_t seed) : engine_(seed) {}

int Random::NextInt(int minInclusive, int maxInclusive) {
    std::uniform_int_distribution<int> distribution(minInclusive, maxInclusive);
    return distribution(engine_);
}

float Random::NextFloat(float minInclusive, float maxInclusive) {
    std::uniform_real_distribution<float> distribution(minInclusive, maxInclusive);
    return distribution(engine_);
}
