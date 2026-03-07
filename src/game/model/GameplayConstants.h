#pragma once

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
    static constexpr float kJoystickAcceleration = 0.7F;             // normalized-velocity units / second
    static constexpr float kPlayerProjectileSpeed = 20.0F;           // world-units / second
    static constexpr float kPlayerFireCooldownSeconds = 0.22F;       // seconds

    // Enemy tuning.
    static constexpr float kEnemyDroneSpeed = 1.0F;                  // world-units / second
    static constexpr float kEnemyTorpedoSpeed = 2.0F;                // world-units / second
    static constexpr float kEnemyHunterSpeed = 3.0F;                 // world-units / second
    static constexpr float kEnemyAssassinSpeed = 4.0F;               // world-units / second
    static constexpr float kEnemyDroneFireInterval = 3.0F;           // seconds
    static constexpr float kEnemyTorpedoFireInterval = 2.0F;         // seconds
    static constexpr float kEnemyHunterFireInterval = 1.5F;          // seconds
    static constexpr float kEnemyAssassinFireInterval = 1.0F;        // seconds
    static constexpr float kEnemyProjectileSpeed = 7.0F;             // world-units / second
    static constexpr float kEnemyAggroRangeUnits = 140.0F;           // world-units
    static constexpr float kEnemyFireRangeUnits = 180.0F;            // world-units
    static constexpr int kDroneMaxLevel = 2;                         // level index
    static constexpr int kTorpedoMaxLevel = 4;                       // level index
    static constexpr int kHunterMaxLevel = 7;                        // level index
    static constexpr float kEnemyAssassinPredictionSeconds = 0.8F;   // seconds
    static constexpr float kEnemyAiRetargetMinSeconds = 0.7F;        // seconds
    static constexpr float kEnemyAiRetargetRandomSeconds = 0.9F;     // seconds
    static constexpr int kMaxAliveEnemies = 999;                     // enemies
    static constexpr int kMaxAliveEnemiesPerBase = 24;               // enemies / base
    static constexpr float kEnemyInitialFireCooldownSeconds = 0.2F;  // seconds
    static constexpr float kBaseSpawnCooldownSeconds = 0.9F;         // seconds
    static constexpr float kEnemyPreferredSeparationUnits = 2.0F;    // center distance world-units (clearance 1.0 with r1=r2=0.5)
    static constexpr float kEnemyMutualKillDistanceUnits = 1.0F;    // world-units (r1+r2, with enemy r=0.5)
    static constexpr float kEnemyLookaheadObstacleUnits = 1.0F;      // world-units
    static constexpr float kEnemyRequiredClearRunUnits = 3.0F;       // world-units
    static constexpr float kSlowRotateFullTurnSeconds = 4.0F;        // seconds
    static constexpr float kTorpedoDetectRangeUnits = 9.0F;          // world-units
    static constexpr float kHunterDetectRangeUnits = 12.0F;          // world-units
    static constexpr float kHunterMinDistanceUnits = 3.0F;           // world-units
    static constexpr float kHunterMaxDistanceUnits = 6.0F;           // world-units
    static constexpr float kAssassinMinDistanceUnits = 3.0F;         // world-units

    // Shared explosion animation parameters (all explosion spritesheets use these).
    static constexpr int kExplosionFrameCount = 6;                             // frames
    static constexpr float kExplosionFrameDurationSeconds = 0.15F;             // seconds / frame
    static constexpr float kExplosionTotalDurationSeconds =
        static_cast<float>(kExplosionFrameCount) * kExplosionFrameDurationSeconds; // 0.9s

    // Enemy explosion animation (explosion-1.png, 32×32 frames).
    static constexpr int kMaxEnemyExplosions = 64;                             // slots
    static constexpr int kEnemyExplosionSourceFrameSizePx = 32;                // px (source cell)
    static constexpr int kEnemyExplosionRenderSizePx = 32;                     // px (destination, 1× scale)
    // Kept for back-compat; callers should prefer the shared constants above.
    static constexpr int kEnemyExplosionFrameCount = kExplosionFrameCount;
    static constexpr float kEnemyExplosionFrameDurationSeconds = kExplosionFrameDurationSeconds;
    static constexpr float kEnemyExplosionTotalDurationSeconds = kExplosionTotalDurationSeconds;

    // Player death explosion animation (explosion-2.png, 32×32 frames).
    static constexpr int kPlayerExplosionSourceFrameSizePx = 32;               // px (source cell)
    static constexpr float kPlayerExplosionRenderWorldUnits = 2.0F;            // world units (32px / 16px·unit⁻¹)

    // Base destruction explosion animation (explosion-3-large.png, 64×64 frames).
    static constexpr int kMaxBaseExplosions = kEnemyBaseCount;                 // slots (one per base)
    static constexpr int kBaseExplosionSourceFrameSizePx = 64;                 // px (source cell)
    static constexpr float kBaseExplosionRenderWorldUnits = 4.0F;              // world units (64px / 16px·unit⁻¹)

    // Game phase tuning.
    static constexpr float kStartModeDurationSeconds = 1.5F;         // seconds
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
    static constexpr float kProjectileLifetimeSeconds = 3.0F;          // seconds
    static constexpr float kProjectileHitRadius = 0.7F;                // world-units
    static constexpr float kPlayerEnemyCollisionRadius = kTankCollisionRadiusUnits * 2.0F; // world-units
    static constexpr float kLineOfSightSampleSpacing = 0.08F;          // world-units

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
