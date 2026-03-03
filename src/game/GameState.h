#pragma once

#include <array>
#include <vector>
#include "core/Types.h"

enum class GameMode {
    Menu,
    Playing,
};

struct GameplayConstants {
    // World geometry.
    static constexpr int kPixelsPerUnit = 16;                // pixels / world-unit
    static constexpr int kMazeWidthCells = 60;               // cells
    static constexpr int kMazeHeightCells = 60;              // cells
    static constexpr int kMazeCellSizeUnits = 6;             // world-units / cell
    static constexpr float kWallThicknessUnits = 0.125F;     // world-units (2px at 16px/unit)
    static constexpr float kEntitySizeUnits = 1.0F;          // world-units
    static constexpr float kEnemyBaseSizeUnits = 3.0F;       // world-units
    static constexpr int kEnemyBaseCount = 6;                // bases / maze

    // Player tuning.
    static constexpr int kStartingLives = 4;                         // lives
    static constexpr float kFuelMax = 100.0F;                        // fuel units
    static constexpr float kPlayerFullVelocity = 20.0F;              // world-units / second
    static constexpr float kPlayerSecondsToFullVelocity = 3.0F;      // seconds
    static constexpr float kPlayerTurnSpeedRadians = 2.5F;           // radians / second
    static constexpr float kPlayerTurretTurnSpeedRadians = 3.0F;     // radians / second
    static constexpr float kJoystickAcceleration = 0.7F;        // normalized-velocity units / second
    static constexpr float kPlayerProjectileSpeed = 20.0F;           // world-units / second
    static constexpr float kPlayerFireCooldownSeconds = 0.22F;       // seconds

    // Enemy tuning.
    static constexpr float kEnemyDroneSpeed = 2.0F;                  // world-units / second
    static constexpr float kEnemyTorpedoSpeed = 4.0F;                // world-units / second
    static constexpr float kEnemyHunterSpeed = 6.0F;                 // world-units / second
    static constexpr float kEnemyAssassinSpeed = 8.0F;              // world-units / second
    static constexpr float kEnemyDroneFireInterval = 3.0F;           // seconds
    static constexpr float kEnemyTorpedoFireInterval = 2.0F;         // seconds
    static constexpr float kEnemyHunterFireInterval = 1.5F;          // seconds
    static constexpr float kEnemyAssassinFireInterval = 1.0F;        // seconds
    static constexpr float kEnemyProjectileSpeed = 7.0F;        // world-units / second
    static constexpr float kEnemyAggroRangeUnits = 140.0F;           // world-units
    static constexpr float kEnemyFireRangeUnits = 180.0F;            // world-units
    static constexpr int kDroneMaxLevel = 2;                         // level index
    static constexpr int kTorpedoMaxLevel = 4;                       // level index
    static constexpr int kHunterMaxLevel = 7;                        // level index
    static constexpr float kEnemyAssassinPredictionSeconds = 0.8F;   // seconds
    static constexpr float kEnemyAiRetargetMinSeconds = 0.7F;        // seconds
    static constexpr float kEnemyAiRetargetRandomSeconds = 0.9F;     // seconds
    static constexpr int kMaxAliveEnemies = 72;                      // enemies
    static constexpr int kMaxAliveEnemiesPerBase = 12;               // enemies / base
    static constexpr float kEnemyInitialFireCooldownSeconds = 0.2F;  // seconds
    static constexpr float kBaseSpawnCooldownSeconds = 3.6F;         // seconds
    static constexpr float kEnemyPreferredSeparationUnits = 1.0F;    // world-units
    static constexpr float kEnemyMutualKillDistanceUnits = 0.12F;    // world-units
    static constexpr float kEnemyLookaheadObstacleUnits = 1.0F;      // world-units
    static constexpr float kEnemyRequiredClearRunUnits = 3.0F;       // world-units
    static constexpr float kSlowRotateFullTurnSeconds = 4.0F;        // seconds
    static constexpr float kTorpedoDetectRangeUnits = 9.0F;          // world-units
    static constexpr float kHunterDetectRangeUnits = 12.0F;          // world-units
    static constexpr float kHunterMinDistanceUnits = 3.0F;           // world-units
    static constexpr float kHunterMaxDistanceUnits = 6.0F;           // world-units
    static constexpr float kAssassinMinDistanceUnits = 3.0F;         // world-units

    // Game phase tuning.
    static constexpr float kStartModeDurationSeconds = 2.0F;         // seconds
    static constexpr float kDeathModeDurationSeconds = 3.0F;         // seconds
    static constexpr float kDeathExplosionDurationSeconds = 3.0F;    // seconds

    // Projectiles and collisions.
    static constexpr float kTankCollisionDiameterPixels = 9.0F;        // pixels
    static constexpr float kTankCollisionRadiusUnits =
        (kTankCollisionDiameterPixels * 0.5F) / static_cast<float>(kPixelsPerUnit); // world-units
    static constexpr float kEnemyWallClearancePixels = 2.0F;           // pixels
    static constexpr float kEnemyWallClearanceUnits =
        kEnemyWallClearancePixels / static_cast<float>(kPixelsPerUnit); // world-units
    static constexpr float kEnemyWallAvoidanceRadiusUnits =
        kTankCollisionRadiusUnits + kEnemyWallClearanceUnits;           // world-units
    static constexpr float kProjectileLifetimeSeconds = 3.0F;        // seconds
    static constexpr float kProjectileHitRadius = 0.7F;              // world-units
    static constexpr float kPlayerEnemyCollisionRadius = kTankCollisionRadiusUnits * 2.0F; // world-units
    static constexpr float kLineOfSightSampleSpacing = 0.08F;        // world-units

    // Fuel/rules.
    static constexpr float kFuelDrainMovementThresholdSq = 0.01F;    // (world-units / second)^2
    static constexpr float kFuelDrainBasePerSecond = 0.9F;           // fuel units / second
    static constexpr float kFuelDrainThrottlePerSecond = 1.1F;       // fuel units / second @ throttle=1
    static constexpr float kLevelClearMessageSeconds = 2.0F;         // seconds
    static constexpr int kEnemyScorePerLevelMultiplier = 1;          // points * level
    static constexpr int kBaseScorePerLevelMultiplier = 100;         // points * level

    // Maze density control: internal wall segments per 100 cells.
    static constexpr float kDensity1WallsPer100Cells = 39.0F;        // wall-segments / 100 cells
    static constexpr float kDensity5WallsPer100Cells = 90.0F;        // wall-segments / 100 cells
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
};

enum class TorpedoMoveMode {
    Move,
    Retreat,
    Targeting,
    Rotate,
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
    int originBaseIndex = -1;
    std::array<Vec2f, kMaxPathWaypoints> pathWaypoints{};
    int pathWaypointCount = 0;
    int pathWaypointIndex = 0;
    bool alive = true;
};

struct EnemyBase {
    Vec2f position;
    bool destroyed = false;
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
        .levelNumber = 3,
        .mazeDensity = 1,
        .invisibility = false,
    };
    WorldState world{};
};
