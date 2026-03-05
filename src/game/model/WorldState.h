#pragma once

#include "core/Types.h"
#include "game/model/EntityTypes.h"

struct WorldState {
    MazeState maze{};
    PlayerTank player{
        .position = Vec2f{.x = 0.0F, .y = 0.0F},
        .velocity = Vec2f{.x = 0.0F, .y = 0.0F},
        .hullHeadingRadians = 0.0F,
        .turretHeadingRadians = 0.0F,
        .turnHoldDirection = 0,
        .turnHoldElapsedSeconds = 0.0F,
        .throttleNormalized = 0.0F,
        .fireCooldownSeconds = 0.0F,
        .fuel = GameplayConstants::kFuelMax,
        .lives = GameplayConstants::kStartingLives,
        .alive = true,
    };
    std::vector<EnemyTank> enemies{};
    std::vector<EnemyBase> enemyBases{};
    std::vector<Projectile> projectiles{};
    int score = 0;
    bool playerTurnLostPending = false;
    float startModeRemainingSeconds = 0.0F;
    float deathModeRemainingSeconds = 0.0F;
    float deathExplosionRemainingSeconds = 0.0F;
    Vec2f deathExplosionPosition{.x = 0.0F, .y = 0.0F};
    bool levelCleared = false;
    bool gameOver = false;
    float levelClearMessageSeconds = 0.0F;
};

struct GameState {
    MenuSettings menuSettings{
        .levelNumber = 4,
        .mazeDensity = 1,
        .invisibility = true,
    };
    WorldState world{};
};
