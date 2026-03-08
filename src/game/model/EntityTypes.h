#pragma once

#include <array>
#include <vector>
#include "core/Types.h"
#include "game/model/GameplayConstants.h"

enum class GameMode {
    Menu,
    Playing,
};

struct MazeCell {
    bool northWall = true;
    bool eastWall = true;
    bool southWall = true;
    bool westWall = true;
};

struct MazeState {
    int widthCells = GameplayConstants::kMazeWidthCells;
    int heightCells = GameplayConstants::kMazeHeightCells;
    int cellSizeUnits = GameplayConstants::kMazeCellSizeUnits;
    std::vector<MazeCell> cells{};
};

struct PlayerTank {
    Vec2f position;
    Vec2f velocity;
    float hullHeadingRadians;
    float turretHeadingRadians;
    int turnHoldDirection = 0;
    float turnHoldElapsedSeconds = 0.0F;
    float throttleNormalized = 0.0F;
    float fireCooldownSeconds = 0.0F;
    float fuel = GameplayConstants::kFuelMax;
    int lives = GameplayConstants::kStartingLives;
    bool alive = true;
};

enum class EnemyType {
    Drone,
    Torpedo,
    Hunter,
    Assassin,
};

enum class EnemySubtype {
    Basic,
    Advanced,
    Lord,
};

enum class EnemyAiMode {
    Wander,
    Watch,
    Scout,
    Chase,
    Rotate,
    Path,
    Pursuit,
    Uncouple,
};

enum class TorpedoMoveMode {
    Move,
    Retreat,
    Targeting,
    Rotate,
};

enum class EnemySimTier {
    Full,
    Cheap,
};

struct EnemyTank {
    static constexpr int kMaxPathWaypoints = 96;

    Vec2f position;
    Vec2f velocity;
    float headingRadians;
    EnemyType type = EnemyType::Drone;
    EnemySubtype subtype = EnemySubtype::Advanced;
    EnemyAiMode aiMode = EnemyAiMode::Wander;
    float fireCooldownSeconds = 0.0F;
    float aiStateTimerSeconds = 0.0F;
    float aiModeElapsedSeconds = 0.0F;
    float selfAwarenessIntervalSeconds = 6.0F;
    float selfAwarenessTimerSeconds = 6.0F;
    EnemyAiMode preUncoupleAiMode = EnemyAiMode::Wander;
    float desiredHeadingRadians = 0.0F;
    Vec2f wanderDirection{.x = 0.0F, .y = -1.0F};
    int watchRotateDirection = 1;
    bool returnToBase = false;
    TorpedoMoveMode torpedoMoveMode = TorpedoMoveMode::Move;
    float torpedoStraightDistanceSinceTurnUnits = 3.0F;
    float torpedoMoveDecisionHoldRemainingUnits = 0.0F;
    float torpedoRetreatMovedUnits = 0.0F;
    float torpedoPlayerDetectTimerSeconds = 0.0F;
    bool torpedoPlayerDetected = false;
    float torpedoLastKnownPlayerHeadingRadians = 0.0F;
    float torpedoChosenHeadingRadians = 0.0F;
    float torpedoRotateTargetHeadingRadians = 0.0F;
    EnemySimTier simTier = EnemySimTier::Full;
    float offscreenCachedHeadingRadians = 0.0F;
    Vec2f offscreenSegmentEnd{.x = 0.0F, .y = 0.0F};
    bool offscreenSegmentActive = false;
    int originBaseIndex = -1;
    std::array<Vec2f, kMaxPathWaypoints> pathWaypoints{};
    int pathWaypointCount = 0;
    int pathWaypointIndex = 0;
    int cachedPlayerCellHash = -1;
    int expectedPathCellHash = -1;
    int cachedFlowFromCellHash = -1;
    float cachedFlowHeadingRadians = 0.0F;
    bool alive = true;
};

struct EnemyExplosion {
    Vec2f position;
    float elapsedSeconds = 0.0F;
    bool active = false;
};

struct EnemyBase {
    Vec2f position;
    bool destroyed = false;
    bool explosionPlayed = false;
    float enemyGenerationIntervalSeconds = GameplayConstants::kBaseSpawnCooldownSeconds;
    float enemyGenerationTimerSeconds = GameplayConstants::kBaseSpawnCooldownSeconds;
    int activeEnemies = 0;
};

enum class ProjectileOwner {
    Player,
    Enemy,
};

struct Projectile {
    Vec2f previousPosition;
    Vec2f position;
    Vec2f velocity;
    ProjectileOwner owner = ProjectileOwner::Enemy;
    float remainingLifeSeconds = 0.0F;
    bool alive = true;
};
