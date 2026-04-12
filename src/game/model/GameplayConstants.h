#pragma once

struct GameplayConstants {
    // World geometry.
    static constexpr int kPixelsPerUnit = 16;                // pixels / world-unit
    static constexpr int kMazeWidthCells = 60;               // cells
    static constexpr int kMazeHeightCells = 60;              // cells
    static constexpr int kMazeCellSizeUnits = 6;             // world-units / cell
    
    static constexpr float kEntitySizeUnits = 0.875F;          // world-units (7px x 2 scale at 16px/unit)
    static constexpr float kEnemyBaseSizeUnits = 3.0F;       // world-units
    static constexpr int kEnemyBaseCount = 6;                // bases / maze
    static constexpr int kBaseOuterSegmentMaxHealth = 4;     // hit points / side
    static constexpr float kBaseRepairDelaySeconds = 4.0F;   // seconds
    static constexpr int kBaseRepairHealthPerTick = 1;       // hit points
    static constexpr float kEnemyBaseCoreRadiusUnits = 0.4375F;  // world-units (7 px @ 16 px/unit)

    static constexpr float kEntityRadiusUnits = kEntitySizeUnits * 0.5F; // world-units

    static constexpr float kWallHalfThicknessPixels = 1.0F;             // pixels 
    static constexpr float kWallHalfThicknessUnits =
        kWallHalfThicknessPixels / static_cast<float>(kPixelsPerUnit);  // world-units
    static constexpr float kWallThicknessUnits = 
        2.0F * kWallHalfThicknessUnits;                                 // world-units

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
    static constexpr int kPlayerInitialSpawnMinBaseDistanceCells = 8;  // cells
    static constexpr int kPlayerInitialSpawnMaxBaseDistanceCells = 20; // cells
    static constexpr int kPlayerRespawnMinBaseDistanceCells = 12;      // cells
    static constexpr int kPlayerRespawnMaxBaseDistanceCells = 24;      // cells
    static constexpr int kPlayerRespawnEnemyExclusionManhattanCells = 3; // cells

