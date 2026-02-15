#pragma once

#include <cstdint>
#include <random>

class Random {
public:
    explicit Random(std::uint32_t seed);

    int NextInt(int minInclusive, int maxInclusive);
    float NextFloat(float minInclusive, float maxInclusive);

private:
    std::mt19937 engine_;
};
