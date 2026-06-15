#pragma once

struct Vec2f {
    float x;
    float y;
};

/** Default level number at app start and when returning to menu. Single source of truth. */
constexpr int kDefaultGameLevelNumber = 8;

struct MenuSettings {
    int levelNumber;
    int mazeDensity;
    bool invisibility;
    bool debugInfo;
    bool mouseControl;
};