    // Enemy tuning.
    static constexpr float kEnemyDroneSpeed = 1.0F;                  // world-units / second
    /// Baseline torpedo speed; multiply by `EnemySubtypeSpeedMultiplier` (Basic = 0.75) where applied.
    static constexpr float kEnemyTorpedoSpeed = 6.0F;                // world-units / second
    /// Ram forward-speed ceiling: `2 × kEnemyTorpedoSpeed`; multiply by subtype where applied.
    static constexpr float kEnemyTorpedoSpeedMax = 2.0F * kEnemyTorpedoSpeed; // world-units / second
    static constexpr float kEnemyHunterSpeed = 3.0F;                 // world-units / second
    static constexpr float kEnemyAssassinSpeed = 4.0F;               // world-units / second
    static constexpr float kEnemyDroneFireInterval = 3.0F;           // seconds
    static constexpr float kEnemyTorpedoFireInterval = 2.0F;         // seconds
    static constexpr float kEnemyHunterFireInterval = 1.5F;          // seconds
    static constexpr float kEnemyAssassinFireInterval = 1.0F;        // seconds
    static constexpr float kEnemyVisualContactMusicHoldSeconds = 4.0F; // seconds
    static constexpr float kEnemyAlarmClearDelaySeconds = 5.0F;        // seconds without drone visual contact
    static constexpr int kEnemyAlarmInfluenceRangeCells = 24;          // maze cells from player
    static constexpr float kEnemyProjectileSpeed = 7.0F;             // world-units / second
    static constexpr float kEnemyAggroRangeUnits = 15.0F;           // world-units
    /// Half-angle (radians) from hull forward for `seesPlayer`: visible if bearing to player
    /// is within this arc (default ±90° → forward hemisphere).
    static constexpr float kEnemyForwardVisionHalfAngleRadiansDefault =
        1.57079632679489661923F;  // π/2, 90°
    /// Torpedo: tighter forward arc (±45°); outside this arc `seesPlayer` is false.
    static constexpr float kTorpedoForwardVisionHalfAngleRadians =
        0.78539816339744830962F;  // π/4, 45°
    // Enemy projectile max distance uses per-type detect ranges (see RunFiringPhase).
    static constexpr float kEnemyAssassinPredictionSeconds = 0.8F;   // seconds
    static constexpr float kEnemyAiRetargetMinSeconds = 0.7F;        // seconds
    static constexpr float kEnemyAiRetargetRandomSeconds = 0.9F;     // seconds
    static constexpr int kMaxAliveEnemies = 200;                     // enemies
    static constexpr int kMaxAliveEnemiesPerBase = 24;               // enemies / base
    static constexpr float kEnemyInitialFireCooldownSeconds = 0.2F;  // seconds
    static constexpr float kBaseSpawnCooldownSeconds = 0.9F;         // seconds
    static constexpr float kBaseSpawnCoreGrowDurationSeconds = 2.0F; // seconds
    static constexpr float kEnemyPreferredSeparationUnits = 2.0F;    // center distance world-units (clearance 1.0 with r1=r2=0.5)
    static constexpr float kEnemyMutualKillDistanceUnits = 1.0F;    // world-units (r1+r2, with enemy r=0.5)
    static constexpr float kEnemyLookaheadObstacleUnits = 1.0F;      // world-units
    static constexpr float kEnemyRequiredClearRunUnits = 3.0F;       // world-units
    static constexpr float kSlowRotateFullTurnSeconds = 4.0F;        // seconds
    static constexpr float kTorpedoDetectRangeUnits = 9.0F;          // world-units (legacy non-ram sensing)
    static constexpr float kTorpedoRamDetectRangeUnits = 12.0F;      // world-units
    /// Full-tier torpedo accel cap; multiply by `EnemySubtypeSpeedMultiplier` (Basic = 0.75) where applied.
    static constexpr float kTorpedoFullTierAccelMaxUnitsPerSecondSq = 2.0F; // world-units / second^2
    /// Full-tier torpedo max turn rate (Fly / Ram / Rotate / uncouple, etc.); radians / second.
    // static constexpr float kTorpedoFullTierTurnSpeedRadiansPerSecond = 0.7853982F; // 45 deg / second
    static constexpr float kTorpedoFullTierTurnSpeedRadiansPerSecond = 2.0F; // 45 deg / second
    /// Midpoint shift along bend-to-opposite-bend direction for `AdjacentCellSegmentPlanner` diagonal 2-segment paths.
    static constexpr float kAdjacentCellDiagonalMidpointShiftUnits = 1.75F; // world-units
    static constexpr float kDroneDetectRangeUnits = 18.0F;           // world-units
    static constexpr float kDronePursuitSpeedFactor = 0.5F;          // unitless
    static constexpr float kDronePlayerAvoidanceDistanceUnits = 4.0F; // world-units
    /// Minimum maze graph distance (cells) from nearest alive base for drone "return to base" / Watch distance gate.
    static constexpr int kDroneReturnToBaseMinBaseDistanceCells = 7;
    /// Drone self-awareness timer roll range (full and cheap tier).
    static constexpr float kDroneSelfAwarenessIntervalMinSeconds = 5.0F;
    static constexpr float kDroneSelfAwarenessIntervalMaxSeconds = 8.0F;
    /// If drone bearing differs from `BaseFlowField` step toward base by at least this (radians), self-awareness may `DroneReset`.
    static constexpr float kDroneSelfAwarenessOffFlowBearingThresholdRadians = 1.3962634F;  // 80°
    static constexpr float kHunterDetectRangeUnits = 15.0F;          // world-units
    static constexpr float kHunterMinDistanceUnits = 3.0F;           // world-units
    static constexpr float kHunterMaxDistanceUnits = 6.0F;           // world-units
    static constexpr float kAssassinMinDistanceUnits = 4.0F;         // world-units

    // Shared explosion animation parameters (all explosion spritesheets use these).
    static constexpr int kExplosionFrameCount = 6;                             // frames
    static constexpr float kExplosionFrameDurationSeconds = 0.15F;             // seconds / frame
    static constexpr float kExplosionTotalDurationSeconds =
        static_cast<float>(kExplosionFrameCount) * kExplosionFrameDurationSeconds; // 0.9s

    /// How long explosion blast damage applies per slot (VFX may run `kExplosionTotalDurationSeconds`).
    static constexpr float kExplosionBlastDamageDurationSeconds = 0.5F;  // seconds

    /// Gameplay blast kill radius from explosion center (enemy / wall / player death VFX).
    static constexpr float kExplosionBlastRadiusUnits = 0.5F;   // world-units
    /// Larger blast radius for destroyed base explosion VFX.
    static constexpr float kBaseExplosionBlastRadiusUnits = 1.5F;  // world-units

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
    // Refuel lock: fuel ramps 0 → max over this interval (new game, respawn, level restart).
    static constexpr float kStartModeDurationSeconds = 1.6F;
    static constexpr float kDeathModeDurationSeconds = 3.0F;         // seconds
    static constexpr float kDeathExplosionDurationSeconds = 3.0F;    // seconds
    static constexpr float kVictoryPhaseDurationSeconds = 3.0F;      // seconds
    static constexpr int kEvacZoneMinDistanceFromPlayerCells = 3;     // maze cells
    static constexpr int kEvacZoneMaxDistanceFromPlayerCells = 6;     // maze cells
    static constexpr float kEvacZoneSizeUnits = 3.0F;                 // world-units
    static constexpr float kEvacZoneFlickerCycleSeconds = 1.0F;       // seconds

