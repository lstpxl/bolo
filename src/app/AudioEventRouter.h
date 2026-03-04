#pragma once

#include "game/GameState.h"
#include "raylib.h"

struct AudioEventRouterConfig {
    bool audioReady = false;
    bool powerUpLoaded = false;
    bool playerShotLoaded = false;
    bool enemyShotLoaded = false;
    bool enemySpawningLoaded = false;
    bool enemyExplodingLoaded = false;
    bool baseExplodingLoaded = false;
    Sound* powerUpSound = nullptr;
    Sound* playerShotSound = nullptr;
    Sound* enemyShotSound = nullptr;
    Sound* enemySpawningSound = nullptr;
    Sound* enemyExplodingSound = nullptr;
    Sound* baseExplodingSound = nullptr;
};

class AudioEventRouter {
public:
    void RouteStep(
        const GameState& beforeUpdate,
        const GameState& afterUpdate,
        float stepSeconds,
        const AudioEventRouterConfig& config) const;
};
