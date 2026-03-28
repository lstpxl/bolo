#pragma once

#include <array>
#include "core/Types.h"
#include "game/model/EntityTypes.h"
#include "game/model/GameplayEvents.h"
#include "game/model/GameplayConstants.h"
#include "game/navigation/BaseDistanceField.h"
#include "game/navigation/BaseFlowField.h"
#include "game/navigation/CellCoordCache.h"
#include "game/navigation/PlayerFlowField.h"
#include "game/spatial/EnemyCellOccupancy.h"
#include "game/spatial/SweepPruneBroadPhase.h"

struct NavigationRuntimeCache {
    game::navigation::CellCoordCache cellCoords{};
    game::navigation::BaseDistanceField baseDistanceField{};
    game::navigation::BaseFlowField baseFlowField{};
    game::navigation::PlayerFlowField playerFlowField{};
    game::spatial::EnemyCellOccupancy enemyCellOccupancy{};
    game::spatial::EnemyCellOccupancy enemyRayQueryOccupancy{};
    int flowFieldInvalidationGeneration = 0;
};

struct CollisionRuntimeCache {
    game::spatial::SweepPruneBroadPhase sweepPrune{};
};

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
    std::array<EnemyExplosion, GameplayConstants::kMaxEnemyExplosions> enemyExplosions{};
    std::array<EnemyExplosion, GameplayConstants::kMaxBaseExplosions> baseExplosions{};
    std::vector<EnemyBase> enemyBases{};
    std::vector<Projectile> projectiles{};
    int score = 0;
    bool playerTurnLostPending = false;
    float enemyVisualContactMusicTimerSeconds = 0.0F;
    float startModeRemainingSeconds = 0.0F;
    StartModeReason startModeReason = StartModeReason::Unknown;
    float deathModeRemainingSeconds = 0.0F;
    float deathExplosionRemainingSeconds = 0.0F;
    Vec2f deathExplosionPosition{.x = 0.0F, .y = 0.0F};
    bool levelCleared = false;
    bool gameOver = false;
    float levelClearMessageSeconds = 0.0F;
    NavigationRuntimeCache navigationCache{};
    CollisionRuntimeCache collisionCache{};
    bool panModeActive = false;
    Vec2f panTarget{.x = 0.0F, .y = 0.0F};
    GameplayEventQueue gameplayEvents{};
};

struct GameState {
    MenuSettings menuSettings{
        .levelNumber = kDefaultGameLevelNumber,
        .mazeDensity = 1,
        .invisibility = false,
        .debugInfo = false,
    };
    WorldState world{};
};
