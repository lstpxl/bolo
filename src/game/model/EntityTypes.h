#pragma once

#include <array>
#include <vector>
#include "core/Types.h"
#include "game/model/GameplayConstants.h"
#include "game/navigation/MazeCellCoord.h"

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
    // Torpedo movement states (unified with aiMode; semantics match other type modes)
    Fly,
    Ram,
    Retreat,
    Targeting,
};

enum class EnemySimTier {
    Full,
    Cheap,
};

enum class CheapSegmentFailReason {
    None = 0,
    NoFlowBuild = 1,
    InvalidNextCell = 2,
    NonCardinalFlowStep = 3,
    MidpointTooClose = 4,
    EdgeExitFailed = 5,
    SegmentIntersectsWall = 6,
    EmergencyFallbackBlocked = 7,
    SegmentEndpointInvalid = 8,
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
    /// Full-tier DroneReset: Watch rotates toward this heading before returning to Wander.
    bool droneWatchAlignToHeading = false;
    float droneWatchAlignHeadingRadians = 0.0F;
    bool returnToBase = false;
    float torpedoStraightDistanceSinceTurnUnits = 3.0F;
    float torpedoMoveDecisionHoldRemainingUnits = 0.0F;
    float torpedoRetreatMovedUnits = 0.0F;
    float torpedoPlayerDetectTimerSeconds = 0.0F;
    bool torpedoPlayerDetected = false;
    float torpedoLastKnownPlayerHeadingRadians = 0.0F;
    float torpedoChosenHeadingRadians = 0.0F;
    float torpedoRotateTargetHeadingRadians = 0.0F;
    float torpedoCurrentSpeedUnitsPerSecond = 0.0F;
    EnemySimTier simTier = EnemySimTier::Full;
    float offscreenCachedHeadingRadians = 0.0F;
    Vec2f offscreenSegmentEnd{.x = 0.0F, .y = 0.0F};
    bool offscreenSegmentActive = false;
    bool hunterScoutPathActive = false;
    int hunterScoutTargetCellHash = -1;
    std::array<Vec2f, 2> hunterScoutSegmentPoints{};
    int hunterScoutSegmentCount = 0;
    int hunterScoutSegmentIndex = 0;
    float hunterScoutCachedHeadingRadians = 0.0F;
    bool torpedoFlyPathActive = false;
    /// Planner adjacent cell for the current segment (hash matches `CellCoordCache::CellHash`); rebuild when entered.
    int torpedoFlyTargetCellHash = -1;
    std::array<Vec2f, 2> torpedoFlySegmentPoints{};
    int torpedoFlySegmentCount = 0;
    int torpedoFlySegmentIndex = 0;
    float torpedoFlyCachedHeadingRadians = 0.0F;
    int originBaseIndex = -1;
    std::array<Vec2f, kMaxPathWaypoints> pathWaypoints{};
    int pathWaypointCount = 0;
    int pathWaypointIndex = 0;
    game::navigation::MazeCellCoord cellCoord{};
    int cachedPlayerCellHash = -1;
    int expectedPathCellHash = -1;
    int cachedFlowFromCellHash = -1;
    float cachedFlowHeadingRadians = 0.0F;
    int cheapSegmentBuildFailCount = 0;
    int cheapSegmentLastFailCellHash = -1;
    CheapSegmentFailReason cheapSegmentLastFailReason = CheapSegmentFailReason::None;
    int cheapSegmentBuildMethodStage = 0;
    bool cheapSegmentInsideWallAvoidLastFrame = false;
    bool cheapTierCrowdedSlowMode = false;  // assassins: 0.5x speed when another enemy in same cell
    bool seesPlayer = false;
    bool alive = true;
};

struct EnemyExplosion {
    Vec2f position;
    float elapsedSeconds = 0.0F;
    bool active = false;
};

enum class BaseOuterSegment {
    Top,
    Right,
    Bottom,
    Left,
};

struct EnemyBase {
    Vec2f position;
    bool destroyed = false;
    bool explosionPlayed = false;
    int topSegmentHealth = GameplayConstants::kBaseOuterSegmentMaxHealth;
    int rightSegmentHealth = GameplayConstants::kBaseOuterSegmentMaxHealth;
    int bottomSegmentHealth = GameplayConstants::kBaseOuterSegmentMaxHealth;
    int leftSegmentHealth = GameplayConstants::kBaseOuterSegmentMaxHealth;
    float repairCountdownSeconds = 0.0F;
    float enemyGenerationIntervalSeconds = GameplayConstants::kBaseSpawnCooldownSeconds;
    float enemyGenerationTimerSeconds = GameplayConstants::kBaseSpawnCooldownSeconds;
    int activeEnemies = 0;

    int SegmentHealth(BaseOuterSegment segment) const {
        switch (segment) {
        case BaseOuterSegment::Top:
            return topSegmentHealth;
        case BaseOuterSegment::Right:
            return rightSegmentHealth;
        case BaseOuterSegment::Bottom:
            return bottomSegmentHealth;
        case BaseOuterSegment::Left:
            return leftSegmentHealth;
        }
        return 0;
    }

    int& SegmentHealthRef(BaseOuterSegment segment) {
        switch (segment) {
        case BaseOuterSegment::Top:
            return topSegmentHealth;
        case BaseOuterSegment::Right:
            return rightSegmentHealth;
        case BaseOuterSegment::Bottom:
            return bottomSegmentHealth;
        case BaseOuterSegment::Left:
            return leftSegmentHealth;
        }
        return topSegmentHealth;
    }

    bool HasDamagedSegments() const {
        return topSegmentHealth < GameplayConstants::kBaseOuterSegmentMaxHealth ||
            rightSegmentHealth < GameplayConstants::kBaseOuterSegmentMaxHealth ||
            bottomSegmentHealth < GameplayConstants::kBaseOuterSegmentMaxHealth ||
            leftSegmentHealth < GameplayConstants::kBaseOuterSegmentMaxHealth;
    }

    bool IsSegmentDamaged(BaseOuterSegment segment) const {
        return SegmentHealth(segment) < GameplayConstants::kBaseOuterSegmentMaxHealth;
    }
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
