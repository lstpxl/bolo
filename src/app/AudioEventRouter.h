#pragma once

#include "game/model/GameplayEvents.h"
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
        const Vec2f& listener,
        GameplayEventQueue& events,
        const AudioEventRouterConfig& config) const;
};
