#pragma once

#include "app/SoundPool.h"
#include "game/model/GameplayEvents.h"

struct AudioEventRouterConfig {
    bool audioReady = false;
    bool powerUpLoaded = false;
    bool playerShotLoaded = false;
    bool enemyShotLoaded = false;
    bool enemySpawningLoaded = false;
    bool enemyExplodingLoaded = false;
    bool baseExplodingLoaded = false;
    SoundPool<4>* powerUpSound = nullptr;
    SoundPool<4>* playerShotSound = nullptr;
    SoundPool<4>* enemyShotSound = nullptr;
    SoundPool<4>* enemySpawningSound = nullptr;
    SoundPool<4>* enemyExplodingSound = nullptr;
    SoundPool<4>* baseExplodingSound = nullptr;
};

class AudioEventRouter {
public:
    void RouteStep(
        const Vec2f& listener,
        GameplayEventQueue& events,
        const AudioEventRouterConfig& config) const;
};