    // Projectiles and collisions.
    // Clearance constants (expansion beyond wall half-thickness or base half-size).
    static constexpr float kWallClearanceUnits = 0.05F;                 // world-units
    static constexpr float kEnemyWallAvoidanceClearanceUnits = 0.5F;   // world-units
    static constexpr float kEnemyEnemyClearanceUnits = 0.5F;                 // world-units

    // Wall collisions (wall = finite line extruded as pill by half-thickness).
    static constexpr float kEnemyWallHardCollisionUnits =
        kWallHalfThicknessUnits + kEntityRadiusUnits;                   // world-units
    static constexpr float kEnemyWallSoftCollisionUnits =
        kWallHalfThicknessUnits + kEntityRadiusUnits + kWallClearanceUnits;  // world-units
    static constexpr float kEnemyWallAvoidanceUnits =
        kWallHalfThicknessUnits + kEntityRadiusUnits +
        kEnemyWallAvoidanceClearanceUnits;                              // world-units
    static constexpr float kPlayerWallHardCollisionUnits =
        kWallHalfThicknessUnits + kEntityRadiusUnits;                  // world-units
    static constexpr float kPlayerWallSoftCollisionUnits =
        kWallHalfThicknessUnits + kEntityRadiusUnits + kWallClearanceUnits;  // world-units

    // Base collisions (same formula as wall: halfSize + entityRadius + optional clearance).
    static constexpr float kEnemyBaseHardCollisionUnits =
        kEnemyBaseSizeUnits * 0.5F + kEntityRadiusUnits;
    static constexpr float kEnemyBaseSoftCollisionUnits =
        kEnemyBaseHardCollisionUnits + kWallClearanceUnits;
    static constexpr float kEnemyBaseAvoidanceUnits =
        kEnemyBaseHardCollisionUnits + kEnemyWallAvoidanceClearanceUnits;
    static constexpr float kPlayerBaseHardCollisionUnits = kEnemyBaseHardCollisionUnits;
    static constexpr float kPlayerBaseSoftCollisionUnits = kEnemyBaseSoftCollisionUnits;

    // Clearance to pass into geometry (expansion beyond wall half-thickness).
    static constexpr float kWallClearanceForHard = kEntityRadiusUnits;
    static constexpr float kWallClearanceForSoft = kEntityRadiusUnits + kWallClearanceUnits;
    static constexpr float kWallClearanceForAvoidance =
        kEntityRadiusUnits + kEnemyWallAvoidanceClearanceUnits;

    // Enemy dual-radius model (universal for all types):
    static constexpr float kEnemyCollisionRadiusUnits = kEntityRadiusUnits;   // hard radius: collision
    static constexpr float kEnemyAvoidanceRadiusUnits =
        kEnemyEnemyClearanceUnits + kEntityRadiusUnits + kEntityRadiusUnits;  // soft radius: clearance + 2×radius

    static constexpr float kProjectileLifetimeSeconds = 1.5F;          // seconds
    static constexpr float kProjectileHitRadius = 0.7F;                // world-units
    static constexpr float kPlayerEnemyCollisionRadius = kEntityRadiusUnits * 2.0F;  // world-units (sum of radii)
    static constexpr float kLineOfSightSampleSpacing = 0.08F;          // world-units

    // Fuel/rules.
    static constexpr float kFuelDrainPercentOfMaxPerSecond = 1.5F;  // percent of kFuelMax / second
    static constexpr float kFuelLowWarningThreshold = 20.0F;         // fuel units
    static constexpr float kFuelEmptySpeedFactor = 0.1F;             // max speed multiplier at zero fuel
    static constexpr float kLevelClearMessageSeconds = 2.0F;         // seconds
    static constexpr float kGameplayStartingPhaseMinSeconds = 1.0F;  // seconds
    static constexpr int kEnemyScorePerLevelMultiplier = 1;          // points * level
    static constexpr int kBaseScorePerLevelMultiplier = 100;         // points * level

    // Maze density control: internal wall segments per 100 cells.
    static constexpr float kDensity1WallsPer100Cells = 39.0F;        // wall-segments / 100 cells
    static constexpr float kDensity5WallsPer100Cells = 90.0F;        // wall-segments / 100 cells
};
