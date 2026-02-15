#pragma once

enum class DifficultyLevel {
    Easy = 0,
    Normal = 1,
    Hard = 2,
};

struct Vec2f {
    float x;
    float y;
};

struct MenuSettings {
    DifficultyLevel difficulty;
    int mazeDensityPercent;
};
