#pragma once

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
    static constexpr int kMaxAliveEnemies = 18;                      // enemies
    static constexpr float kEnemyInitialFireCooldownSeconds = 0.2F;  // seconds
    static constexpr float kBaseSpawnCooldownSeconds = 1.2F;         // seconds

    // Projectiles and collisions.
    static constexpr float kProjectileLifetimeSeconds = 3.0F;        // seconds
    static constexpr float kProjectileHitRadius = 0.7F;              // world-units
    static constexpr float kPlayerEnemyCollisionRadius = 1.0F;       // world-units
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

struct EnemyTank {
    Vec2f position;
    Vec2f velocity;
    float headingRadians;
    EnemyType type = EnemyType::Drone;
    float fireCooldownSeconds = 0.0F;
    float aiStateTimerSeconds = 0.0F;
    Vec2f wanderDirection{.x = 0.0F, .y = -1.0F};
    bool alive = true;
};

struct EnemyBase {
    Vec2f position;
    bool destroyed = false;
    float spawnCooldownSeconds = 0.0F;
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
    bool levelCleared = false;
    bool gameOver = false;
    float levelClearMessageSeconds = 0.0F;
};

struct GameState {
    MenuSettings menuSettings{
        .levelNumber = 1,
        .mazeDensity = 2,
    };
    WorldState world{};
};
