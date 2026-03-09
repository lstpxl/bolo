#include "game/systems/EnemySystem.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <future>
#include <limits>
#include <vector>
#include "core/AngleMath.h"
#include "core/Profiling.h"
#include "core/Random.h"
#include "game/geometry/WorldGeometry.h"
#include "game/navigation/CellCoordCache.h"
#include "game/navigation/PlayerFlowField.h"
#include "game/spatial/EnemyCellOccupancy.h"
#include "game/spatial/EnemySpatialGrid.h"
#include "game/spatial/SweepPruneBroadPhase.h"
#include "game/systems/ProjectileSystem.h"

namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kEightDirectionStep = kPi / 4.0F;
constexpr float kCosThirtyDegrees = 0.8660254F;
constexpr float kDroneBaseBearingThresholdRadians = 1.3962634F;  // 80 degrees
constexpr float kDroneReturnRequiredClearRunUnits = 6.0F;
constexpr float kEnemyPlanningClearanceScale = 1.5F;
constexpr float kTorpedoNearCollisionCheckDistanceUnits = 3.0F;
constexpr float kTorpedoMoveDecisionHoldDistanceUnits = 1.0F;
constexpr float kTorpedoRetreatExitClearanceUnits = 2.0F;
constexpr float kTorpedoRetreatSpeedFactor = 0.1F;
constexpr float kTorpedoImmediateObstacleDistanceUnits = 1.0F;
constexpr float kTorpedoLongPathProbeUnits = 24.0F;
constexpr float kTorpedoPlayerDetectIntervalSeconds = 0.25F;
constexpr float kSegmentBuildProbeMaxUnits = 15.0F;
constexpr float kSegmentBuildSafetyReduceUnits = 4.0F;
constexpr float kSegmentBuildMinLengthUnits = 2.0F;
constexpr float kOffscreenSegmentLengthUnits = 8.0F;
constexpr float kOffscreenTorpedoSegmentLengthUnits = 12.0F;
constexpr float kOffscreenTorpedoDetectIntervalSeconds = 0.6F;
constexpr float kAssassinCheapSegmentOutOfCellUnits = 1.0F;
constexpr float kEnemyUncoupleDurationSeconds = 1.0F;
constexpr float kUncoupleEnemyForceRangeUnits = GameplayConstants::kEnemyPreferredSeparationUnits * 2.0F;
constexpr float kUncoupleWallProbeRangeUnits = 1.5F;
constexpr float kUncoupleObstacleProbeRangeUnits = 2.0F;
constexpr float kUncoupleObstacleAheadDotThreshold = 0.35F;
constexpr float kUncouplePathFollowForceScale = 0.6F;
constexpr float kUncoupleObstacleForceScale = 0.9F;
constexpr float kUncoupleWallForceScale = 1.2F;
constexpr float kUncoupleRandomForceScale = 0.08F;
constexpr float kUncoupleReentryDistanceUnits = 1.5F;
constexpr float kParallelWallSideProbeUnits = 0.75F;
constexpr float kParallelWallContactThresholdUnits = 0.18F;
constexpr int kEnemyTypeTelemetryCount = 4;
constexpr bool kUseFlowFieldPathGuidance = true;
constexpr int kMaxFlowFieldAge = 2;
constexpr bool kUseAssassinFlowFieldOnlyNavigation = true;
constexpr bool kUseAssassinAStarBackupNavigation = false;
constexpr bool kUseSweepPruneBroadPhase = true;
constexpr std::uint64_t kUncoupleEventSampleCap = 16U;
constexpr float kUncouplePriorityEpsilon = 0.05F;
constexpr float kUncouplePriorityCrowdingRangeUnits = GameplayConstants::kEnemyPreferredSeparationUnits * 1.5F;
constexpr float kUncouplePriorityClearProbeUnits = 3.0F;

enum class UncoupleReason {
    FrontalCollision,
    SeparationProximity,
    SelfWallContact,
};

int EnemyTypeTelemetryIndex(EnemyType type) {
    switch (type) {
    case EnemyType::Drone:
        return 0;
    case EnemyType::Torpedo:
        return 1;
    case EnemyType::Hunter:
        return 2;
    case EnemyType::Assassin:
        return 3;
    }
    return 0;
}

const char* EnemyTypeTelemetryLabel(int idx) {
    switch (idx) {
    case 0:
        return "D";
    case 1:
        return "T";
    case 2:
        return "H";
    case 3:
        return "A";
    default:
        return "?";
    }
}

const char* UncoupleReasonLabel(UncoupleReason reason) {
    switch (reason) {
    case UncoupleReason::FrontalCollision:
        return "frontal";
    case UncoupleReason::SeparationProximity:
        return "separation";
    case UncoupleReason::SelfWallContact:
        return "wallContact";
    }
    return "?";
}

EnemyRuntimeStats gEnemyRuntimeStats{};
std::uint64_t gLastEnemyStatsPrintedFrame = 0;
struct EnemyRuntimeWindowStats {
    int minAliveCount = std::numeric_limits<int>::max();
    int maxAliveCount = 0;
    int minVisibleCount = std::numeric_limits<int>::max();
    int maxVisibleCount = 0;
    int minFullTierCount = std::numeric_limits<int>::max();
    int maxFullTierCount = 0;
    std::uint64_t fixedSteps = 0;
    float windowSeconds = 0.0F;
    std::uint64_t collisionPassRuns = 0;
    std::uint64_t collisionPassSkips = 0;
    std::uint64_t frontalGridCandidates = 0;
    std::uint64_t frontalGridCellTransitions = 0;
    std::uint64_t frontalGridInsertEstimate = 0;
    std::uint64_t separationGridCandidates = 0;
    std::uint64_t frontalPairsVisited = 0;
    std::uint64_t frontalPairsDistanceChecks = 0;
    std::uint64_t separationPairsVisited = 0;
    std::uint64_t separationPairsResolved = 0;
    std::uint64_t frontalPairsBaseSkipped = 0;
    std::uint64_t separationPairsBaseSkipped = 0;
    std::uint64_t frontalPairsMutualKills = 0;
    std::uint64_t separationPairsMutualKills = 0;
    std::uint64_t separationPairsWallBlockedPushes = 0;
    std::array<std::uint64_t, kEnemyTypeTelemetryCount * kEnemyTypeTelemetryCount> frontalPairsByType{};
    std::array<std::uint64_t, kEnemyTypeTelemetryCount * kEnemyTypeTelemetryCount> separationPairsByType{};
    std::array<std::uint64_t, kEnemyTypeTelemetryCount> segmentsBuiltByType{};
    std::array<float, kEnemyTypeTelemetryCount> segmentLengthSumByType{};
    std::array<std::uint64_t, kEnemyTypeTelemetryCount> segmentBuildFailsByType{};
    std::uint64_t torpedoHeadingEvalCalls = 0;
    std::uint64_t torpedoHeadingRetreatStarts = 0;
    std::uint64_t torpedoHeadingChosenStraight = 0;
    std::uint64_t torpedoHeadingChosenLeft = 0;
    std::uint64_t torpedoHeadingChosenRight = 0;
    double torpedoHeadingBestClearSum = 0.0;
    double torpedoHeadingChosenClearSum = 0.0;
    std::uint64_t navPlayerCellChanges = 0;
    std::uint64_t navFlowRebuilds = 0;
    std::uint64_t navFlowHeadingSelections = 0;
    std::uint64_t navFlowMisses = 0;
    std::uint64_t navPathBuildCalls = 0;
    std::uint64_t navPathBuildSuccesses = 0;
    std::uint64_t sapUpdateCalls = 0;
    std::uint64_t sapActiveItems = 0;
    std::uint64_t sapCandidatePairs = 0;
    std::uint64_t sapXRepairs = 0;
    std::uint64_t sapYRepairs = 0;
    std::uint64_t killDebugEnemyEnemyEvents = 0;
    std::uint64_t killDebugEnemyEnemyFrontalEvents = 0;
    std::uint64_t killDebugEnemyEnemySeparationEvents = 0;
    std::uint64_t killDebugEnemyEnemyReenterEither = 0;
    std::uint64_t killDebugEnemyEnemyReenterBoth = 0;
    std::uint64_t killDebugEnemyEnemyWallContact = 0;
    std::uint64_t killDebugEnemyEnemySamplesPrinted = 0;
    float killDebugEnemyEnemyMinDistance = std::numeric_limits<float>::max();
    float killDebugEnemyEnemyMaxDistance = 0.0F;
    double killDebugEnemyEnemyDistanceSum = 0.0;
    std::uint64_t uncoupleEntries = 0;
    std::uint64_t uncoupleReentryResets = 0;
    std::uint64_t uncoupleEntriesFrontal = 0;
    std::uint64_t uncoupleEntriesSeparation = 0;
    std::uint64_t uncoupleEntriesWallContact = 0;
    std::uint64_t uncoupleSamplesPrinted = 0;
};
EnemyRuntimeWindowStats gEnemyRuntimeWindowStats{};

[[maybe_unused]] void RecordEnemyEnemyMutualKillDebug(
    const WorldState& world,
    int i,
    int j,
    const EnemyTank& a,
    const EnemyTank& b,
    float centerDistance,
    bool reenteredA,
    bool reenteredB,
    bool frontalPass) {
    EnemyRuntimeWindowStats& stats = gEnemyRuntimeWindowStats;
    stats.killDebugEnemyEnemyEvents += 1;
    if (frontalPass) {
        stats.killDebugEnemyEnemyFrontalEvents += 1;
    } else {
        stats.killDebugEnemyEnemySeparationEvents += 1;
    }
    if (reenteredA || reenteredB) {
        stats.killDebugEnemyEnemyReenterEither += 1;
    }
    if (reenteredA && reenteredB) {
        stats.killDebugEnemyEnemyReenterBoth += 1;
    }
    const bool wallA = game::geometry::IsPointInWall(world, a.position, GameplayConstants::kTankCollisionRadiusUnits);
    const bool wallB = game::geometry::IsPointInWall(world, b.position, GameplayConstants::kTankCollisionRadiusUnits);
    if (wallA || wallB) {
        stats.killDebugEnemyEnemyWallContact += 1;
    }
    stats.killDebugEnemyEnemyMinDistance = std::min(stats.killDebugEnemyEnemyMinDistance, centerDistance);
    stats.killDebugEnemyEnemyMaxDistance = std::max(stats.killDebugEnemyEnemyMaxDistance, centerDistance);
    stats.killDebugEnemyEnemyDistanceSum += centerDistance;

    if (stats.killDebugEnemyEnemySamplesPrinted < 12U) {
        std::printf(
            "[ENEMY_KILL_DEBUG_EVENT] reason=enemy_enemy pass=%s pair=%d,%d dist=%.3f killDist=%.3f reenter=%d,%d wallContact=%d,%d tier=%d,%d posA=(%.2f,%.2f) posB=(%.2f,%.2f)\n",
            frontalPass ? "frontal" : "separation",
            i,
            j,
            centerDistance,
            GameplayConstants::kEnemyMutualKillDistanceUnits,
            reenteredA ? 1 : 0,
            reenteredB ? 1 : 0,
            wallA ? 1 : 0,
            wallB ? 1 : 0,
            static_cast<int>(a.simTier),
            static_cast<int>(b.simTier),
            a.position.x,
            a.position.y,
            b.position.x,
            b.position.y);
        stats.killDebugEnemyEnemySamplesPrinted += 1;
    }
}

void AccumulateEnemyWindowStats(int aliveCount, int visibleCount, int fullTierCount) {
    EnemyRuntimeWindowStats& stats = gEnemyRuntimeWindowStats;
    stats.fixedSteps += 1;
    stats.minAliveCount = std::min(stats.minAliveCount, aliveCount);
    stats.maxAliveCount = std::max(stats.maxAliveCount, aliveCount);
    stats.minVisibleCount = std::min(stats.minVisibleCount, visibleCount);
    stats.maxVisibleCount = std::max(stats.maxVisibleCount, visibleCount);
    stats.minFullTierCount = std::min(stats.minFullTierCount, fullTierCount);
    stats.maxFullTierCount = std::max(stats.maxFullTierCount, fullTierCount);
}

void AccumulateEnemyWindowTime(float deltaSeconds) {
    gEnemyRuntimeWindowStats.windowSeconds += std::max(0.0F, deltaSeconds);
}

void AccumulateEnemyWindowPairStats(const EnemyRuntimeStats& frameStats) {
    gEnemyRuntimeWindowStats.frontalPairsVisited += static_cast<std::uint64_t>(std::max(0, frameStats.frontalPairsVisited));
    gEnemyRuntimeWindowStats.frontalPairsDistanceChecks +=
        static_cast<std::uint64_t>(std::max(0, frameStats.frontalPairsDistanceChecks));
    gEnemyRuntimeWindowStats.separationPairsVisited +=
        static_cast<std::uint64_t>(std::max(0, frameStats.separationPairsVisited));
    gEnemyRuntimeWindowStats.separationPairsResolved +=
        static_cast<std::uint64_t>(std::max(0, frameStats.separationPairsResolved));
}

void ResetEnemyWindowStats() {
    gEnemyRuntimeWindowStats = EnemyRuntimeWindowStats{};
}

float NormalizeAngle(float angleRadians) {
    return core::angle::NormalizeAngle(angleRadians);
}

float AngleDistance(float aRadians, float bRadians) {
    return core::angle::AngleDistance(aRadians, bRadians);
}

float SignedAngleDelta(float fromRadians, float toRadians) {
    return core::angle::SignedAngleDelta(fromRadians, toRadians);
}

float QuantizeToEightDirections(float angleRadians) {
    return core::angle::QuantizeToEightDirections(angleRadians);
}

Vec2f DirectionFromHeading(float headingRadians) {
    return core::angle::DirectionFromHeading(headingRadians);
}

float DistanceSq(const Vec2f& a, const Vec2f& b);
Vec2f NormalizeOrZero(const Vec2f& v);

EnemyAiMode DefaultAiModeForType(EnemyType type) {
    switch (type) {
    case EnemyType::Drone:
        return EnemyAiMode::Wander;
    case EnemyType::Torpedo:
        return EnemyAiMode::Wander;
    case EnemyType::Hunter:
        return EnemyAiMode::Scout;
    case EnemyType::Assassin:
        return EnemyAiMode::Pursuit;
    }
    return EnemyAiMode::Wander;
}

void RestoreFromUncoupleMode(EnemyTank& enemy) {
    EnemyAiMode mode = enemy.preUncoupleAiMode;
    if (mode == EnemyAiMode::Uncouple) {
        mode = DefaultAiModeForType(enemy.type);
    }
    enemy.aiMode = mode;
    enemy.aiModeElapsedSeconds = 0.0F;
    enemy.aiStateTimerSeconds = 0.0F;
}

float SelectUncoupleHeading(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    float fallbackHeading,
    Random& random) {
    const EnemyTank& self = enemies[static_cast<std::size_t>(selfIndex)];
    Vec2f separationForce{.x = 0.0F, .y = 0.0F};
    Vec2f obstacleAvoidanceForce{.x = 0.0F, .y = 0.0F};
    Vec2f wallAvoidanceForce{.x = 0.0F, .y = 0.0F};
    Vec2f randomNoiseForce{.x = 0.0F, .y = 0.0F};

    const float enemyRangeSq = kUncoupleEnemyForceRangeUnits * kUncoupleEnemyForceRangeUnits;
    const Vec2f desiredDir = DirectionFromHeading(QuantizeToEightDirections(fallbackHeading));
    for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
        if (i == selfIndex) {
            continue;
        }
        const EnemyTank& other = enemies[static_cast<std::size_t>(i)];
        if (!other.alive) {
            continue;
        }
        const float distSq = DistanceSq(self.position, other.position);
        if (distSq > enemyRangeSq) {
            continue;
        }

        const float dist = std::sqrt(std::max(0.0F, distSq));
        Vec2f awayDir = NormalizeOrZero(Vec2f{
            .x = self.position.x - other.position.x,
            .y = self.position.y - other.position.y,
        });
        if (awayDir.x == 0.0F && awayDir.y == 0.0F) {
            awayDir = DirectionFromHeading(static_cast<float>(i) * kEightDirectionStep);
        }
        const float separationWeight = 1.0F - std::min(1.0F, dist / kUncoupleEnemyForceRangeUnits);
        separationForce.x += awayDir.x * separationWeight;
        separationForce.y += awayDir.y * separationWeight;

        if (dist <= kUncoupleObstacleProbeRangeUnits) {
            const Vec2f toOtherDir = NormalizeOrZero(Vec2f{
                .x = other.position.x - self.position.x,
                .y = other.position.y - self.position.y,
            });
            const float aheadDot = toOtherDir.x * desiredDir.x + toOtherDir.y * desiredDir.y;
            if (aheadDot >= kUncoupleObstacleAheadDotThreshold) {
                const float obstacleWeight =
                    (1.0F - std::min(1.0F, dist / kUncoupleObstacleProbeRangeUnits)) *
                    kUncoupleObstacleForceScale;
                obstacleAvoidanceForce.x += awayDir.x * obstacleWeight;
                obstacleAvoidanceForce.y += awayDir.y * obstacleWeight;
            }
        }
    }

    // Add wall repulsion to avoid uncouple movement that sticks enemies against walls.
    for (int step = 0; step < 8; ++step) {
        const float heading = NormalizeAngle(static_cast<float>(step) * kEightDirectionStep);
        const Vec2f dir = DirectionFromHeading(heading);
        const float clear = game::geometry::FreeDistanceAhead(
            world,
            self.position,
            heading,
            kUncoupleWallProbeRangeUnits,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
            1.0F);
        if (clear >= kUncoupleWallProbeRangeUnits) {
            continue;
        }
        const float wallForce =
            (1.0F - std::max(0.0F, clear) / kUncoupleWallProbeRangeUnits) * kUncoupleWallForceScale;
        wallAvoidanceForce.x -= dir.x * wallForce;
        wallAvoidanceForce.y -= dir.y * wallForce;
    }

    // Tiny random jitter breaks local force equilibrium in tight clusters/passages.
    const float jitterHeading = random.NextFloat(0.0F, kPi * 2.0F);
    const Vec2f jitterDir = DirectionFromHeading(jitterHeading);
    randomNoiseForce.x += jitterDir.x * kUncoupleRandomForceScale;
    randomNoiseForce.y += jitterDir.y * kUncoupleRandomForceScale;

    // Keep path-following weaker than separation while uncoupling.
    const float separationMag =
        std::sqrt(separationForce.x * separationForce.x + separationForce.y * separationForce.y);
    const float pathFollowMag = std::min(kUncouplePathFollowForceScale, separationMag);
    const Vec2f pathFollowingForce{
        .x = desiredDir.x * pathFollowMag,
        .y = desiredDir.y * pathFollowMag,
    };

    const Vec2f summedForce{
        .x = pathFollowingForce.x + separationForce.x + obstacleAvoidanceForce.x + wallAvoidanceForce.x +
            randomNoiseForce.x,
        .y = pathFollowingForce.y + separationForce.y + obstacleAvoidanceForce.y + wallAvoidanceForce.y +
            randomNoiseForce.y,
    };

    const Vec2f awayDir = NormalizeOrZero(summedForce);
    if (awayDir.x == 0.0F && awayDir.y == 0.0F) {
        return QuantizeToEightDirections(fallbackHeading);
    }
    return QuantizeToEightDirections(std::atan2(awayDir.x, -awayDir.y));
}

void EnterUncoupleMode(
    std::vector<EnemyTank>& enemies,
    int leadingIndex,
    int uncoupleIndex,
    UncoupleReason reason,
    float movedLastFrameUnits = -1.0F) {
    if (uncoupleIndex < 0 || uncoupleIndex >= static_cast<int>(enemies.size())) {
        return;
    }
    EnemyTank& uncoupled = enemies[static_cast<std::size_t>(uncoupleIndex)];
    if (!uncoupled.alive) {
        return;
    }
    EnemyRuntimeWindowStats& stats = gEnemyRuntimeWindowStats;
    stats.uncoupleEntries += 1;
    switch (reason) {
    case UncoupleReason::FrontalCollision:
        stats.uncoupleEntriesFrontal += 1;
        break;
    case UncoupleReason::SeparationProximity:
        stats.uncoupleEntriesSeparation += 1;
        break;
    case UncoupleReason::SelfWallContact:
        stats.uncoupleEntriesWallContact += 1;
        break;
    }

    const bool alreadyUncouple = uncoupled.aiMode == EnemyAiMode::Uncouple;
    const float timerBeforeReset = uncoupled.aiStateTimerSeconds;
    if (alreadyUncouple) {
        stats.uncoupleReentryResets += 1;
    }
    if (uncoupled.aiMode != EnemyAiMode::Uncouple) {
        uncoupled.preUncoupleAiMode = uncoupled.aiMode;
        uncoupled.aiMode = EnemyAiMode::Uncouple;
        uncoupled.aiModeElapsedSeconds = 0.0F;
        uncoupled.aiStateTimerSeconds = kEnemyUncoupleDurationSeconds;
        uncoupled.offscreenSegmentActive = false;
    } else {
        // Keep uncouple timer stable on reentry so movement progress can accumulate.
        uncoupled.aiMode = EnemyAiMode::Uncouple;
    }

    float uncoupleHeading = QuantizeToEightDirections(uncoupled.headingRadians);
    if (leadingIndex >= 0 && leadingIndex < static_cast<int>(enemies.size())) {
        const EnemyTank& leader = enemies[static_cast<std::size_t>(leadingIndex)];
        if (leader.alive) {
            const Vec2f awayDir = NormalizeOrZero(Vec2f{
                .x = uncoupled.position.x - leader.position.x,
                .y = uncoupled.position.y - leader.position.y,
            });
            if (awayDir.x != 0.0F || awayDir.y != 0.0F) {
                uncoupleHeading = QuantizeToEightDirections(std::atan2(awayDir.x, -awayDir.y));
            }
        }
    }
    uncoupled.desiredHeadingRadians = uncoupleHeading;

    const bool shouldSample = alreadyUncouple || reason == UncoupleReason::SelfWallContact;
    if (shouldSample && stats.uncoupleSamplesPrinted < kUncoupleEventSampleCap) {
        std::printf(
            "[ENEMY_UNCOUPLE_EVENT] id=%d reason=%s alreadyUncouple=%d timerBeforeReset=%.3f movedLastFrame=%.3f pos=(%.2f,%.2f)\n",
            uncoupleIndex,
            UncoupleReasonLabel(reason),
            alreadyUncouple ? 1 : 0,
            timerBeforeReset,
            movedLastFrameUnits,
            uncoupled.position.x,
            uncoupled.position.y);
        stats.uncoupleSamplesPrinted += 1;
    }
}

bool ShouldEnterSeparationUncouple(const EnemyTank& a, const EnemyTank& b, float distSq) {
    if (a.aiMode != EnemyAiMode::Uncouple || b.aiMode != EnemyAiMode::Uncouple) {
        return true;
    }
    const float reentryDistSq = kUncoupleReentryDistanceUnits * kUncoupleReentryDistanceUnits;
    return distSq <= reentryDistSq;
}

float EnemySubtypeSpeedMultiplier(EnemyType type, EnemySubtype subtype) {
    if (subtype == EnemySubtype::Basic) {
        return 0.75F;
    }
    if (subtype == EnemySubtype::Lord && type == EnemyType::Hunter) {
        return 1.25F;
    }
    return 1.0F;
}

float EnemySpeed(EnemyType type, EnemySubtype subtype, bool assassinHasLineOfSight, int levelNumber) {
    float baseSpeed = GameplayConstants::kEnemyDroneSpeed;
    if (type == EnemyType::Drone) {
        baseSpeed = GameplayConstants::kEnemyDroneSpeed;
    } else if (type == EnemyType::Torpedo) {
        baseSpeed = GameplayConstants::kEnemyTorpedoSpeed;
    } else if (type == EnemyType::Hunter) {
        baseSpeed = GameplayConstants::kEnemyHunterSpeed;
    } else {
        baseSpeed = assassinHasLineOfSight ? 3.0F : 1.5F;
    }
    float speed = baseSpeed * EnemySubtypeSpeedMultiplier(type, subtype);
    // Level 9: assassin-only debug level with 4× assassin speed for flow-field testing.
    if (levelNumber == 9 && type == EnemyType::Assassin) {
        speed *= 4.0F;
    }
    return speed;
}

float EnemyFireInterval(EnemyType type) {
    if (type == EnemyType::Drone) {
        return GameplayConstants::kEnemyDroneFireInterval;
    }
    if (type == EnemyType::Torpedo) {
        return GameplayConstants::kEnemyTorpedoFireInterval;
    }
    if (type == EnemyType::Hunter) {
        return GameplayConstants::kEnemyHunterFireInterval;
    }
    return GameplayConstants::kEnemyAssassinFireInterval;
}

float DistanceSq(const Vec2f& a, const Vec2f& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

float Distance(const Vec2f& a, const Vec2f& b) {
    return std::sqrt(DistanceSq(a, b));
}

float DistancePointToSegment(const Vec2f& p, const Vec2f& a, const Vec2f& b) {
    const float abX = b.x - a.x;
    const float abY = b.y - a.y;
    const float abLenSq = abX * abX + abY * abY;
    if (abLenSq <= 0.000001F) {
        return Distance(p, a);
    }
    const float apX = p.x - a.x;
    const float apY = p.y - a.y;
    const float t = std::max(0.0F, std::min(1.0F, (apX * abX + apY * abY) / abLenSq));
    const Vec2f closest{
        .x = a.x + abX * t,
        .y = a.y + abY * t,
    };
    return Distance(p, closest);
}

std::size_t PairTypeMatrixIndex(EnemyType a, EnemyType b) {
    int ai = EnemyTypeTelemetryIndex(a);
    int bi = EnemyTypeTelemetryIndex(b);
    if (ai > bi) {
        std::swap(ai, bi);
    }
    return static_cast<std::size_t>(ai * kEnemyTypeTelemetryCount + bi);
}

int RandomRotateDirection(Random& random) {
    return random.NextInt(0, 1) == 0 ? -1 : 1;
}

float NearestBaseDistance(const WorldState& world, const Vec2f& p) {
    float nearest = std::numeric_limits<float>::infinity();
    for (const EnemyBase& base : world.enemyBases) {
        if (base.destroyed) {
            continue;
        }
        nearest = std::min(nearest, Distance(base.position, p));
    }
    return nearest;
}

Vec2f NearestBasePosition(const WorldState& world, const Vec2f& p) {
    Vec2f nearestPos = p;
    float nearest = std::numeric_limits<float>::infinity();
    for (const EnemyBase& base : world.enemyBases) {
        if (base.destroyed) {
            continue;
        }
        const float dist = Distance(base.position, p);
        if (dist < nearest) {
            nearest = dist;
            nearestPos = base.position;
        }
    }
    return nearestPos;
}

void EnterDroneWatchMode(WorldState& world, EnemyTank& enemy, Random& random) {
    enemy.aiMode = EnemyAiMode::Watch;
    enemy.aiModeElapsedSeconds = 0.0F;
    enemy.watchRotateDirection = RandomRotateDirection(random);
    const float nearestBaseDist = NearestBaseDistance(world, enemy.position);
    enemy.returnToBase = nearestBaseDist >= 36.0F;
}

Vec2f NormalizeOrZero(const Vec2f& v) {
    const float lenSq = v.x * v.x + v.y * v.y;
    if (lenSq <= 0.000001F) {
        return Vec2f{.x = 0.0F, .y = 0.0F};
    }
    const float invLen = 1.0F / std::sqrt(lenSq);
    return Vec2f{.x = v.x * invLen, .y = v.y * invLen};
}

bool IsPointInUndestroyedBase(const WorldState& world, const Vec2f& point, float clearanceUnits) {
    return game::geometry::IsPointInUndestroyedBase(world, point, clearanceUnits);
}

bool IsInPlayerViewport(const Vec2f& point, const GameState& state, const GameplayView& view) {
    const float halfWidth = view.viewportWidthUnits * 0.5F;
    const float halfHeight = view.viewportHeightUnits * 0.5F;
    const Vec2f center = state.world.player.position;
    return point.x >= center.x - halfWidth && point.x <= center.x + halfWidth &&
        point.y >= center.y - halfHeight && point.y <= center.y + halfHeight;
}

EnemySimTier DetermineEnemySimTier(const EnemyTank& enemy, const GameState& state, const GameplayView& view) {
    const float cellWidthUnits = static_cast<float>(state.world.maze.cellSizeUnits);
    const float fullTierRadiusUnits =
        (view.viewportWidthUnits * 0.5F + cellWidthUnits) * 1.5F;
    const float fullTierRadiusSq = fullTierRadiusUnits * fullTierRadiusUnits;
    const bool nearPlayer = DistanceSq(enemy.position, state.world.player.position) <= fullTierRadiusSq;
    const bool forceFullForTorpedoState =
        enemy.type == EnemyType::Torpedo &&
        enemy.torpedoMoveMode != TorpedoMoveMode::Move;
    return (nearPlayer || forceFullForTorpedoState)
        ? EnemySimTier::Full
        : EnemySimTier::Cheap;
}

bool SegmentIntersectsWall(const WorldState& world, const Vec2f& from, const Vec2f& to, float clearanceUnits) {
    return game::geometry::SegmentIntersectsWall(world, from, to, clearanceUnits);
}

bool IsEdgeOnWallContact(
    const WorldState& world,
    const Vec2f& candidatePosition,
    float movementHeadingRadians) {
    const float leftHeading = NormalizeAngle(movementHeadingRadians - (kPi * 0.5F));
    const float rightHeading = NormalizeAngle(movementHeadingRadians + (kPi * 0.5F));
    const float leftClear = game::geometry::FreeDistanceAhead(
        world,
        candidatePosition,
        leftHeading,
        kParallelWallSideProbeUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
        1.0F);
    const float rightClear = game::geometry::FreeDistanceAhead(
        world,
        candidatePosition,
        rightHeading,
        kParallelWallSideProbeUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
        1.0F);
    return leftClear <= kParallelWallContactThresholdUnits ||
        rightClear <= kParallelWallContactThresholdUnits;
}

float FreeDistanceAheadWallsOnly(
    const WorldState& world,
    const Vec2f& from,
    float headingRadians,
    float maxDistance,
    float clearanceUnits) {
    return game::geometry::FreeDistanceAheadGridWallsOnly(
        world, from, headingRadians, maxDistance, clearanceUnits, 1.0F);
}

[[maybe_unused]] void DecrementOriginBaseAliveCount(WorldState& world, EnemyTank& enemy) {
    if (enemy.originBaseIndex < 0 || enemy.originBaseIndex >= static_cast<int>(world.enemyBases.size())) {
        enemy.originBaseIndex = -1;
        return;
    }
    EnemyBase& origin = world.enemyBases[static_cast<std::size_t>(enemy.originBaseIndex)];
    origin.activeEnemies = std::max(0, origin.activeEnemies - 1);
    enemy.originBaseIndex = -1;
}

float ChooseBestTurnHeading(
    const WorldState& world,
    const Vec2f& origin,
    float currentHeading,
    const std::array<float, 4>& turnCandidates,
    int candidateCount,
    float requiredDistance) {
    float bestHeading = currentHeading;
    float bestDistance = -1.0F;
    for (int i = 0; i < candidateCount; ++i) {
        const float candidate = QuantizeToEightDirections(currentHeading + turnCandidates[static_cast<std::size_t>(i)]);
        const float freeDist = game::geometry::FreeDistanceAhead(
            world,
            origin,
            candidate,
            requiredDistance + 2.0F,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
            kEnemyPlanningClearanceScale);
        if (freeDist > bestDistance) {
            bestDistance = freeDist;
            bestHeading = candidate;
        }
    }
    if (bestDistance >= requiredDistance) {
        return bestHeading;
    }
    return std::numeric_limits<float>::quiet_NaN();
}

int ClampCellX(const game::navigation::CellCoordCache& cellCache, float x) {
    return cellCache.ClampCellX(x);
}

int ClampCellY(const game::navigation::CellCoordCache& cellCache, float y) {
    return cellCache.ClampCellY(y);
}

int CellIndex(const game::navigation::CellCoordCache& cellCache, int x, int y) {
    return cellCache.CellIndex(x, y);
}

Vec2f CellCenter(const game::navigation::CellCoordCache& cellCache, int x, int y) {
    return cellCache.CellCenter(x, y);
}

bool CanStepToNeighbor(
    const WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    int x,
    int y,
    int nx,
    int ny) {
    if (nx < 0 || ny < 0 || nx >= world.maze.widthCells || ny >= world.maze.heightCells) {
        return false;
    }
    const bool startInsideBase =
        IsPointInUndestroyedBase(world, CellCenter(cellCache, x, y), GameplayConstants::kTankCollisionRadiusUnits);
    if (!startInsideBase &&
        IsPointInUndestroyedBase(world, CellCenter(cellCache, nx, ny), GameplayConstants::kTankCollisionRadiusUnits)) {
        return false;
    }
    const MazeCell& cell = world.maze.cells[static_cast<std::size_t>(CellIndex(cellCache, x, y))];
    if (nx == x + 1) {
        return !cell.eastWall;
    }
    if (nx == x - 1) {
        return !cell.westWall;
    }
    if (ny == y + 1) {
        return !cell.southWall;
    }
    if (ny == y - 1) {
        return !cell.northWall;
    }
    return false;
}

struct AStarNode {
    int parent = -1;
    float g = std::numeric_limits<float>::infinity();
    float f = std::numeric_limits<float>::infinity();
    bool open = false;
    bool closed = false;
};

struct OpenNode {
    int index = 0;
    float f = 0.0F;
};

struct OpenNodeCompare {
    bool operator()(const OpenNode& a, const OpenNode& b) const {
        return a.f > b.f;
    }
};

struct PathfindingPool {
    std::vector<bool> occupied{};
    std::vector<AStarNode> nodes{};
    std::vector<OpenNode> openHeap{};
    std::vector<int> pathCells{};

    void EnsureCapacity(int totalCells) {
        const auto n = static_cast<std::size_t>(totalCells);
        if (occupied.size() < n) {
            occupied.resize(n);
        }
        if (nodes.size() < n) {
            nodes.resize(n);
        }
        if (openHeap.capacity() < static_cast<std::size_t>(std::min(1024, totalCells))) {
            openHeap.reserve(static_cast<std::size_t>(std::min(1024, totalCells)));
        }
        if (pathCells.capacity() < static_cast<std::size_t>(std::min(512, totalCells))) {
            pathCells.reserve(static_cast<std::size_t>(std::min(512, totalCells)));
        }
    }
};

PathfindingPool& GetPathfindingPool() {
    static PathfindingPool pool;
    return pool;
}

float HeuristicManhattan(int x, int y, int tx, int ty) {
    return static_cast<float>(std::abs(tx - x) + std::abs(ty - y));
}

// Only called from BuildAssassinPath, which is only reachable when
// kUseAssassinAStarBackupNavigation = true. Currently dead at runtime.
void BuildEnemyOccupancy(
    const GameState& state,
    const game::navigation::CellCoordCache& cellCache,
    int ignoreEnemyIndex,
    std::vector<bool>& occupied) {
    const int totalCells = state.world.maze.widthCells * state.world.maze.heightCells;
    occupied.assign(static_cast<std::size_t>(totalCells), false);
    for (int i = 0; i < static_cast<int>(state.world.enemies.size()); ++i) {
        if (i == ignoreEnemyIndex) {
            continue;
        }
        const EnemyTank& enemy = state.world.enemies[static_cast<std::size_t>(i)];
        if (!enemy.alive) {
            continue;
        }
        const int cx = ClampCellX(cellCache, enemy.position.x);
        const int cy = ClampCellY(cellCache, enemy.position.y);
        occupied[static_cast<std::size_t>(CellIndex(cellCache, cx, cy))] = true;
    }
}

// Only reachable when kUseAssassinAStarBackupNavigation = true. Currently dead at runtime.
bool BuildAssassinPath(
    GameState& state,
    const game::navigation::CellCoordCache& cellCache,
    EnemyTank& enemy,
    int enemyIndex) {
    profiling::ScopedProfile totalScope(profiling::Scope::PathfindingTotal, true);
    gEnemyRuntimeWindowStats.navPathBuildCalls += 1;
    const int width = state.world.maze.widthCells;
    const int height = state.world.maze.heightCells;
    const int totalCells = width * height;
    if (totalCells <= 0) {
        return false;
    }

    const int startX = ClampCellX(cellCache, enemy.position.x);
    const int startY = ClampCellY(cellCache, enemy.position.y);
    const int goalX = ClampCellX(cellCache, state.world.player.position.x);
    const int goalY = ClampCellY(cellCache, state.world.player.position.y);
    const int startIndex = CellIndex(cellCache, startX, startY);
    const int goalIndex = CellIndex(cellCache, goalX, goalY);
    if (startIndex == goalIndex) {
        enemy.pathWaypointCount = 0;
        enemy.pathWaypointIndex = 0;
        return false;
    }

    PathfindingPool& pool = GetPathfindingPool();
    pool.EnsureCapacity(totalCells);

    std::vector<bool>& occupied = pool.occupied;
    std::vector<AStarNode>& nodes = pool.nodes;
    std::vector<OpenNode>& openHeap = pool.openHeap;
    OpenNodeCompare cmp{};

    {
        profiling::ScopedProfile occupancyScope(profiling::Scope::PathfindingOccupancy, true);
        BuildEnemyOccupancy(state, cellCache, enemyIndex, occupied);
    }
    occupied[static_cast<std::size_t>(startIndex)] = false;
    occupied[static_cast<std::size_t>(goalIndex)] = false;

    const std::size_t n = static_cast<std::size_t>(totalCells);
    for (std::size_t i = 0; i < n; ++i) {
        nodes[i] = AStarNode{};
    }
    nodes[static_cast<std::size_t>(startIndex)].g = 0.0F;
    nodes[static_cast<std::size_t>(startIndex)].f = HeuristicManhattan(startX, startY, goalX, goalY);
    nodes[static_cast<std::size_t>(startIndex)].open = true;
    openHeap.clear();
    openHeap.push_back(OpenNode{.index = startIndex, .f = nodes[static_cast<std::size_t>(startIndex)].f});
    std::push_heap(openHeap.begin(), openHeap.end(), cmp);

    const std::array<int, 4> dx{1, -1, 0, 0};
    const std::array<int, 4> dy{0, 0, 1, -1};
    bool found = false;
    {
        profiling::ScopedProfile searchScope(profiling::Scope::PathfindingSearch, true);
        while (!openHeap.empty()) {
            std::pop_heap(openHeap.begin(), openHeap.end(), cmp);
            const OpenNode top = openHeap.back();
            openHeap.pop_back();
            AStarNode& current = nodes[static_cast<std::size_t>(top.index)];
            if (current.closed) {
                continue;
            }
            current.closed = true;
            if (top.index == goalIndex) {
                found = true;
                break;
            }

            const int x = top.index % width;
            const int y = top.index / width;
            for (int i = 0; i < 4; ++i) {
                const int nx = x + dx[static_cast<std::size_t>(i)];
                const int ny = y + dy[static_cast<std::size_t>(i)];
                if (!CanStepToNeighbor(state.world, cellCache, x, y, nx, ny)) {
                    continue;
                }
                const int ni = CellIndex(cellCache, nx, ny);
                if (occupied[static_cast<std::size_t>(ni)] && ni != goalIndex) {
                    continue;
                }

                AStarNode& neighbor = nodes[static_cast<std::size_t>(ni)];
                if (neighbor.closed) {
                    continue;
                }
                const float tentativeG = current.g + 1.0F;
                if (tentativeG >= neighbor.g) {
                    continue;
                }
                neighbor.parent = top.index;
                neighbor.g = tentativeG;
                neighbor.f = tentativeG + HeuristicManhattan(nx, ny, goalX, goalY);
                neighbor.open = true;
                openHeap.push_back(OpenNode{.index = ni, .f = neighbor.f});
                std::push_heap(openHeap.begin(), openHeap.end(), cmp);
            }
        }
    }

    if (!found) {
        enemy.pathWaypointCount = 0;
        enemy.pathWaypointIndex = 0;
        return false;
    }

    {
        profiling::ScopedProfile postprocessScope(profiling::Scope::PathfindingPostprocess, true);
        std::vector<int>& pathCells = pool.pathCells;
        pathCells.clear();
        int trace = goalIndex;
        while (trace != -1) {
            pathCells.push_back(trace);
            if (trace == startIndex) {
                break;
            }
            trace = nodes[static_cast<std::size_t>(trace)].parent;
        }
        if (pathCells.empty() || pathCells.back() != startIndex) {
            enemy.pathWaypointCount = 0;
            enemy.pathWaypointIndex = 0;
            return false;
        }
        std::reverse(pathCells.begin(), pathCells.end());

        enemy.pathWaypointCount = 0;
        enemy.pathWaypointIndex = 0;
        int lastStepX = 0;
        int lastStepY = 0;
        for (int i = 1; i < static_cast<int>(pathCells.size()); ++i) {
            const int prev = pathCells[static_cast<std::size_t>(i - 1)];
            const int curr = pathCells[static_cast<std::size_t>(i)];
            const int px = prev % width;
            const int py = prev / width;
            const int cx = curr % width;
            const int cy = curr / width;
            const int stepX = cx - px;
            const int stepY = cy - py;
            const bool turnPoint =
                (i == 1) ||
                (stepX != lastStepX) ||
                (stepY != lastStepY) ||
                (i == static_cast<int>(pathCells.size()) - 1);
            lastStepX = stepX;
            lastStepY = stepY;
            if (!turnPoint) {
                continue;
            }
            if (enemy.pathWaypointCount >= EnemyTank::kMaxPathWaypoints) {
                break;
            }
            enemy.pathWaypoints[static_cast<std::size_t>(enemy.pathWaypointCount)] = CellCenter(cellCache, cx, cy);
            ++enemy.pathWaypointCount;
        }
    }
    const bool success = enemy.pathWaypointCount > 0;
    if (success) {
        gEnemyRuntimeWindowStats.navPathBuildSuccesses += 1;
    }
    return success;
}

// Only reachable when kUseAssassinAStarBackupNavigation = true. Currently dead at runtime.
bool BuildAssassinPathToTarget(
    GameState& state,
    const game::navigation::CellCoordCache& cellCache,
    EnemyTank& enemy,
    int enemyIndex,
    const Vec2f& target) {
    const Vec2f previousPlayerPosition = state.world.player.position;
    state.world.player.position = target;
    const bool built = BuildAssassinPath(state, cellCache, enemy, enemyIndex);
    state.world.player.position = previousPlayerPosition;
    return built;
}

Vec2f RandomMazePoint(
    const WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    Random& random) {
    const int cellX = random.NextInt(0, world.maze.widthCells - 1);
    const int cellY = random.NextInt(0, world.maze.heightCells - 1);
    return CellCenter(cellCache, cellX, cellY);
}

// Only reachable when kUseAssassinAStarBackupNavigation = true. Currently dead at runtime.
bool BuildAssassinPathToFarRandomTarget(
    GameState& state,
    const game::navigation::CellCoordCache& cellCache,
    EnemyTank& enemy,
    int enemyIndex,
    Random& random) {
    profiling::ScopedProfile scope(profiling::Scope::PathfindingFarTarget, true);
    constexpr float kMinRandomTargetDistanceUnits = 24.0F;
    constexpr float kMinRandomTargetDistanceSq = kMinRandomTargetDistanceUnits * kMinRandomTargetDistanceUnits;
    constexpr int kMaxTargetAttempts = 24;
    for (int attempt = 0; attempt < kMaxTargetAttempts; ++attempt) {
        const Vec2f randomTarget = RandomMazePoint(state.world, cellCache, random);
        if (DistanceSq(randomTarget, enemy.position) < kMinRandomTargetDistanceSq) {
            continue;
        }
        if (BuildAssassinPathToTarget(state, cellCache, enemy, enemyIndex, randomTarget)) {
            return true;
        }
    }

    // Fallback: still pick some random destination if no far target succeeded.
    const Vec2f fallbackTarget = RandomMazePoint(state.world, cellCache, random);
    return BuildAssassinPathToTarget(state, cellCache, enemy, enemyIndex, fallbackTarget);
}

bool TrySelectAssassinFlowNextStep(
    const game::navigation::CellCoordCache& cellCache,
    const game::navigation::PlayerFlowField& flowField,
    EnemyTank& enemy,
    bool& outNeedsInitialFlowBuild,
    float& outHeadingRadians) {
    outNeedsInitialFlowBuild = false;
    if (!flowField.HasBuild()) {
        outNeedsInitialFlowBuild = true;
        gEnemyRuntimeWindowStats.navFlowMisses += 1;
        return false;
    }

    const game::navigation::MazeCellCoord enemyCell = cellCache.WorldToCell(enemy.position);
    const int enemyCellHash = cellCache.CellHash(enemyCell.x, enemyCell.y);
    const int playerHash = cellCache.PlayerCellHash();
    if (enemy.cachedPlayerCellHash != playerHash) {
        enemy.cachedPlayerCellHash = playerHash;
        enemy.expectedPathCellHash = -1;
        enemy.cachedFlowFromCellHash = -1;
    }
    if (enemy.cachedFlowFromCellHash == enemyCellHash && enemy.expectedPathCellHash >= 0) {
        outHeadingRadians = enemy.cachedFlowHeadingRadians;
        gEnemyRuntimeWindowStats.navFlowHeadingSelections += 1;
        return true;
    }

    const int nextCellHash = flowField.NextCellHash(enemyCellHash);
    if (nextCellHash < 0 || nextCellHash == enemyCellHash) {
        enemy.expectedPathCellHash = -1;
        enemy.cachedFlowFromCellHash = -1;
        gEnemyRuntimeWindowStats.navFlowMisses += 1;
        return false;
    }

    const Vec2f nextCenter = flowField.NextCellCenter(enemyCellHash, cellCache);
    const Vec2f toNext{
        .x = nextCenter.x - enemy.position.x,
        .y = nextCenter.y - enemy.position.y,
    };
    const Vec2f stepDir = NormalizeOrZero(toNext);
    if (stepDir.x == 0.0F && stepDir.y == 0.0F) {
        enemy.expectedPathCellHash = -1;
        enemy.cachedFlowFromCellHash = -1;
        gEnemyRuntimeWindowStats.navFlowMisses += 1;
        return false;
    }

    enemy.expectedPathCellHash = nextCellHash;
    outHeadingRadians = QuantizeToEightDirections(std::atan2(stepDir.x, -stepDir.y));
    enemy.cachedFlowFromCellHash = enemyCellHash;
    enemy.cachedFlowHeadingRadians = outHeadingRadians;
    gEnemyRuntimeWindowStats.navFlowHeadingSelections += 1;
    return true;
}

bool SelectDroneReturnToBaseHeading(
    const WorldState& world,
    const EnemyTank& enemy,
    Random& random,
    float& selectedHeading) {
    const Vec2f nearestBase = NearestBasePosition(world, enemy.position);
    const Vec2f toBase{
        .x = nearestBase.x - enemy.position.x,
        .y = nearestBase.y - enemy.position.y,
    };
    if (std::fabs(toBase.x) <= 0.001F && std::fabs(toBase.y) <= 0.001F) {
        return false;
    }

    const float desiredHeading = QuantizeToEightDirections(std::atan2(toBase.x, -toBase.y));
    const std::array<float, 8> offsets{
        0.0F,
        kEightDirectionStep,
        -kEightDirectionStep,
        kEightDirectionStep * 2.0F,
        -kEightDirectionStep * 2.0F,
        kEightDirectionStep * 3.0F,
        -kEightDirectionStep * 3.0F,
        kEightDirectionStep * 4.0F};

    std::array<float, 8> candidateHeadings{};
    int candidateCount = 0;
    int bestCandidateIndex = -1;
    float bestAlignment = std::numeric_limits<float>::infinity();
    for (float offset : offsets) {
        const float candidate = QuantizeToEightDirections(desiredHeading + offset);
        const float clearDistance = game::geometry::FreeDistanceAhead(
            world,
            enemy.position,
            candidate,
            kDroneReturnRequiredClearRunUnits,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
            kEnemyPlanningClearanceScale);
        if (clearDistance < kDroneReturnRequiredClearRunUnits) {
            continue;
        }
        const float alignment = AngleDistance(candidate, desiredHeading);
        if (candidateCount < static_cast<int>(candidateHeadings.size())) {
            candidateHeadings[static_cast<std::size_t>(candidateCount)] = candidate;
            if (alignment < bestAlignment) {
                bestAlignment = alignment;
                bestCandidateIndex = candidateCount;
            }
            ++candidateCount;
        }
    }

    if (candidateCount <= 0 || bestCandidateIndex < 0) {
        return false;
    }
    if (candidateCount == 1) {
        selectedHeading = candidateHeadings[0];
        return true;
    }

    constexpr float kBestHeadingWeight = 0.6F;
    constexpr float kOtherHeadingsTotalWeight = 0.4F;
    const float otherWeightEach = kOtherHeadingsTotalWeight / static_cast<float>(candidateCount - 1);
    const float pick = random.NextFloat(0.0F, 1.0F);
    float cumulative = 0.0F;
    for (int i = 0; i < candidateCount; ++i) {
        const float weight = (i == bestCandidateIndex) ? kBestHeadingWeight : otherWeightEach;
        cumulative += weight;
        if (pick <= cumulative || i == candidateCount - 1) {
            selectedHeading = candidateHeadings[static_cast<std::size_t>(i)];
            return true;
        }
    }

    selectedHeading = candidateHeadings[static_cast<std::size_t>(bestCandidateIndex)];
    return true;
}

bool SelectDroneWatchEscapeHeading(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    float deltaSeconds,
    float& selectedHeading) {
    const EnemyTank& self = enemies[static_cast<std::size_t>(selfIndex)];

    float currentNearestDistance = std::numeric_limits<float>::infinity();
    int nearestEnemyIndex = -1;
    for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
        if (i == selfIndex) {
            continue;
        }
        const EnemyTank& other = enemies[static_cast<std::size_t>(i)];
        if (!other.alive) {
            continue;
        }
        const float dist = Distance(self.position, other.position);
        if (dist < currentNearestDistance) {
            currentNearestDistance = dist;
            nearestEnemyIndex = i;
        }
    }

    float awayHeading = self.headingRadians;
    if (nearestEnemyIndex >= 0) {
        const EnemyTank& nearestEnemy = enemies[static_cast<std::size_t>(nearestEnemyIndex)];
        awayHeading = std::atan2(
            self.position.x - nearestEnemy.position.x,
            -(self.position.y - nearestEnemy.position.y));
    }

    const std::array<float, 8> offsets{
        0.0F,
        kEightDirectionStep,
        -kEightDirectionStep,
        kEightDirectionStep * 2.0F,
        -kEightDirectionStep * 2.0F,
        kEightDirectionStep * 3.0F,
        -kEightDirectionStep * 3.0F,
        kEightDirectionStep * 4.0F};

    const float stepDistance = GameplayConstants::kEnemyDroneSpeed * deltaSeconds;
    bool found = false;
    float bestNearestDistance = -1.0F;
    float bestAwayAlignment = std::numeric_limits<float>::infinity();
    float bestHeading = self.headingRadians;
    for (float offset : offsets) {
        const float candidateHeading = QuantizeToEightDirections(self.headingRadians + offset);
        const float clearDistance = game::geometry::FreeDistanceAhead(
            world,
            self.position,
            candidateHeading,
            GameplayConstants::kEnemyRequiredClearRunUnits + 0.5F,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
            kEnemyPlanningClearanceScale);
        if (clearDistance <= GameplayConstants::kEnemyRequiredClearRunUnits) {
            continue;
        }

        const Vec2f dir = DirectionFromHeading(candidateHeading);
        const Vec2f candidatePosition{
            .x = self.position.x + dir.x * stepDistance,
            .y = self.position.y + dir.y * stepDistance,
        };

        float nearestDistance = std::numeric_limits<float>::infinity();
        for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
            if (i == selfIndex) {
                continue;
            }
            const EnemyTank& other = enemies[static_cast<std::size_t>(i)];
            if (!other.alive) {
                continue;
            }
            nearestDistance = std::min(nearestDistance, Distance(candidatePosition, other.position));
        }
        const float awayAlignment = AngleDistance(candidateHeading, awayHeading);

        if (!found ||
            nearestDistance > bestNearestDistance + 0.001F ||
            (std::fabs(nearestDistance - bestNearestDistance) <= 0.001F &&
             awayAlignment < bestAwayAlignment)) {
            found = true;
            bestNearestDistance = nearestDistance;
            bestAwayAlignment = awayAlignment;
            bestHeading = candidateHeading;
        }
    }

    if (!found) {
        return false;
    }

    if (currentNearestDistance < GameplayConstants::kEnemyPreferredSeparationUnits &&
        bestNearestDistance <= currentNearestDistance + 0.001F) {
        return false;
    }

    selectedHeading = bestHeading;
    return true;
}

bool PlayerAheadForTorpedo(const EnemyTank& enemy, const Vec2f& toPlayerNormalized) {
    const Vec2f forward = DirectionFromHeading(enemy.headingRadians);
    const float dot = forward.x * toPlayerNormalized.x + forward.y * toPlayerNormalized.y;
    return dot >= kCosThirtyDegrees;
}

float SelectBestLongStraightHeading(const WorldState& world, const EnemyTank& enemy) {
    float bestHeading = QuantizeToEightDirections(enemy.headingRadians);
    float bestClear = -1.0F;
    for (int step = 0; step < 8; ++step) {
        const float candidate = NormalizeAngle(static_cast<float>(step) * kEightDirectionStep);
        const float clearDist = game::geometry::FreeDistanceAhead(
            world,
            enemy.position,
            candidate,
            kTorpedoLongPathProbeUnits,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
            kEnemyPlanningClearanceScale);
        if (clearDist > bestClear) {
            bestClear = clearDist;
            bestHeading = candidate;
        }
    }
    return QuantizeToEightDirections(bestHeading);
}

float SelectTorpedoMoveHeading(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    EnemyTank& enemy,
    Random& random,
    bool& startRetreat,
    bool& decidedStraight,
    const game::spatial::EnemySpatialGrid* spatialGrid) {
    profiling::ScopedProfile selectScope(profiling::Scope::EnemyTorpedoSelectHeading, true);
    gEnemyRuntimeWindowStats.torpedoHeadingEvalCalls += 1;
    startRetreat = false;
    decidedStraight = true;
    const float straightHeading = QuantizeToEightDirections(enemy.headingRadians);
    const float straightClearWithEnemies = game::geometry::FreeDistanceAheadWithEnemies(
        world,
        enemies,
        selfIndex,
        enemy.position,
        straightHeading,
        kSegmentBuildProbeMaxUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
        kEnemyPlanningClearanceScale,
        spatialGrid);
    const float leftHeading = QuantizeToEightDirections(straightHeading - kEightDirectionStep);
    const float rightHeading = QuantizeToEightDirections(straightHeading + kEightDirectionStep);
    const float leftClear = game::geometry::FreeDistanceAheadWithEnemies(
        world,
        enemies,
        selfIndex,
        enemy.position,
        leftHeading,
        kSegmentBuildProbeMaxUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
        kEnemyPlanningClearanceScale,
        spatialGrid);
    const float rightClear = game::geometry::FreeDistanceAheadWithEnemies(
        world,
        enemies,
        selfIndex,
        enemy.position,
        rightHeading,
        kSegmentBuildProbeMaxUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
        kEnemyPlanningClearanceScale,
        spatialGrid);

    if (straightClearWithEnemies < kTorpedoImmediateObstacleDistanceUnits &&
        leftClear < kTorpedoImmediateObstacleDistanceUnits &&
        rightClear < kTorpedoImmediateObstacleDistanceUnits) {
        startRetreat = true;
        gEnemyRuntimeWindowStats.torpedoHeadingRetreatStarts += 1;
        return straightHeading;
    }

    struct Candidate {
        float heading;
        float clearDistance;
    };
    const std::array<Candidate, 3> candidates{{
        {.heading = straightHeading, .clearDistance = straightClearWithEnemies},
        {.heading = leftHeading, .clearDistance = leftClear},
        {.heading = rightHeading, .clearDistance = rightClear},
    }};
    float bestClear = -1.0F;
    for (const Candidate& candidate : candidates) {
        bestClear = std::max(bestClear, candidate.clearDistance);
    }
    std::array<int, 3> bestIndices{};
    int bestCount = 0;
    constexpr float kTieEpsilon = 0.001F;
    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
        if (std::fabs(candidates[static_cast<std::size_t>(i)].clearDistance - bestClear) <= kTieEpsilon) {
            bestIndices[static_cast<std::size_t>(bestCount)] = i;
            ++bestCount;
        }
    }
    const int chosenIndex =
        bestIndices[static_cast<std::size_t>(random.NextInt(0, std::max(0, bestCount - 1)))];
    const Candidate& chosen = candidates[static_cast<std::size_t>(chosenIndex)];
    gEnemyRuntimeWindowStats.torpedoHeadingBestClearSum += static_cast<double>(bestClear);
    gEnemyRuntimeWindowStats.torpedoHeadingChosenClearSum += static_cast<double>(chosen.clearDistance);
    if (chosenIndex == 0) {
        gEnemyRuntimeWindowStats.torpedoHeadingChosenStraight += 1;
    } else if (chosenIndex == 1) {
        gEnemyRuntimeWindowStats.torpedoHeadingChosenLeft += 1;
    } else {
        gEnemyRuntimeWindowStats.torpedoHeadingChosenRight += 1;
    }
    const float maxSegmentLength = chosen.clearDistance - kSegmentBuildSafetyReduceUnits;
    if (maxSegmentLength < kSegmentBuildMinLengthUnits) {
        startRetreat = true;
        gEnemyRuntimeWindowStats.torpedoHeadingRetreatStarts += 1;
        return straightHeading;
    }
    enemy.torpedoMoveDecisionHoldRemainingUnits =
        random.NextFloat(kSegmentBuildMinLengthUnits, maxSegmentLength);
    enemy.torpedoStraightDistanceSinceTurnUnits = 0.0F;
    decidedStraight = chosen.heading == straightHeading;

    return chosen.heading;
}

void EnterTorpedoTargetingMode(EnemyTank& enemy) {
    enemy.torpedoMoveMode = TorpedoMoveMode::Targeting;
}

void EnterTorpedoRotateMode(EnemyTank& enemy) {
    enemy.torpedoMoveMode = TorpedoMoveMode::Rotate;
    enemy.torpedoRotateTargetHeadingRadians = enemy.torpedoChosenHeadingRadians;
}

float UpdateTorpedoRotateHeading(EnemyTank& enemy, float deltaSeconds) {
    const float rotateStep =
        (kPi * 2.0F / GameplayConstants::kSlowRotateFullTurnSeconds) * deltaSeconds;
    const float signedDelta = SignedAngleDelta(enemy.headingRadians, enemy.torpedoRotateTargetHeadingRadians);
    if (std::fabs(signedDelta) <= rotateStep + 0.0001F) {
        const float heading = QuantizeToEightDirections(enemy.torpedoRotateTargetHeadingRadians);
        enemy.torpedoMoveMode = TorpedoMoveMode::Move;
        enemy.torpedoStraightDistanceSinceTurnUnits = 0.0F;
        enemy.torpedoMoveDecisionHoldRemainingUnits = 0.0F;
        return heading;
    }
    const float direction = signedDelta > 0.0F ? 1.0F : -1.0F;
    return NormalizeAngle(enemy.headingRadians + direction * rotateStep);
}

float SelectScoutHeadingWithFallback(
    const WorldState& world,
    const EnemyTank& enemy,
    bool allowNinetyTurns,
    bool& shouldRotate) {
    const float lookahead = game::geometry::FreeDistanceAhead(
        world,
        enemy.position,
        enemy.headingRadians,
        GameplayConstants::kEnemyLookaheadObstacleUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
        kEnemyPlanningClearanceScale);
    if (lookahead >= GameplayConstants::kEnemyLookaheadObstacleUnits) {
        shouldRotate = false;
        return enemy.headingRadians;
    }

    const std::array<float, 4> turns45{
        -kEightDirectionStep, kEightDirectionStep, 0.0F, 0.0F};
    const float turned45 = ChooseBestTurnHeading(
        world,
        enemy.position,
        enemy.headingRadians,
        turns45,
        2,
        GameplayConstants::kEnemyRequiredClearRunUnits);
    if (!std::isnan(turned45)) {
        shouldRotate = false;
        return turned45;
    }

    if (allowNinetyTurns) {
        const std::array<float, 4> turns90{
            -kEightDirectionStep * 2.0F, kEightDirectionStep * 2.0F, 0.0F, 0.0F};
        const float turned90 = ChooseBestTurnHeading(
            world,
            enemy.position,
            enemy.headingRadians,
            turns90,
            2,
            GameplayConstants::kEnemyRequiredClearRunUnits);
        if (!std::isnan(turned90)) {
            shouldRotate = false;
            return turned90;
        }
    }

    shouldRotate = true;
    return enemy.headingRadians;
}

bool TrySeparationTurn(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    float speed,
    float deltaSeconds,
    float& chosenHeading,
    Vec2f& candidatePosition) {
    const EnemyTank& self = enemies[static_cast<std::size_t>(selfIndex)];
    const std::array<float, 2> turnOffsets{-kEightDirectionStep, kEightDirectionStep};
    float bestDistanceSq = -1.0F;
    const float sepSq = GameplayConstants::kEnemyPreferredSeparationUnits * GameplayConstants::kEnemyPreferredSeparationUnits;
    bool found = false;
    for (float offset : turnOffsets) {
        const float candidateHeading = QuantizeToEightDirections(self.headingRadians + offset);
        const Vec2f dir = DirectionFromHeading(candidateHeading);
        const Vec2f candidate{
            .x = self.position.x + dir.x * speed * deltaSeconds,
            .y = self.position.y + dir.y * speed * deltaSeconds,
        };
        if (SegmentIntersectsWall(
                world,
                self.position,
                candidate,
                GameplayConstants::kEnemyWallAvoidanceRadiusUnits)) {
            continue;
        }
        float nearestSq = std::numeric_limits<float>::infinity();
        for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
            if (i == selfIndex) {
                continue;
            }
            const EnemyTank& other = enemies[static_cast<std::size_t>(i)];
            if (!other.alive) {
                continue;
            }
            nearestSq = std::min(nearestSq, DistanceSq(candidate, other.position));
        }
        if (nearestSq > bestDistanceSq && nearestSq >= sepSq) {
            bestDistanceSq = nearestSq;
            chosenHeading = candidateHeading;
            candidatePosition = candidate;
            found = true;
        }
    }
    return found && bestDistanceSq >= sepSq;
}

bool IsMovementBlockedByEnemies(
    const std::vector<EnemyTank>& enemies,
    const std::vector<Vec2f>& frameStartPositions,
    int selfIndex,
    const Vec2f& from,
    const Vec2f& to,
    float minSeparation,
    const std::vector<float>* uncoupleEscapeScores = nullptr) {
    auto otherYieldsToSelf = [&](int otherIndex) {
        if (uncoupleEscapeScores == nullptr) {
            return false;
        }
        if (selfIndex < 0 || otherIndex < 0 ||
            selfIndex >= static_cast<int>(enemies.size()) ||
            otherIndex >= static_cast<int>(enemies.size())) {
            return false;
        }
        const EnemyTank& self = enemies[static_cast<std::size_t>(selfIndex)];
        const EnemyTank& other = enemies[static_cast<std::size_t>(otherIndex)];
        if (self.aiMode != EnemyAiMode::Uncouple || other.aiMode != EnemyAiMode::Uncouple) {
            return false;
        }
        if (selfIndex >= static_cast<int>(uncoupleEscapeScores->size()) ||
            otherIndex >= static_cast<int>(uncoupleEscapeScores->size())) {
            return false;
        }
        const float selfScore = (*uncoupleEscapeScores)[static_cast<std::size_t>(selfIndex)];
        const float otherScore = (*uncoupleEscapeScores)[static_cast<std::size_t>(otherIndex)];
        if (selfScore > otherScore + kUncouplePriorityEpsilon) {
            return true;
        }
        if (std::fabs(selfScore - otherScore) <= kUncouplePriorityEpsilon) {
            return selfIndex < otherIndex;
        }
        return false;
    };

    constexpr float kSeparationProgressEpsilon = 0.001F;
    for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
        if (i == selfIndex) {
            continue;
        }
        if (otherYieldsToSelf(i)) {
            continue;
        }
        const EnemyTank& other = enemies[static_cast<std::size_t>(i)];
        if (!other.alive) {
            continue;
        }

        // Use updated position for already-processed enemies and frame-start position for others.
        const Vec2f otherObstacle = (i < selfIndex)
            ? other.position
            : frameStartPositions[static_cast<std::size_t>(i)];

        const float fromDistance = Distance(from, otherObstacle);
        const float toDistance = Distance(to, otherObstacle);
        const bool separatingFromOverlap =
            fromDistance < minSeparation &&
            toDistance > fromDistance + kSeparationProgressEpsilon;

        if (toDistance < minSeparation && !separatingFromOverlap) {
            return true;
        }
        if (!separatingFromOverlap &&
            DistancePointToSegment(otherObstacle, from, to) < minSeparation) {
            return true;
        }
    }
    return false;
}

void ResolveEnemySeparationLegacyGrid(
    WorldState& world,
    game::spatial::EnemySpatialGrid& grid,
    const std::vector<std::uint8_t>& includeMask,
    const std::vector<std::uint8_t>& reenteredFullTierMask) {
    (void)reenteredFullTierMask;
    profiling::ScopedProfile scope(profiling::Scope::EnemySeparation, true);
    {
        std::uint64_t included = 0;
        for (std::size_t i = 0; i < world.enemies.size() && i < includeMask.size(); ++i) {
            if (includeMask[i] == 0U) {
                continue;
            }
            if (!world.enemies[i].alive) {
                continue;
            }
            included += 1;
        }
        gEnemyRuntimeWindowStats.separationGridCandidates += included;
    }
    {
        profiling::ScopedProfile buildScope(profiling::Scope::EnemySeparationGridBuild, true);
        grid.BuildFromPositions(world, nullptr, &includeMask);
    }
    {
        profiling::ScopedProfile pairTraverseScope(profiling::Scope::EnemySeparationPairTraverse, true);
        grid.ForEachPairInSameOrAdjacentCell(world.enemies, [&](int i, int j) {
            gEnemyRuntimeStats.separationPairsVisited += 1;
            EnemyTank& a = world.enemies[static_cast<std::size_t>(i)];
            EnemyTank& b = world.enemies[static_cast<std::size_t>(j)];
            gEnemyRuntimeWindowStats.separationPairsByType[PairTypeMatrixIndex(a.type, b.type)] += 1;
            if (game::geometry::IsPointInUndestroyedBase(world, a.position, 1.0F) ||
                game::geometry::IsPointInUndestroyedBase(world, b.position, 1.0F)) {
                gEnemyRuntimeWindowStats.separationPairsBaseSkipped += 1;
                return;
            }
            profiling::ScopedProfile pairScope(profiling::Scope::EnemySeparationPairResolve, true);

            const float distSq = DistanceSq(a.position, b.position);
            const float killDistSq =
                GameplayConstants::kEnemyMutualKillDistanceUnits * GameplayConstants::kEnemyMutualKillDistanceUnits;
            if (distSq <= killDistSq) {
                EnterUncoupleMode(world.enemies, j, i, UncoupleReason::SeparationProximity);
                EnterUncoupleMode(world.enemies, i, j, UncoupleReason::SeparationProximity);
            }
            const float sepSq =
                GameplayConstants::kEnemyPreferredSeparationUnits * GameplayConstants::kEnemyPreferredSeparationUnits;
            if (distSq >= sepSq) {
                return;
            }
            if (!ShouldEnterSeparationUncouple(a, b, distSq)) {
                return;
            }
            gEnemyRuntimeStats.separationPairsResolved += 1;
            EnterUncoupleMode(world.enemies, j, i, UncoupleReason::SeparationProximity);
            EnterUncoupleMode(world.enemies, i, j, UncoupleReason::SeparationProximity);
        });
    }
}

void ResolveEnemyFrontalCollisionsLegacyGrid(
    WorldState& world,
    const std::vector<Vec2f>& frameStartPositions,
    game::spatial::EnemySpatialGrid& grid,
    const std::vector<std::uint8_t>& includeMask,
    const std::vector<std::uint8_t>& reenteredFullTierMask) {
    (void)reenteredFullTierMask;
    profiling::ScopedProfile scope(profiling::Scope::EnemyFrontalCollisions, true);
    {
        const float cellSize = GameplayConstants::kMazeCellSizeUnits;
        auto worldToCellX = [&](float x) {
            const int cx = static_cast<int>(std::floor(x / cellSize));
            return std::max(0, std::min(world.maze.widthCells - 1, cx));
        };
        auto worldToCellY = [&](float y) {
            const int cy = static_cast<int>(std::floor(y / cellSize));
            return std::max(0, std::min(world.maze.heightCells - 1, cy));
        };
        std::uint64_t included = 0;
        std::uint64_t crossedCell = 0;
        for (std::size_t i = 0; i < world.enemies.size() && i < includeMask.size(); ++i) {
            if (includeMask[i] == 0U) {
                continue;
            }
            const EnemyTank& enemy = world.enemies[i];
            if (!enemy.alive) {
                continue;
            }
            included += 1;
            const Vec2f& start = frameStartPositions[i];
            if (worldToCellX(start.x) != worldToCellX(enemy.position.x) ||
                worldToCellY(start.y) != worldToCellY(enemy.position.y)) {
                crossedCell += 1;
            }
        }
        gEnemyRuntimeWindowStats.frontalGridCandidates += included;
        gEnemyRuntimeWindowStats.frontalGridCellTransitions += crossedCell;
        gEnemyRuntimeWindowStats.frontalGridInsertEstimate += included + crossedCell;
    }
    {
        profiling::ScopedProfile buildScope(profiling::Scope::EnemyFrontalGridBuild, true);
        grid.BuildFromSegments(world, frameStartPositions, &includeMask);
    }
    {
        profiling::ScopedProfile pairTraverseScope(profiling::Scope::EnemyFrontalPairTraverse, true);
        grid.ForEachPairInSameOrAdjacentCell(world.enemies, [&](int i, int j) {
            gEnemyRuntimeStats.frontalPairsVisited += 1;
            EnemyTank& a = world.enemies[static_cast<std::size_t>(i)];
            EnemyTank& b = world.enemies[static_cast<std::size_t>(j)];
            gEnemyRuntimeWindowStats.frontalPairsByType[PairTypeMatrixIndex(a.type, b.type)] += 1;
            if (game::geometry::IsPointInUndestroyedBase(world, a.position, 1.0F) ||
                game::geometry::IsPointInUndestroyedBase(world, b.position, 1.0F)) {
                gEnemyRuntimeWindowStats.frontalPairsBaseSkipped += 1;
                return;
            }
            profiling::ScopedProfile pairScope(profiling::Scope::EnemyFrontalPairNarrowphase, true);
            gEnemyRuntimeStats.frontalPairsDistanceChecks += 1;
            const float centerDistSq = DistanceSq(a.position, b.position);
            const float killDistSq =
                GameplayConstants::kEnemyMutualKillDistanceUnits * GameplayConstants::kEnemyMutualKillDistanceUnits;
            if (centerDistSq <= killDistSq) {
                EnterUncoupleMode(world.enemies, j, i, UncoupleReason::FrontalCollision);
                EnterUncoupleMode(world.enemies, i, j, UncoupleReason::FrontalCollision);
            }
        });
    }
}

void ResolveEnemyCollisionsSinglePass(
    WorldState& world,
    const std::vector<Vec2f>& frameStartPositions,
    game::spatial::SweepPruneBroadPhase& broadPhase,
    const std::vector<std::uint8_t>& includeMask,
    const std::vector<std::uint8_t>& reenteredFullTierMask) {
    (void)reenteredFullTierMask;
    profiling::ScopedProfile scope(profiling::Scope::EnemyFrontalCollisions, true);
    constexpr float kSharedBroadRadiusUnits = 1.0F;  // r + 0.5 with enemy r=0.5
    const float killDist =
        GameplayConstants::kEnemyMutualKillDistanceUnits;
    const float killDistSq = killDist * killDist;
    const float separationDist =
        GameplayConstants::kEnemyPreferredSeparationUnits;
    const float separationDistSq = separationDist * separationDist;
    {
        const float cellSize = GameplayConstants::kMazeCellSizeUnits;
        auto worldToCellX = [&](float x) {
            const int cx = static_cast<int>(std::floor(x / cellSize));
            return std::max(0, std::min(world.maze.widthCells - 1, cx));
        };
        auto worldToCellY = [&](float y) {
            const int cy = static_cast<int>(std::floor(y / cellSize));
            return std::max(0, std::min(world.maze.heightCells - 1, cy));
        };
        std::uint64_t included = 0;
        std::uint64_t crossedCell = 0;
        for (std::size_t i = 0; i < world.enemies.size() && i < includeMask.size(); ++i) {
            if (includeMask[i] == 0U) {
                continue;
            }
            const EnemyTank& enemy = world.enemies[i];
            if (!enemy.alive) {
                continue;
            }
            included += 1;
            const Vec2f& start = frameStartPositions[i];
            if (worldToCellX(start.x) != worldToCellX(enemy.position.x) ||
                worldToCellY(start.y) != worldToCellY(enemy.position.y)) {
                crossedCell += 1;
            }
        }
        gEnemyRuntimeWindowStats.frontalGridCandidates += included;
        gEnemyRuntimeWindowStats.separationGridCandidates += included;
        gEnemyRuntimeWindowStats.frontalGridCellTransitions += crossedCell;
        gEnemyRuntimeWindowStats.frontalGridInsertEstimate += included + crossedCell;
    }
    {
        profiling::ScopedProfile buildScope(profiling::Scope::EnemyFrontalGridBuild, true);
        broadPhase.BeginFrame(static_cast<int>(world.enemies.size()));
        for (int i = 0; i < static_cast<int>(world.enemies.size()); ++i) {
            const bool active = i < static_cast<int>(includeMask.size()) &&
                includeMask[static_cast<std::size_t>(i)] != 0U &&
                world.enemies[static_cast<std::size_t>(i)].alive;
            const Vec2f& start = frameStartPositions[static_cast<std::size_t>(i)];
            const Vec2f& end = world.enemies[static_cast<std::size_t>(i)].position;
            broadPhase.UpdateEntity(
                i,
                start,
                end,
                kSharedBroadRadiusUnits,
                active);
        }
    }
    {
        profiling::ScopedProfile pairTraverseScope(profiling::Scope::EnemyFrontalPairTraverse, true);
        broadPhase.ForEachCandidatePair([&](int i, int j) {
            EnemyTank& a = world.enemies[static_cast<std::size_t>(i)];
            EnemyTank& b = world.enemies[static_cast<std::size_t>(j)];
            if (!a.alive || !b.alive) {
                return;
            }

            const bool inBase =
                game::geometry::IsPointInUndestroyedBase(world, a.position, 1.0F) ||
                game::geometry::IsPointInUndestroyedBase(world, b.position, 1.0F);

            gEnemyRuntimeStats.frontalPairsVisited += 1;
            gEnemyRuntimeWindowStats.frontalPairsByType[PairTypeMatrixIndex(a.type, b.type)] += 1;
            if (inBase) {
                gEnemyRuntimeWindowStats.frontalPairsBaseSkipped += 1;
            } else {
                profiling::ScopedProfile frontalPairScope(profiling::Scope::EnemyFrontalPairNarrowphase, true);
                gEnemyRuntimeStats.frontalPairsDistanceChecks += 1;
                const float centerDistSq = DistanceSq(a.position, b.position);
                if (centerDistSq <= killDistSq) {
                    EnterUncoupleMode(world.enemies, j, i, UncoupleReason::FrontalCollision);
                    EnterUncoupleMode(world.enemies, i, j, UncoupleReason::FrontalCollision);
                }
            }

            if (!a.alive || !b.alive) {
                return;
            }

            gEnemyRuntimeStats.separationPairsVisited += 1;
            gEnemyRuntimeWindowStats.separationPairsByType[PairTypeMatrixIndex(a.type, b.type)] += 1;
            if (inBase) {
                gEnemyRuntimeWindowStats.separationPairsBaseSkipped += 1;
                return;
            }

            profiling::ScopedProfile separationPairScope(profiling::Scope::EnemySeparationPairResolve, true);
            const float distSq = DistanceSq(a.position, b.position);
            if (distSq <= killDistSq) {
                EnterUncoupleMode(world.enemies, j, i, UncoupleReason::SeparationProximity);
                EnterUncoupleMode(world.enemies, i, j, UncoupleReason::SeparationProximity);
            }
            if (distSq >= separationDistSq) {
                return;
            }
            if (!ShouldEnterSeparationUncouple(a, b, distSq)) {
                return;
            }

            gEnemyRuntimeStats.separationPairsResolved += 1;
            EnterUncoupleMode(world.enemies, j, i, UncoupleReason::SeparationProximity);
            EnterUncoupleMode(world.enemies, i, j, UncoupleReason::SeparationProximity);
        });
    }
    const game::spatial::SweepPruneBroadPhase::FrameStats& sapStats = broadPhase.GetFrameStats();
    gEnemyRuntimeWindowStats.sapUpdateCalls += sapStats.updateCalls;
    gEnemyRuntimeWindowStats.sapActiveItems += sapStats.activeItems;
    gEnemyRuntimeWindowStats.sapCandidatePairs += sapStats.candidatePairs;
    gEnemyRuntimeWindowStats.sapXRepairs += sapStats.xLocalRepairs;
    gEnemyRuntimeWindowStats.sapYRepairs += sapStats.yLocalRepairs;
}

struct EnemyPerception {
    Vec2f toPlayer{};
    Vec2f toPlayerNormalized{};
    float distanceToPlayerSq = 0.0F;
    float distanceToPlayer = 0.0F;
    bool playerObscured = false;
    bool assassinHasLineOfSight = false;
};

EnemyPerception RunPerceptionPhase(
    GameState& state,
    EnemyTank& enemy,
    float deltaSeconds,
    bool playerInvisible,
    Random& random) {
    EnemyPerception perception{};
    if (enemy.selfAwarenessIntervalSeconds <= 0.0F) {
        enemy.selfAwarenessIntervalSeconds = (enemy.type == EnemyType::Drone)
            ? random.NextFloat(6.0F, 12.0F)
            : random.NextFloat(4.0F, 8.0F);
        enemy.selfAwarenessTimerSeconds = enemy.selfAwarenessIntervalSeconds;
    }
    enemy.selfAwarenessTimerSeconds -= deltaSeconds;
    if (enemy.selfAwarenessTimerSeconds <= 0.0F) {
        enemy.selfAwarenessTimerSeconds = enemy.selfAwarenessIntervalSeconds;
        if (enemy.type == EnemyType::Drone) {
            const float nearestBaseDist = NearestBaseDistance(state.world, enemy.position);
            if (nearestBaseDist >= 36.0F) {
                const Vec2f nearestBase = NearestBasePosition(state.world, enemy.position);
                const Vec2f toBase{
                    .x = nearestBase.x - enemy.position.x,
                    .y = nearestBase.y - enemy.position.y,
                };
                const float headingToBase = std::atan2(toBase.x, -toBase.y);
                const float relativeBearing = AngleDistance(enemy.headingRadians, headingToBase);
                if (relativeBearing >= kDroneBaseBearingThresholdRadians) {
                    enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
                    EnterDroneWatchMode(state.world, enemy, random);
                }
            }
        }
    }
    enemy.aiModeElapsedSeconds += deltaSeconds;
    perception.toPlayer = Vec2f{
        .x = state.world.player.position.x - enemy.position.x,
        .y = state.world.player.position.y - enemy.position.y,
    };
    perception.toPlayerNormalized = NormalizeOrZero(perception.toPlayer);
    perception.distanceToPlayerSq = DistanceSq(enemy.position, state.world.player.position);
    perception.distanceToPlayer = std::sqrt(perception.distanceToPlayerSq);
    const bool playerInAggroRange =
        !playerInvisible &&
        perception.distanceToPlayerSq <=
            (GameplayConstants::kEnemyAggroRangeUnits * GameplayConstants::kEnemyAggroRangeUnits);
    perception.playerObscured =
        playerInvisible ||
        game::geometry::IsSegmentObscuredByWall(state.world, enemy.position, state.world.player.position);
    perception.assassinHasLineOfSight =
        enemy.type == EnemyType::Assassin && playerInAggroRange && !perception.playerObscured;
    return perception;
}

void RunFiringPhase(
    GameState& state,
    EnemyTank& enemy,
    const EnemyPerception& perception,
    const GameplayView& view,
    float deltaSeconds) {
    enemy.fireCooldownSeconds -= deltaSeconds;
    const bool enemyVisibleInViewport = IsInPlayerViewport(enemy.position, state, view);
    bool canFireTypeSpecific = true;
    if (enemy.type == EnemyType::Torpedo) {
        canFireTypeSpecific = PlayerAheadForTorpedo(enemy, perception.toPlayerNormalized);
    }
    if (state.world.player.alive &&
        enemy.fireCooldownSeconds <= 0.0F &&
        enemyVisibleInViewport &&
        !perception.playerObscured &&
        canFireTypeSpecific &&
        perception.distanceToPlayerSq <
            (GameplayConstants::kEnemyFireRangeUnits * GameplayConstants::kEnemyFireRangeUnits)) {
        const float headingToPlayer = std::atan2(perception.toPlayer.x, -perception.toPlayer.y);
        const float quantizedHeadingToPlayer = QuantizeToEightDirections(headingToPlayer);
        SpawnProjectile(
            state,
            ProjectileOwner::Enemy,
            enemy.position,
            quantizedHeadingToPlayer,
            GameplayConstants::kEnemyProjectileSpeed);
        enemy.fireCooldownSeconds = EnemyFireInterval(enemy.type);
    }
}

profiling::Scope EnemyTypeProfileScope(EnemyType type) {
    switch (type) {
    case EnemyType::Drone:
        return profiling::Scope::EnemyTypeDroneUpdate;
    case EnemyType::Torpedo:
        return profiling::Scope::EnemyTypeTorpedoUpdate;
    case EnemyType::Hunter:
        return profiling::Scope::EnemyTypeHunterUpdate;
    case EnemyType::Assassin:
        return profiling::Scope::EnemyTypeAssassinUpdate;
    }
    return profiling::Scope::EnemyTypeDroneUpdate;
}

void AdvanceCheapTierTimers(
    GameState& state,
    EnemyTank& enemy,
    const EnemyPerception& perception,
    float deltaSeconds,
    bool playerInvisible,
    const GameplayView& view) {
    enemy.aiModeElapsedSeconds += deltaSeconds;
    enemy.selfAwarenessTimerSeconds -= deltaSeconds;
    if (enemy.selfAwarenessTimerSeconds <= 0.0F) {
        if (enemy.selfAwarenessIntervalSeconds <= 0.0F) {
            enemy.selfAwarenessIntervalSeconds = (enemy.type == EnemyType::Drone) ? 9.0F : 6.0F;
        }
        enemy.selfAwarenessTimerSeconds += enemy.selfAwarenessIntervalSeconds;
    }

    if (enemy.type == EnemyType::Torpedo) {
        enemy.torpedoPlayerDetectTimerSeconds -= deltaSeconds;
        if (enemy.torpedoPlayerDetectTimerSeconds <= 0.0F) {
            enemy.torpedoPlayerDetectTimerSeconds = kOffscreenTorpedoDetectIntervalSeconds;
            enemy.torpedoPlayerDetected =
                !playerInvisible &&
                perception.distanceToPlayer <= GameplayConstants::kTorpedoDetectRangeUnits &&
                !game::geometry::IsSegmentObscuredByWall(state.world, enemy.position, state.world.player.position);
            enemy.torpedoLastKnownPlayerHeadingRadians =
                std::atan2(perception.toPlayer.x, -perception.toPlayer.y);
        }
    }

    // Keep weapon cadence realistic while cheap; this prevents on-screen burst anomalies.
    enemy.fireCooldownSeconds = std::max(0.0F, enemy.fireCooldownSeconds - deltaSeconds);
    if (enemy.aiMode == EnemyAiMode::Uncouple) {
        enemy.aiStateTimerSeconds = std::max(0.0F, enemy.aiStateTimerSeconds - deltaSeconds);
        if (enemy.aiStateTimerSeconds <= 0.0F) {
            RestoreFromUncoupleMode(enemy);
        }
    }
    (void)view;
}

void BuildOffscreenSegment(WorldState& world, EnemyTank& enemy, float segmentLengthUnits, Random& random) {
    profiling::ScopedProfile buildScope(profiling::Scope::EnemyCheapSegmentBuild, true);
    const float forwardHeading = QuantizeToEightDirections(enemy.headingRadians);
    const float leftHeading = QuantizeToEightDirections(forwardHeading - kEightDirectionStep);
    const float rightHeading = QuantizeToEightDirections(forwardHeading + kEightDirectionStep);
    struct Candidate {
        float heading;
        float clearDistance;
    };
    const std::array<Candidate, 3> candidates{{
        {.heading = forwardHeading,
            .clearDistance = FreeDistanceAheadWallsOnly(
                world,
                enemy.position,
                forwardHeading,
                kSegmentBuildProbeMaxUnits,
                GameplayConstants::kEnemyWallAvoidanceRadiusUnits)},
        {.heading = leftHeading,
            .clearDistance = FreeDistanceAheadWallsOnly(
                world,
                enemy.position,
                leftHeading,
                kSegmentBuildProbeMaxUnits,
                GameplayConstants::kEnemyWallAvoidanceRadiusUnits)},
        {.heading = rightHeading,
            .clearDistance = FreeDistanceAheadWallsOnly(
                world,
                enemy.position,
                rightHeading,
                kSegmentBuildProbeMaxUnits,
                GameplayConstants::kEnemyWallAvoidanceRadiusUnits)},
    }};
    float bestClear = -1.0F;
    for (const Candidate& candidate : candidates) {
        bestClear = std::max(bestClear, candidate.clearDistance);
    }
    std::array<int, 3> bestIndices{};
    int bestCount = 0;
    constexpr float kTieEpsilon = 0.001F;
    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
        if (std::fabs(candidates[static_cast<std::size_t>(i)].clearDistance - bestClear) <= kTieEpsilon) {
            bestIndices[static_cast<std::size_t>(bestCount)] = i;
            ++bestCount;
        }
    }
    const int chosenIndex =
        bestIndices[static_cast<std::size_t>(random.NextInt(0, std::max(0, bestCount - 1)))];
    const Candidate& chosen = candidates[static_cast<std::size_t>(chosenIndex)];
    const int typeIdx = EnemyTypeTelemetryIndex(enemy.type);
    const float maxSegmentLength =
        std::min(segmentLengthUnits, chosen.clearDistance - kSegmentBuildSafetyReduceUnits);
    if (maxSegmentLength < kSegmentBuildMinLengthUnits) {
        if (enemy.type == EnemyType::Torpedo) {
            enemy.headingRadians = QuantizeToEightDirections(enemy.headingRadians - kEightDirectionStep);
        } else if (enemy.type == EnemyType::Drone) {
            const int randomStep = random.NextInt(0, 7);
            enemy.headingRadians =
                NormalizeAngle(static_cast<float>(randomStep) * kEightDirectionStep);
        }
        enemy.offscreenSegmentActive = false;
        enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        gEnemyRuntimeWindowStats.segmentBuildFailsByType[static_cast<std::size_t>(typeIdx)] += 1;
        return;
    }
    const float targetDistance = random.NextFloat(kSegmentBuildMinLengthUnits, maxSegmentLength);
    const Vec2f dir = DirectionFromHeading(chosen.heading);
    enemy.offscreenCachedHeadingRadians = chosen.heading;
    enemy.offscreenSegmentEnd = Vec2f{
        .x = enemy.position.x + dir.x * targetDistance,
        .y = enemy.position.y + dir.y * targetDistance,
    };
    enemy.offscreenSegmentActive = true;
    gEnemyRuntimeWindowStats.segmentsBuiltByType[static_cast<std::size_t>(typeIdx)] += 1;
    gEnemyRuntimeWindowStats.segmentLengthSumByType[static_cast<std::size_t>(typeIdx)] += targetDistance;
}

bool BuildSegmentExitPointFromCell(
    const game::navigation::CellCoordCache& cellCache,
    const game::navigation::MazeCellCoord& cell,
    const Vec2f& from,
    const Vec2f& direction,
    float outOfCellUnits,
    Vec2f& outTarget) {
    constexpr float kEpsilon = 0.0001F;
    const float cellSize = static_cast<float>(cellCache.CellSizeUnits());
    const float cellMinX = static_cast<float>(cell.x) * cellSize;
    const float cellMinY = static_cast<float>(cell.y) * cellSize;
    const float cellMaxX = cellMinX + cellSize;
    const float cellMaxY = cellMinY + cellSize;

    float exitDistance = std::numeric_limits<float>::infinity();
    if (direction.x > kEpsilon) {
        exitDistance = std::min(exitDistance, (cellMaxX - from.x) / direction.x);
    } else if (direction.x < -kEpsilon) {
        exitDistance = std::min(exitDistance, (cellMinX - from.x) / direction.x);
    }
    if (direction.y > kEpsilon) {
        exitDistance = std::min(exitDistance, (cellMaxY - from.y) / direction.y);
    } else if (direction.y < -kEpsilon) {
        exitDistance = std::min(exitDistance, (cellMinY - from.y) / direction.y);
    }
    if (!std::isfinite(exitDistance) || exitDistance <= kEpsilon) {
        return false;
    }

    const float distanceToTarget = exitDistance + outOfCellUnits;
    outTarget = Vec2f{
        .x = from.x + direction.x * distanceToTarget,
        .y = from.y + direction.y * distanceToTarget,
    };
    return true;
}

bool BuildAssassinCheapFlowSegment(
    WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    const game::navigation::PlayerFlowField& flowField,
    EnemyTank& enemy,
    bool& outNeedsInitialFlowBuild) {
    outNeedsInitialFlowBuild = false;
    if (!flowField.HasBuild()) {
        outNeedsInitialFlowBuild = true;
        gEnemyRuntimeWindowStats.navFlowMisses += 1;
        return false;
    }

    const game::navigation::MazeCellCoord enemyCell = cellCache.WorldToCell(enemy.position);
    const int enemyCellHash = cellCache.CellHash(enemyCell.x, enemyCell.y);
    const int nextCellHash = flowField.NextCellHash(enemyCellHash);
    if (nextCellHash < 0 || nextCellHash == enemyCellHash || cellCache.WidthCells() <= 0) {
        gEnemyRuntimeWindowStats.navFlowMisses += 1;
        return false;
    }

    const int nextCellX = nextCellHash % cellCache.WidthCells();
    const int nextCellY = nextCellHash / cellCache.WidthCells();
    const int flowDx = nextCellX - enemyCell.x;
    const int flowDy = nextCellY - enemyCell.y;
    if (std::abs(flowDx) + std::abs(flowDy) != 1) {
        gEnemyRuntimeWindowStats.navFlowMisses += 1;
        return false;
    }

    const float flowHeading = QuantizeToEightDirections(
        std::atan2(static_cast<float>(flowDx), -static_cast<float>(flowDy)));
    const Vec2f flowDirection = DirectionFromHeading(flowHeading);
    float segmentHeading = flowHeading;
    Vec2f segmentTarget{};

    const bool hasPreviousFlow = enemy.expectedPathCellHash >= 0;
    if (hasPreviousFlow) {
        const float previousFlowHeading = enemy.cachedFlowHeadingRadians;
        const float delta = SignedAngleDelta(previousFlowHeading, flowHeading);
        const float absDelta = std::fabs(delta);
        constexpr float kTurnEpsilon = 0.001F;
        const bool sameDirection = absDelta <= kTurnEpsilon;
        const bool perpendicularTurn = std::fabs(absDelta - (kPi * 0.5F)) <= kTurnEpsilon;

        if (perpendicularTurn) {
            const float turnSign = (delta >= 0.0F) ? 1.0F : -1.0F;
            segmentHeading = QuantizeToEightDirections(previousFlowHeading + turnSign * kEightDirectionStep);
            const Vec2f diagonalDirection = DirectionFromHeading(segmentHeading);
            const Vec2f cellCenter = cellCache.CellCenter(enemyCell.x, enemyCell.y);
            const Vec2f destinationEdgeMidpoint{
                .x = cellCenter.x + flowDirection.x * (static_cast<float>(cellCache.CellSizeUnits()) * 0.5F),
                .y = cellCenter.y + flowDirection.y * (static_cast<float>(cellCache.CellSizeUnits()) * 0.5F),
            };
            segmentTarget = Vec2f{
                .x = destinationEdgeMidpoint.x + diagonalDirection.x * kAssassinCheapSegmentOutOfCellUnits,
                .y = destinationEdgeMidpoint.y + diagonalDirection.y * kAssassinCheapSegmentOutOfCellUnits,
            };

            const Vec2f toTarget{
                .x = segmentTarget.x - enemy.position.x,
                .y = segmentTarget.y - enemy.position.y,
            };
            const Vec2f targetDir = NormalizeOrZero(toTarget);
            if (targetDir.x == 0.0F || targetDir.y == 0.0F ||
                targetDir.x * diagonalDirection.x + targetDir.y * diagonalDirection.y < 0.95F) {
                if (!BuildSegmentExitPointFromCell(
                        cellCache,
                        enemyCell,
                        enemy.position,
                        diagonalDirection,
                        kAssassinCheapSegmentOutOfCellUnits,
                        segmentTarget)) {
                    gEnemyRuntimeWindowStats.navFlowMisses += 1;
                    return false;
                }
            }
        } else {
            segmentHeading = flowHeading;
            if (!BuildSegmentExitPointFromCell(
                    cellCache,
                    enemyCell,
                    enemy.position,
                    flowDirection,
                    kAssassinCheapSegmentOutOfCellUnits,
                    segmentTarget)) {
                gEnemyRuntimeWindowStats.navFlowMisses += 1;
                return false;
            }
            (void)sameDirection;
        }
    } else {
        if (!BuildSegmentExitPointFromCell(
                cellCache,
                enemyCell,
                enemy.position,
                flowDirection,
                kAssassinCheapSegmentOutOfCellUnits,
                segmentTarget)) {
            gEnemyRuntimeWindowStats.navFlowMisses += 1;
            return false;
        }
    }

    if (SegmentIntersectsWall(
            world,
            enemy.position,
            segmentTarget,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits)) {
        gEnemyRuntimeWindowStats.navFlowMisses += 1;
        return false;
    }

    const int typeIdx = EnemyTypeTelemetryIndex(enemy.type);
    enemy.offscreenCachedHeadingRadians = segmentHeading;
    enemy.offscreenSegmentEnd = segmentTarget;
    enemy.offscreenSegmentActive = true;
    enemy.expectedPathCellHash = nextCellHash;
    enemy.cachedFlowFromCellHash = enemyCellHash;
    enemy.cachedFlowHeadingRadians = flowHeading;
    gEnemyRuntimeWindowStats.navFlowHeadingSelections += 1;
    gEnemyRuntimeWindowStats.segmentsBuiltByType[static_cast<std::size_t>(typeIdx)] += 1;
    gEnemyRuntimeWindowStats.segmentLengthSumByType[static_cast<std::size_t>(typeIdx)] +=
        Distance(enemy.position, segmentTarget);
    return true;
}

void ApplyCheapTierMovement(
    GameState& state,
    const game::navigation::CellCoordCache& cellCache,
    const game::navigation::PlayerFlowField& flowField,
    EnemyTank& enemy,
    int enemyIndex,
    float deltaSeconds,
    float speed,
    Random& random,
    bool& outNeedsInitialFlowBuild) {
    outNeedsInitialFlowBuild = false;
    float segmentLength = kOffscreenSegmentLengthUnits;
    if (enemy.type == EnemyType::Torpedo) {
        segmentLength = kOffscreenTorpedoSegmentLengthUnits;
    }

    if (enemy.type == EnemyType::Assassin && kUseFlowFieldPathGuidance) {
        const game::navigation::MazeCellCoord enemyCell = cellCache.WorldToCell(enemy.position);
        const int enemyCellHash = cellCache.CellHash(enemyCell.x, enemyCell.y);
        const bool enteredNewCell = enemy.cachedFlowFromCellHash != enemyCellHash;
        if (enteredNewCell) {
            game::spatial::EnemyCellOccupancy& occ = state.world.navigationCache.enemyCellOccupancy;
            if (occ.HasOtherInCell(enemyCell.x, enemyCell.y, enemyIndex)) {
                enemy.cheapTierCrowdedSlowMode = true;
            } else {
                enemy.cheapTierCrowdedSlowMode = false;
            }
        }
        const bool reachedSegmentEnd =
            !enemy.offscreenSegmentActive ||
            DistanceSq(enemy.position, enemy.offscreenSegmentEnd) <= 0.04F;
        if (enteredNewCell || reachedSegmentEnd) {
            if (!BuildAssassinCheapFlowSegment(
                    state.world,
                    cellCache,
                    flowField,
                    enemy,
                    outNeedsInitialFlowBuild)) {
                enemy.offscreenSegmentActive = false;
                enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
            }
        }
    } else {
        const bool reachedSegmentEnd =
            !enemy.offscreenSegmentActive ||
            DistanceSq(enemy.position, enemy.offscreenSegmentEnd) <= 0.04F;
        if (reachedSegmentEnd) {
            // Cheap-tier policy: decide the next segment only at segment endpoints.
            BuildOffscreenSegment(state.world, enemy, segmentLength, random);
        }
    }

    if (!enemy.offscreenSegmentActive || std::fabs(speed) <= 0.0001F) {
        enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        return;
    }

    float effectiveSpeed = speed;
    if (enemy.type == EnemyType::Assassin && enemy.cheapTierCrowdedSlowMode) {
        effectiveSpeed *= 0.5F;
    }
    const Vec2f dir = DirectionFromHeading(enemy.offscreenCachedHeadingRadians);
    const float maxStep = std::max(0.0F, effectiveSpeed * deltaSeconds);
    const float remaining = Distance(enemy.position, enemy.offscreenSegmentEnd);
    const float step = std::min(maxStep, remaining);
    enemy.position = Vec2f{
        .x = enemy.position.x + dir.x * step,
        .y = enemy.position.y + dir.y * step,
    };
    enemy.headingRadians = enemy.offscreenCachedHeadingRadians;
    enemy.velocity = Vec2f{
        .x = dir.x * effectiveSpeed,
        .y = dir.y * effectiveSpeed,
    };

    if (enemy.type == EnemyType::Torpedo && enemy.torpedoMoveMode == TorpedoMoveMode::Move) {
        enemy.torpedoStraightDistanceSinceTurnUnits += step;
    }
    (void)enemyIndex;
}

bool TryGetAssassinFlowHeading(
    const game::navigation::CellCoordCache& cellCache,
    const game::navigation::PlayerFlowField& flowField,
    const EnemyTank& enemy,
    float& outHeadingRadians) {
    if (!flowField.HasBuild()) {
        return false;
    }
    const game::navigation::MazeCellCoord enemyCell = cellCache.WorldToCell(enemy.position);
    const int enemyCellHash = cellCache.CellHash(enemyCell.x, enemyCell.y);
    const int nextCellHash = flowField.NextCellHash(enemyCellHash);
    if (nextCellHash < 0 || nextCellHash == enemyCellHash || cellCache.WidthCells() <= 0) {
        return false;
    }
    const int nextCellX = nextCellHash % cellCache.WidthCells();
    const int nextCellY = nextCellHash / cellCache.WidthCells();
    const int flowDx = nextCellX - enemyCell.x;
    const int flowDy = nextCellY - enemyCell.y;
    if (std::abs(flowDx) + std::abs(flowDy) != 1) {
        return false;
    }
    outHeadingRadians = QuantizeToEightDirections(
        std::atan2(static_cast<float>(flowDx), -static_cast<float>(flowDy)));
    return true;
}

float ComputeUncoupleEscapeScore(
    const WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    const game::navigation::PlayerFlowField& flowField,
    const std::vector<EnemyTank>& enemies,
    int selfIndex) {
    if (selfIndex < 0 || selfIndex >= static_cast<int>(enemies.size())) {
        return -1000.0F;
    }
    const EnemyTank& self = enemies[static_cast<std::size_t>(selfIndex)];
    if (!self.alive || self.aiMode != EnemyAiMode::Uncouple) {
        return -1000.0F;
    }

    float strategicHeading = self.desiredHeadingRadians;
    if (self.type == EnemyType::Assassin) {
        float flowHeading = strategicHeading;
        if (TryGetAssassinFlowHeading(cellCache, flowField, self, flowHeading)) {
            strategicHeading = flowHeading;
        }
    }

    const float clearAhead = game::geometry::FreeDistanceAhead(
        world,
        self.position,
        strategicHeading,
        kUncouplePriorityClearProbeUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
        1.0F);
    const float alignment = std::cos(SignedAngleDelta(self.headingRadians, strategicHeading));

    const float crowdingRangeSq = kUncouplePriorityCrowdingRangeUnits * kUncouplePriorityCrowdingRangeUnits;
    int crowdingCount = 0;
    for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
        if (i == selfIndex) {
            continue;
        }
        const EnemyTank& other = enemies[static_cast<std::size_t>(i)];
        if (!other.alive || other.aiMode != EnemyAiMode::Uncouple) {
            continue;
        }
        if (DistanceSq(self.position, other.position) <= crowdingRangeSq) {
            crowdingCount += 1;
        }
    }

    // Higher score means better downstream release candidate during uncouple.
    return clearAhead * 2.0F + alignment * 0.75F - static_cast<float>(crowdingCount) * 0.5F;
}
}  // namespace

void UpdateEnemySystem(
    GameState& state,
    const GameplayView& view,
    float deltaSeconds,
    Random& random,
    game::navigation::FlowRebuildWorker& flowWorker) {
    profiling::ScopedProfile scope(profiling::Scope::EnemyUpdate, true);
    gEnemyRuntimeStats = EnemyRuntimeStats{};
    const bool playerInvisible = state.menuSettings.invisibility;
    std::vector<Vec2f> frameStartPositions{};
    frameStartPositions.reserve(state.world.enemies.size());
    for (const EnemyTank& enemy : state.world.enemies) {
        frameStartPositions.push_back(enemy.position);
    }

    NavigationRuntimeCache& navigationCache = state.world.navigationCache;
    game::navigation::CellCoordCache& cellCache = navigationCache.cellCoords;
    cellCache.ConfigureFromMaze(state.world.maze);

    game::spatial::EnemyCellOccupancy& occupancy = navigationCache.enemyCellOccupancy;
    const int maxEnemies =
        std::max(static_cast<int>(state.world.enemies.size()),
                 static_cast<int>(GameplayConstants::kMaxAliveEnemies));
    occupancy.Reserve(cellCache.WidthCells(), cellCache.HeightCells(), maxEnemies);
    for (int i = 0; i < static_cast<int>(state.world.enemies.size()); ++i) {
        const EnemyTank& e = state.world.enemies[static_cast<std::size_t>(i)];
        if (!e.alive) {
            occupancy.Remove(i);
            continue;
        }
        const game::navigation::MazeCellCoord c =
            cellCache.WorldToCell(frameStartPositions[static_cast<std::size_t>(i)]);
        occupancy.SetCell(i, c.x, c.y);
    }
    game::navigation::PlayerFlowField& playerFlowField = navigationCache.playerFlowField;

    // Collect a completed background rebuild and swap it into the live field.
    if (flowWorker.inFlight &&
        flowWorker.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        flowWorker.future.get();
        std::swap(navigationCache.playerFlowField, flowWorker.pendingFlowField);
        flowWorker.inFlight = false;
    }

    // Schedule an async rebuild. Skips silently if one is already in flight.
    // The live playerFlowField continues to be used until the rebuild completes.
    auto scheduleFlowRebuild = [&]() {
        if (flowWorker.inFlight) {
            return;
        }
        flowWorker.inFlight = true;
        MazeState mazeCopy = state.world.maze;
        game::navigation::CellCoordCache cacheCopy = cellCache;
        std::vector<EnemyBase> basesCopy = state.world.enemyBases;
        flowWorker.future = std::async(
            std::launch::async,
            [&pending = flowWorker.pendingFlowField,
             m = std::move(mazeCopy),
             c = std::move(cacheCopy),
             b = std::move(basesCopy)]() mutable {
                pending.Rebuild(m, c, b);
            });
    };

    if (kUseFlowFieldPathGuidance) {
        const int previousPlayerCellHash = cellCache.PlayerCellHash();
        const bool playerCrossedCellBorder = cellCache.UpdatePlayerCell(state.world.player.position);
        if (playerCrossedCellBorder) {
            gEnemyRuntimeWindowStats.navPlayerCellChanges += 1;
            if (navigationCache.playerFlowFieldCacheActive) {
                navigationCache.playerFlowFieldAge += 1;
                if (navigationCache.playerFlowFieldAge > kMaxFlowFieldAge) {
                    scheduleFlowRebuild();
                    navigationCache.playerFlowFieldAge = 0;
                    gEnemyRuntimeWindowStats.navFlowRebuilds += 1;
                } else {
                    const int currentPlayerCellHash = cellCache.PlayerCellHash();
                    if (playerFlowField.HasBuild() &&
                        previousPlayerCellHash >= 0 &&
                        currentPlayerCellHash >= 0 &&
                        previousPlayerCellHash != currentPlayerCellHash) {
                        playerFlowField.OverrideNextCellHash(previousPlayerCellHash, currentPlayerCellHash);
                    }
                }
            }
        }
        if (navigationCache.playerFlowFieldSpawnRequestActive) {
            navigationCache.playerFlowFieldSpawnRequestActive = false;
            navigationCache.playerFlowFieldCacheActive = true;
            if (!playerFlowField.HasBuild()) {
                scheduleFlowRebuild();
                navigationCache.playerFlowFieldAge = 0;
                gEnemyRuntimeWindowStats.navFlowRebuilds += 1;
            }
        }
    }

    game::spatial::EnemySpatialGrid rayQueryGrid;
    rayQueryGrid.BuildFromPositions(state.world, &frameStartPositions);
    std::vector<std::uint8_t> reenteredFullTierMask(state.world.enemies.size(), 0U);
    std::vector<float> uncoupleEscapeScores(state.world.enemies.size(), -1000.0F);
    for (int enemyIndex = 0; enemyIndex < static_cast<int>(state.world.enemies.size()); ++enemyIndex) {
        uncoupleEscapeScores[static_cast<std::size_t>(enemyIndex)] =
            ComputeUncoupleEscapeScore(
                state.world,
                cellCache,
                playerFlowField,
                state.world.enemies,
                enemyIndex);
    }

    for (int enemyIndex = 0; enemyIndex < static_cast<int>(state.world.enemies.size()); ++enemyIndex) {
        EnemyTank& enemy = state.world.enemies[static_cast<std::size_t>(enemyIndex)];
        if (!enemy.alive) {
            continue;
        }
        profiling::ScopedProfile enemyTypeScope(EnemyTypeProfileScope(enemy.type), true);

        const EnemySimTier previousTier = enemy.simTier;
        enemy.simTier = DetermineEnemySimTier(enemy, state, view);
        const bool reenteredFullTier =
            previousTier == EnemySimTier::Cheap &&
            enemy.simTier == EnemySimTier::Full;
        if (reenteredFullTier) {
            reenteredFullTierMask[static_cast<std::size_t>(enemyIndex)] = 1U;
            enemy.offscreenSegmentActive = false;
            if (enemy.type == EnemyType::Assassin) {
                enemy.pathWaypointCount = 0;
                enemy.pathWaypointIndex = 0;
                enemy.expectedPathCellHash = -1;
                enemy.cachedFlowFromCellHash = -1;
                enemy.cheapTierCrowdedSlowMode = false;
            }
        }

        if (enemy.simTier == EnemySimTier::Cheap) {
            EnemyPerception cheapPerception{};
            cheapPerception.toPlayer = Vec2f{
                .x = state.world.player.position.x - enemy.position.x,
                .y = state.world.player.position.y - enemy.position.y,
            };
            cheapPerception.toPlayerNormalized = NormalizeOrZero(cheapPerception.toPlayer);
            cheapPerception.distanceToPlayerSq = DistanceSq(enemy.position, state.world.player.position);
            cheapPerception.distanceToPlayer = std::sqrt(cheapPerception.distanceToPlayerSq);
            cheapPerception.playerObscured = true;
            cheapPerception.assassinHasLineOfSight = false;

            AdvanceCheapTierTimers(
                state,
                enemy,
                cheapPerception,
                deltaSeconds,
                playerInvisible,
                view);

            float cheapSpeed = EnemySpeed(enemy.type, enemy.subtype, false, state.menuSettings.levelNumber);
            if (enemy.aiMode == EnemyAiMode::Watch || enemy.aiMode == EnemyAiMode::Rotate) {
                cheapSpeed = 0.0F;
            }
            bool needsInitialFlowBuild = false;
            ApplyCheapTierMovement(
                state,
                cellCache,
                playerFlowField,
                enemy,
                enemyIndex,
                deltaSeconds,
                cheapSpeed,
                random,
                needsInitialFlowBuild);
            if (needsInitialFlowBuild && enemy.type == EnemyType::Assassin) {
                navigationCache.playerFlowFieldCacheActive = true;
                scheduleFlowRebuild();
                navigationCache.playerFlowFieldAge = 0;
                gEnemyRuntimeWindowStats.navFlowRebuilds += 1;
            }
            const game::navigation::MazeCellCoord cheapCell =
                cellCache.WorldToCell(enemy.position);
            occupancy.SetCell(enemyIndex, cheapCell.x, cheapCell.y);
            continue;
        }

        EnemyPerception perception{};
        {
            profiling::ScopedProfile phaseScope(profiling::Scope::EnemyAiPerception, true);
            perception = RunPerceptionPhase(state, enemy, deltaSeconds, playerInvisible, random);
        }

        float movementHeading = QuantizeToEightDirections(enemy.headingRadians);
        float speed = EnemySpeed(enemy.type, enemy.subtype, perception.assassinHasLineOfSight, state.menuSettings.levelNumber);
        bool preserveContinuousHeading = false;
        {
            profiling::ScopedProfile phaseScope(profiling::Scope::EnemyAiDecision, true);
            bool handledByUncouple = false;
            if (enemy.aiMode == EnemyAiMode::Uncouple) {
                enemy.aiStateTimerSeconds = std::max(0.0F, enemy.aiStateTimerSeconds - deltaSeconds);
                if (enemy.aiStateTimerSeconds > 0.0F) {
                    float uncoupleFallbackHeading = enemy.desiredHeadingRadians;
                    if (enemy.type == EnemyType::Assassin) {
                        float flowHeading = uncoupleFallbackHeading;
                        if (TryGetAssassinFlowHeading(cellCache, playerFlowField, enemy, flowHeading)) {
                            uncoupleFallbackHeading = flowHeading;
                        }
                    }
                    movementHeading =
                        SelectUncoupleHeading(
                            state.world,
                            state.world.enemies,
                            enemyIndex,
                            uncoupleFallbackHeading,
                            random);
                    enemy.desiredHeadingRadians = movementHeading;
                    handledByUncouple = true;
                } else {
                    RestoreFromUncoupleMode(enemy);
                }
            }

            if (!handledByUncouple && enemy.type == EnemyType::Drone) {
                if (enemy.aiMode != EnemyAiMode::Watch && enemy.aiMode != EnemyAiMode::Wander) {
                    enemy.aiMode = EnemyAiMode::Wander;
                    enemy.aiModeElapsedSeconds = 0.0F;
                }
                if (enemy.aiMode == EnemyAiMode::Watch) {
                    speed = 0.0F;
                    preserveContinuousHeading = true;
                    movementHeading = NormalizeAngle(
                        enemy.headingRadians +
                        static_cast<float>(enemy.watchRotateDirection) *
                            (kPi * 2.0F / GameplayConstants::kSlowRotateFullTurnSeconds) * deltaSeconds);
                    const float clearDistance = game::geometry::FreeDistanceAhead(
                        state.world,
                        enemy.position,
                        movementHeading,
                        GameplayConstants::kEnemyRequiredClearRunUnits + 0.5F,
                        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                        kEnemyPlanningClearanceScale);
                    if (enemy.aiModeElapsedSeconds >= GameplayConstants::kSlowRotateFullTurnSeconds) {
                        if (enemy.returnToBase) {
                            float returnHeading = movementHeading;
                            if (SelectDroneReturnToBaseHeading(state.world, enemy, random, returnHeading)) {
                                movementHeading = returnHeading;
                                enemy.returnToBase = false;
                                enemy.aiMode = EnemyAiMode::Wander;
                                enemy.aiModeElapsedSeconds = 0.0F;
                            }
                        } else if (clearDistance > GameplayConstants::kEnemyRequiredClearRunUnits) {
                            float escapeHeading = movementHeading;
                            if (SelectDroneWatchEscapeHeading(
                                    state.world,
                                    state.world.enemies,
                                    enemyIndex,
                                    deltaSeconds,
                                    escapeHeading)) {
                                movementHeading = escapeHeading;
                                enemy.aiMode = EnemyAiMode::Wander;
                                enemy.aiModeElapsedSeconds = 0.0F;
                            }
                        }
                    }
                } else {
                    bool shouldWatch = false;
                    movementHeading = SelectScoutHeadingWithFallback(
                        state.world,
                        enemy,
                        false,
                        shouldWatch);
                    if (shouldWatch) {
                        EnterDroneWatchMode(state.world, enemy, random);
                        speed = 0.0F;
                    }
                }
            } else if (!handledByUncouple && enemy.type == EnemyType::Torpedo) {
                enemy.torpedoPlayerDetectTimerSeconds -= deltaSeconds;
                if (enemy.torpedoPlayerDetectTimerSeconds <= 0.0F) {
                    enemy.torpedoPlayerDetectTimerSeconds = kTorpedoPlayerDetectIntervalSeconds;
                    enemy.torpedoPlayerDetected =
                        !playerInvisible &&
                        !perception.playerObscured &&
                        perception.distanceToPlayer <= GameplayConstants::kTorpedoDetectRangeUnits;
                    enemy.torpedoLastKnownPlayerHeadingRadians =
                        std::atan2(perception.toPlayer.x, -perception.toPlayer.y);
                }
                if (enemy.torpedoMoveMode == TorpedoMoveMode::Retreat) {
                    movementHeading = QuantizeToEightDirections(enemy.headingRadians);
                    speed = -std::abs(speed) * kTorpedoRetreatSpeedFactor;
                    const float forwardClear = game::geometry::FreeDistanceAheadWithEnemies(
                        state.world,
                        state.world.enemies,
                        enemyIndex,
                        enemy.position,
                        enemy.headingRadians,
                        kTorpedoNearCollisionCheckDistanceUnits,
                        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                        kEnemyPlanningClearanceScale,
                        &rayQueryGrid);
                    if (enemy.torpedoRetreatMovedUnits >= kTorpedoRetreatExitClearanceUnits &&
                        forwardClear >= kTorpedoRetreatExitClearanceUnits) {
                        speed = 0.0F;
                        EnterTorpedoTargetingMode(enemy);
                    }
                } else if (enemy.torpedoMoveMode == TorpedoMoveMode::Targeting) {
                    speed = 0.0F;
                    enemy.torpedoChosenHeadingRadians = SelectBestLongStraightHeading(state.world, enemy);
                    EnterTorpedoRotateMode(enemy);
                } else if (enemy.torpedoMoveMode == TorpedoMoveMode::Rotate) {
                    speed = 0.0F;
                    preserveContinuousHeading = true;
                    movementHeading = UpdateTorpedoRotateHeading(enemy, deltaSeconds);
                } else {
                    const float straightHeading = QuantizeToEightDirections(enemy.headingRadians);
                    const bool lockHeadingForBaseExit = IsPointInUndestroyedBase(
                        state.world,
                        enemy.position,
                        1.0F);
                    if (lockHeadingForBaseExit || enemy.torpedoMoveDecisionHoldRemainingUnits > 0.0F) {
                        movementHeading = straightHeading;
                        const float nearClear = game::geometry::FreeDistanceAheadWithEnemies(
                            state.world,
                            state.world.enemies,
                            enemyIndex,
                            enemy.position,
                            straightHeading,
                            kTorpedoNearCollisionCheckDistanceUnits,
                            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                            kEnemyPlanningClearanceScale,
                            &rayQueryGrid);
                        if (nearClear < kTorpedoImmediateObstacleDistanceUnits) {
                            enemy.torpedoMoveMode = TorpedoMoveMode::Retreat;
                            enemy.torpedoRetreatMovedUnits = 0.0F;
                            enemy.torpedoMoveDecisionHoldRemainingUnits = 0.0F;
                            movementHeading = straightHeading;
                            speed = -std::abs(speed) * kTorpedoRetreatSpeedFactor;
                        } else if (!lockHeadingForBaseExit &&
                            nearClear < kTorpedoNearCollisionCheckDistanceUnits) {
                            enemy.torpedoMoveDecisionHoldRemainingUnits = 0.0F;
                        } else if (lockHeadingForBaseExit) {
                            // Keep torpedo on its spawn heading until it clears base + margin.
                            enemy.torpedoMoveDecisionHoldRemainingUnits = std::max(
                                enemy.torpedoMoveDecisionHoldRemainingUnits,
                                kTorpedoMoveDecisionHoldDistanceUnits);
                        }
                    } else {
                        bool startRetreat = false;
                        bool decidedStraight = true;
                        movementHeading = SelectTorpedoMoveHeading(
                            state.world,
                            state.world.enemies,
                            enemyIndex,
                            enemy,
                            random,
                            startRetreat,
                            decidedStraight,
                            &rayQueryGrid);
                        if (startRetreat) {
                            enemy.torpedoMoveMode = TorpedoMoveMode::Retreat;
                            enemy.torpedoRetreatMovedUnits = 0.0F;
                            enemy.torpedoMoveDecisionHoldRemainingUnits = 0.0F;
                            movementHeading = straightHeading;
                            speed = -std::abs(speed) * kTorpedoRetreatSpeedFactor;
                        }
                    }
                }
            } else if (!handledByUncouple && enemy.type == EnemyType::Hunter) {
                const bool canChase =
                    !playerInvisible &&
                    !perception.playerObscured &&
                    perception.distanceToPlayer <= GameplayConstants::kHunterDetectRangeUnits;
                if (canChase) {
                    enemy.aiMode = EnemyAiMode::Chase;
                    enemy.aiModeElapsedSeconds = 0.0F;
                } else if (enemy.aiMode == EnemyAiMode::Chase) {
                    enemy.aiMode = EnemyAiMode::Scout;
                    enemy.aiModeElapsedSeconds = 0.0F;
                }

                if (enemy.aiMode == EnemyAiMode::Chase) {
                    if (perception.distanceToPlayer < GameplayConstants::kHunterMinDistanceUnits) {
                        movementHeading = QuantizeToEightDirections(
                            std::atan2(-perception.toPlayer.x, perception.toPlayer.y));
                    } else if (perception.distanceToPlayer > GameplayConstants::kHunterMaxDistanceUnits) {
                        movementHeading = QuantizeToEightDirections(
                            std::atan2(perception.toPlayer.x, -perception.toPlayer.y));
                    } else {
                        speed = 0.0F;
                    }
                } else if (enemy.aiMode == EnemyAiMode::Rotate) {
                    speed = 0.0F;
                    preserveContinuousHeading = true;
                    movementHeading = NormalizeAngle(
                        enemy.headingRadians + (kPi * 2.0F / GameplayConstants::kSlowRotateFullTurnSeconds) * deltaSeconds);
                    const float clearDistance = game::geometry::FreeDistanceAhead(
                        state.world,
                        enemy.position,
                        movementHeading,
                        GameplayConstants::kEnemyRequiredClearRunUnits + 0.5F,
                        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                        kEnemyPlanningClearanceScale);
                    if (clearDistance > GameplayConstants::kEnemyRequiredClearRunUnits) {
                        enemy.aiMode = EnemyAiMode::Scout;
                        enemy.aiModeElapsedSeconds = 0.0F;
                    }
                } else {
                    bool shouldRotate = false;
                    movementHeading = SelectScoutHeadingWithFallback(
                        state.world,
                        enemy,
                        true,
                        shouldRotate);
                    if (shouldRotate) {
                        enemy.aiMode = EnemyAiMode::Rotate;
                        enemy.aiModeElapsedSeconds = 0.0F;
                        speed = 0.0F;
                    } else {
                        enemy.aiMode = EnemyAiMode::Scout;
                    }
                }
            } else if (!handledByUncouple) {
                enemy.aiMode = EnemyAiMode::Pursuit;
                if (!playerInvisible && perception.distanceToPlayer < GameplayConstants::kAssassinMinDistanceUnits) {
                    speed = 0.0F;
                    enemy.pathWaypointCount = 0;
                    enemy.pathWaypointIndex = 0;
                    enemy.expectedPathCellHash = -1;
                    enemy.cachedFlowFromCellHash = -1;
                } else {
                    if (kUseAssassinFlowFieldOnlyNavigation) {
                        bool flowHeadingSelected = false;
                        bool needsInitialFlowBuild = false;
                        if (kUseFlowFieldPathGuidance) {
                            flowHeadingSelected = TrySelectAssassinFlowNextStep(
                                cellCache,
                                playerFlowField,
                                enemy,
                                needsInitialFlowBuild,
                                movementHeading);
                            if (!flowHeadingSelected && needsInitialFlowBuild) {
                                navigationCache.playerFlowFieldCacheActive = true;
                                // Kick off a background rebuild; assassin falls through to
                                // prediction-based heading this frame.
                                scheduleFlowRebuild();
                                navigationCache.playerFlowFieldAge = 0;
                                gEnemyRuntimeWindowStats.navFlowRebuilds += 1;
                            }
                        }

                        enemy.pathWaypointCount = 0;
                        enemy.pathWaypointIndex = 0;

                        if (!flowHeadingSelected) {
                            const Vec2f predicted{
                                .x = state.world.player.position.x +
                                    state.world.player.velocity.x * GameplayConstants::kEnemyAssassinPredictionSeconds,
                                .y = state.world.player.position.y +
                                    state.world.player.velocity.y * GameplayConstants::kEnemyAssassinPredictionSeconds,
                            };
                            movementHeading = QuantizeToEightDirections(
                                std::atan2(predicted.x - enemy.position.x, -(predicted.y - enemy.position.y)));
                        }
                    } else if (kUseAssassinAStarBackupNavigation) {
                        const float obstacleAhead = game::geometry::FreeDistanceAhead(
                            state.world,
                            enemy.position,
                            enemy.headingRadians,
                            2.0F,
                            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                            kEnemyPlanningClearanceScale);
                        const bool needRepathObstacle = obstacleAhead < 2.0F;
                        const bool needRepathEmpty = enemy.pathWaypointCount <= 0 || enemy.pathWaypointIndex >= enemy.pathWaypointCount;
                        if (needRepathObstacle || needRepathEmpty) {
                            if (playerInvisible) {
                                BuildAssassinPathToFarRandomTarget(state, cellCache, enemy, enemyIndex, random);
                            } else {
                                BuildAssassinPath(state, cellCache, enemy, enemyIndex);
                            }
                        }

                        if (enemy.pathWaypointCount > 0 &&
                            enemy.pathWaypointIndex < enemy.pathWaypointCount) {
                            const Vec2f waypoint = enemy.pathWaypoints[static_cast<std::size_t>(enemy.pathWaypointIndex)];
                            const Vec2f toWaypoint{
                                .x = waypoint.x - enemy.position.x,
                                .y = waypoint.y - enemy.position.y,
                            };
                            if (DistanceSq(waypoint, enemy.position) <= 0.36F) {
                                enemy.pathWaypointIndex += 1;
                                if (playerInvisible) {
                                    BuildAssassinPathToFarRandomTarget(state, cellCache, enemy, enemyIndex, random);
                                } else if (enemy.pathWaypointIndex < enemy.pathWaypointCount) {
                                    BuildAssassinPath(state, cellCache, enemy, enemyIndex);
                                }
                            }
                            const Vec2f stepDir = NormalizeOrZero(toWaypoint);
                            if (stepDir.x != 0.0F || stepDir.y != 0.0F) {
                                movementHeading = QuantizeToEightDirections(std::atan2(stepDir.x, -stepDir.y));
                            }
                        } else {
                            const Vec2f predicted{
                                .x = state.world.player.position.x +
                                    state.world.player.velocity.x * GameplayConstants::kEnemyAssassinPredictionSeconds,
                                .y = state.world.player.position.y +
                                    state.world.player.velocity.y * GameplayConstants::kEnemyAssassinPredictionSeconds,
                            };
                            movementHeading = QuantizeToEightDirections(
                                std::atan2(predicted.x - enemy.position.x, -(predicted.y - enemy.position.y)));
                        }
                    } else {
                        const Vec2f predicted{
                            .x = state.world.player.position.x +
                                state.world.player.velocity.x * GameplayConstants::kEnemyAssassinPredictionSeconds,
                            .y = state.world.player.position.y +
                                state.world.player.velocity.y * GameplayConstants::kEnemyAssassinPredictionSeconds,
                        };
                        movementHeading = QuantizeToEightDirections(
                            std::atan2(predicted.x - enemy.position.x, -(predicted.y - enemy.position.y)));
                    }
                }
            }
        }

        const Vec2f previousPosition = enemy.position;
        {
            profiling::ScopedProfile phaseScope(profiling::Scope::EnemyAiMovement, true);
            if (preserveContinuousHeading) {
                movementHeading = NormalizeAngle(movementHeading);
            } else {
                movementHeading = QuantizeToEightDirections(movementHeading);
            }
            const Vec2f snappedDirection = DirectionFromHeading(movementHeading);
            Vec2f candidatePosition{
                .x = enemy.position.x + snappedDirection.x * speed * deltaSeconds,
                .y = enemy.position.y + snappedDirection.y * speed * deltaSeconds,
            };

            // Keep enemies from overlapping: turn first, stop second.
            {
                profiling::ScopedProfile sepScope(profiling::Scope::EnemyMovementSeparationProbe, true);
                constexpr float sepSq =
                    GameplayConstants::kEnemyPreferredSeparationUnits * GameplayConstants::kEnemyPreferredSeparationUnits;
                float minDistSqToOthers = std::numeric_limits<float>::infinity();
                float currentMinDistSqToOthers = std::numeric_limits<float>::infinity();
                const float selfUncoupleScore = enemyIndex < static_cast<int>(uncoupleEscapeScores.size())
                    ? uncoupleEscapeScores[static_cast<std::size_t>(enemyIndex)]
                    : -1000.0F;
                for (int i = 0; i < static_cast<int>(state.world.enemies.size()); ++i) {
                    if (i == enemyIndex) {
                        continue;
                    }
                    const EnemyTank& other = state.world.enemies[static_cast<std::size_t>(i)];
                    if (!other.alive) {
                        continue;
                    }
                    if (enemy.aiMode == EnemyAiMode::Uncouple &&
                        other.aiMode == EnemyAiMode::Uncouple &&
                        i < static_cast<int>(uncoupleEscapeScores.size())) {
                        const float otherScore = uncoupleEscapeScores[static_cast<std::size_t>(i)];
                        const bool otherYieldsToSelf =
                            (selfUncoupleScore > otherScore + kUncouplePriorityEpsilon) ||
                            (std::fabs(selfUncoupleScore - otherScore) <= kUncouplePriorityEpsilon &&
                                enemyIndex < i);
                        if (otherYieldsToSelf) {
                            continue;
                        }
                    }
                    currentMinDistSqToOthers =
                        std::min(currentMinDistSqToOthers, DistanceSq(enemy.position, other.position));
                    minDistSqToOthers =
                        std::min(minDistSqToOthers, DistanceSq(candidatePosition, other.position));
                }
                constexpr float kSeparationProgressEpsilonSq = 0.01F;
                const bool makingSeparationProgress =
                    minDistSqToOthers > currentMinDistSqToOthers + kSeparationProgressEpsilonSq;
                if (std::fabs(speed) > 0.0F &&
                    minDistSqToOthers < sepSq &&
                    !makingSeparationProgress) {
                    float turnHeading = movementHeading;
                    Vec2f turnCandidate = candidatePosition;
                    if (TrySeparationTurn(state.world, state.world.enemies, enemyIndex, speed, deltaSeconds, turnHeading, turnCandidate)) {
                        movementHeading = turnHeading;
                        candidatePosition = turnCandidate;
                    } else {
                        speed = 0.0F;
                        candidatePosition = enemy.position;
                        enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
                        if (enemy.type == EnemyType::Drone) {
                            EnterDroneWatchMode(state.world, enemy, random);
                        }
                    }
                }
            }

            {
                profiling::ScopedProfile overlapScope(profiling::Scope::EnemyMovementOverlapCheck, true);
                bool blocked = false;
                if (std::fabs(speed) > 0.0F) {
                    {
                        profiling::ScopedProfile scope(profiling::Scope::EnemyMovementOverlapIsBlocked, true);
                        blocked = IsMovementBlockedByEnemies(
                            state.world.enemies,
                            frameStartPositions,
                            enemyIndex,
                            previousPosition,
                            candidatePosition,
                            GameplayConstants::kEnemyPreferredSeparationUnits,
                            &uncoupleEscapeScores);
                    }
                }
                if (blocked) {
                    float turnHeading = movementHeading;
                    Vec2f turnCandidate = candidatePosition;
                    bool foundTurn = false;
                    {
                        profiling::ScopedProfile scope(profiling::Scope::EnemyMovementOverlapSeparationTurn, true);
                        foundTurn = TrySeparationTurn(
                            state.world,
                            state.world.enemies,
                            enemyIndex,
                            speed,
                            deltaSeconds,
                            turnHeading,
                            turnCandidate);
                    }
                    bool turnValid = false;
                    if (foundTurn) {
                        profiling::ScopedProfile scope(profiling::Scope::EnemyMovementOverlapTurnValid, true);
                        turnValid = !IsMovementBlockedByEnemies(
                            state.world.enemies,
                            frameStartPositions,
                            enemyIndex,
                            previousPosition,
                            turnCandidate,
                            GameplayConstants::kEnemyPreferredSeparationUnits,
                            &uncoupleEscapeScores);
                    }
                    if (turnValid) {
                        movementHeading = turnHeading;
                        candidatePosition = turnCandidate;
                    } else {
                        enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
                        candidatePosition = enemy.position;
                        if (enemy.type == EnemyType::Drone) {
                            EnterDroneWatchMode(state.world, enemy, random);
                        }
                    }
                }
            }

            {
                profiling::ScopedProfile wallScope(profiling::Scope::EnemyMovementWallCheck, true);
                const bool moving = std::fabs(speed) > 0.001F;
                const bool segmentWallHit = moving && SegmentIntersectsWall(
                    state.world,
                    previousPosition,
                    candidatePosition,
                    GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
                const bool edgeOnWallContact = moving && !segmentWallHit &&
                    IsEdgeOnWallContact(state.world, candidatePosition, movementHeading);
                if (segmentWallHit || edgeOnWallContact) {
                    const float movedLastFrameUnits = Distance(candidatePosition, previousPosition);
                    enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
                    // Only zero timer when entering uncouple from non-uncouple; preserve it on re-entry
                    // so the assassin can accumulate movement progress and eventually escape.
                    if (enemy.aiMode != EnemyAiMode::Uncouple) {
                        enemy.aiStateTimerSeconds = 0.0F;
                    }
                    // Wall contact enters uncouple so wall and neighbor repulsion can resolve local jams.
                    EnterUncoupleMode(
                        state.world.enemies,
                        enemyIndex,
                        enemyIndex,
                        UncoupleReason::SelfWallContact,
                        movedLastFrameUnits);
                } else {
                    enemy.velocity = Vec2f{
                        .x = snappedDirection.x * speed,
                        .y = snappedDirection.y * speed,
                    };
                    enemy.position = candidatePosition;
                    enemy.headingRadians = movementHeading;
                }
            }

            if (enemy.type == EnemyType::Torpedo && enemy.torpedoMoveMode == TorpedoMoveMode::Move) {
                const float movedDistance = Distance(enemy.position, previousPosition);
                if (movedDistance > 0.0001F) {
                    enemy.torpedoStraightDistanceSinceTurnUnits += movedDistance;
                    enemy.torpedoMoveDecisionHoldRemainingUnits = std::max(
                        0.0F,
                        enemy.torpedoMoveDecisionHoldRemainingUnits - movedDistance);
                }
            } else if (enemy.type == EnemyType::Torpedo && enemy.torpedoMoveMode == TorpedoMoveMode::Retreat) {
                const float movedDistance = Distance(enemy.position, previousPosition);
                if (movedDistance > 0.0001F) {
                    enemy.torpedoRetreatMovedUnits += movedDistance;
                }
            }
        }

        {
            profiling::ScopedProfile phaseScope(profiling::Scope::EnemyAiFiring, true);
            RunFiringPhase(state, enemy, perception, view, deltaSeconds);
        }

        const game::navigation::MazeCellCoord fullCell =
            cellCache.WorldToCell(enemy.position);
        occupancy.SetCell(enemyIndex, fullCell.x, fullCell.y);
    }

    std::vector<std::uint8_t> fullTierMask(state.world.enemies.size(), 0U);
    for (std::size_t i = 0; i < state.world.enemies.size(); ++i) {
        const EnemyTank& enemy = state.world.enemies[i];
        if (!enemy.alive) {
            continue;
        }
        gEnemyRuntimeStats.aliveCount += 1;
        if (IsInPlayerViewport(enemy.position, state, view)) {
            gEnemyRuntimeStats.visibleInViewportCount += 1;
        }
        if (enemy.simTier == EnemySimTier::Full) {
            fullTierMask[i] = 1U;
            gEnemyRuntimeStats.fullTierCount += 1;
            if (game::geometry::IsPointInUndestroyedBase(state.world, enemy.position, 1.0F)) {
                gEnemyRuntimeStats.fullTierInBaseClearanceCount += 1;
            }
        } else {
            gEnemyRuntimeStats.cheapTierCount += 1;
        }
    }
    AccumulateEnemyWindowStats(
        gEnemyRuntimeStats.aliveCount,
        gEnemyRuntimeStats.visibleInViewportCount,
        gEnemyRuntimeStats.fullTierCount);
    AccumulateEnemyWindowTime(deltaSeconds);

    if (gEnemyRuntimeStats.fullTierCount >= 2) {
        if (kUseSweepPruneBroadPhase) {
            game::spatial::SweepPruneBroadPhase& broadPhase = state.world.collisionCache.sweepPrune;
            ResolveEnemyCollisionsSinglePass(
                state.world,
                frameStartPositions,
                broadPhase,
                fullTierMask,
                reenteredFullTierMask);
        } else {
            game::spatial::EnemySpatialGrid spatialGrid;
            ResolveEnemyFrontalCollisionsLegacyGrid(
                state.world,
                frameStartPositions,
                spatialGrid,
                fullTierMask,
                reenteredFullTierMask);
            ResolveEnemySeparationLegacyGrid(state.world, spatialGrid, fullTierMask, reenteredFullTierMask);
        }
        gEnemyRuntimeWindowStats.collisionPassRuns += 1;
    } else {
        gEnemyRuntimeWindowStats.collisionPassSkips += 1;
    }
    AccumulateEnemyWindowPairStats(gEnemyRuntimeStats);

    const auto& profiler = profiling::Profiler::Instance();
    const std::uint64_t frameIndex = profiler.FrameIndex();
    if (profiler.ShouldEmitPeriodicReport() && frameIndex != gLastEnemyStatsPrintedFrame) {
        gLastEnemyStatsPrintedFrame = frameIndex;
        std::printf(
            "[ENEMY_STATS] frame=%llu alive=%d visible=%d full=%d cheap=%d fullInBase=%d pairs(frontal=%d checks=%d separation=%d resolved=%d)\n",
            static_cast<unsigned long long>(frameIndex),
            gEnemyRuntimeStats.aliveCount,
            gEnemyRuntimeStats.visibleInViewportCount,
            gEnemyRuntimeStats.fullTierCount,
            gEnemyRuntimeStats.cheapTierCount,
            gEnemyRuntimeStats.fullTierInBaseClearanceCount,
            gEnemyRuntimeStats.frontalPairsVisited,
            gEnemyRuntimeStats.frontalPairsDistanceChecks,
            gEnemyRuntimeStats.separationPairsVisited,
            gEnemyRuntimeStats.separationPairsResolved);
        if (gEnemyRuntimeWindowStats.fixedSteps > 0) {
            std::printf(
                "[ENEMY_WINDOW] steps=%llu alive[min=%d max=%d] visible[min=%d max=%d] full[min=%d max=%d] collisionPasses[runs=%llu skips=%llu]\n",
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.fixedSteps),
                gEnemyRuntimeWindowStats.minAliveCount,
                gEnemyRuntimeWindowStats.maxAliveCount,
                gEnemyRuntimeWindowStats.minVisibleCount,
                gEnemyRuntimeWindowStats.maxVisibleCount,
                gEnemyRuntimeWindowStats.minFullTierCount,
                gEnemyRuntimeWindowStats.maxFullTierCount,
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.collisionPassRuns),
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.collisionPassSkips));

            const float windowSeconds = std::max(0.0001F, gEnemyRuntimeWindowStats.windowSeconds);
            std::printf("[ENEMY_SEGMENTS] window=%.3fs", windowSeconds);
            for (int typeIdx = 0; typeIdx < kEnemyTypeTelemetryCount; ++typeIdx) {
                const std::uint64_t built =
                    gEnemyRuntimeWindowStats.segmentsBuiltByType[static_cast<std::size_t>(typeIdx)];
                const std::uint64_t fails =
                    gEnemyRuntimeWindowStats.segmentBuildFailsByType[static_cast<std::size_t>(typeIdx)];
                const float builtPerSec = static_cast<float>(built) / windowSeconds;
                const float avgLen = built > 0
                    ? (gEnemyRuntimeWindowStats.segmentLengthSumByType[static_cast<std::size_t>(typeIdx)] /
                        static_cast<float>(built))
                    : 0.0F;
                std::printf(
                    " %s{b/s=%.2f avgLen=%.2f fail=%llu}",
                    EnemyTypeTelemetryLabel(typeIdx),
                    builtPerSec,
                    avgLen,
                    static_cast<unsigned long long>(fails));
            }
            std::printf("\n");

            const float frontalPairsPerSec =
                static_cast<float>(gEnemyRuntimeWindowStats.frontalPairsVisited) / windowSeconds;
            const float frontalChecksPerSec =
                static_cast<float>(gEnemyRuntimeWindowStats.frontalPairsDistanceChecks) / windowSeconds;
            const float separationPairsPerSec =
                static_cast<float>(gEnemyRuntimeWindowStats.separationPairsVisited) / windowSeconds;
            const float separationResolvePerSec =
                static_cast<float>(gEnemyRuntimeWindowStats.separationPairsResolved) / windowSeconds;
            const float frontalKillPct = gEnemyRuntimeWindowStats.frontalPairsDistanceChecks > 0
                ? (100.0F * static_cast<float>(gEnemyRuntimeWindowStats.frontalPairsMutualKills) /
                    static_cast<float>(gEnemyRuntimeWindowStats.frontalPairsDistanceChecks))
                : 0.0F;
            const float separationResolvePct = gEnemyRuntimeWindowStats.separationPairsVisited > 0
                ? (100.0F * static_cast<float>(gEnemyRuntimeWindowStats.separationPairsResolved) /
                    static_cast<float>(gEnemyRuntimeWindowStats.separationPairsVisited))
                : 0.0F;
            std::printf(
                "[ENEMY_GRID] frontal{cand/s=%.1f xcell/s=%.1f ins/s=%.1f} separation{cand/s=%.1f}\n",
                static_cast<float>(gEnemyRuntimeWindowStats.frontalGridCandidates) / windowSeconds,
                static_cast<float>(gEnemyRuntimeWindowStats.frontalGridCellTransitions) / windowSeconds,
                static_cast<float>(gEnemyRuntimeWindowStats.frontalGridInsertEstimate) / windowSeconds,
                static_cast<float>(gEnemyRuntimeWindowStats.separationGridCandidates) / windowSeconds);
            std::printf(
                "[ENEMY_COLLISION_WINDOW] frontal{pairs/s=%.1f checks/s=%.1f baseSkip=%llu kill=%llu(%.1f%%)} separation{pairs/s=%.1f resolved/s=%.1f(%.1f%%) baseSkip=%llu kill=%llu wallBlock=%llu}\n",
                frontalPairsPerSec,
                frontalChecksPerSec,
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.frontalPairsBaseSkipped),
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.frontalPairsMutualKills),
                frontalKillPct,
                separationPairsPerSec,
                separationResolvePerSec,
                separationResolvePct,
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.separationPairsBaseSkipped),
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.separationPairsMutualKills),
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.separationPairsWallBlockedPushes));
            const float uncoupleReentryPct = gEnemyRuntimeWindowStats.uncoupleEntries > 0
                ? (100.0F * static_cast<float>(gEnemyRuntimeWindowStats.uncoupleReentryResets) /
                    static_cast<float>(gEnemyRuntimeWindowStats.uncoupleEntries))
                : 0.0F;
            std::printf(
                "[ENEMY_UNCOUPLE] entries=%llu reentryResets=%llu(%.1f%%) reason{frontal=%llu separation=%llu wallContact=%llu}\n",
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.uncoupleEntries),
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.uncoupleReentryResets),
                uncoupleReentryPct,
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.uncoupleEntriesFrontal),
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.uncoupleEntriesSeparation),
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.uncoupleEntriesWallContact));
            if (gEnemyRuntimeWindowStats.killDebugEnemyEnemyEvents > 0) {
                const float killDistAvg = static_cast<float>(
                    gEnemyRuntimeWindowStats.killDebugEnemyEnemyDistanceSum /
                    static_cast<double>(gEnemyRuntimeWindowStats.killDebugEnemyEnemyEvents));
                std::printf(
                    "[ENEMY_KILL_DEBUG] enemyEnemy{events=%llu frontal=%llu separation=%llu reenterEither=%llu reenterBoth=%llu wallContact=%llu dist[min=%.3f avg=%.3f max=%.3f]}\n",
                    static_cast<unsigned long long>(gEnemyRuntimeWindowStats.killDebugEnemyEnemyEvents),
                    static_cast<unsigned long long>(gEnemyRuntimeWindowStats.killDebugEnemyEnemyFrontalEvents),
                    static_cast<unsigned long long>(gEnemyRuntimeWindowStats.killDebugEnemyEnemySeparationEvents),
                    static_cast<unsigned long long>(gEnemyRuntimeWindowStats.killDebugEnemyEnemyReenterEither),
                    static_cast<unsigned long long>(gEnemyRuntimeWindowStats.killDebugEnemyEnemyReenterBoth),
                    static_cast<unsigned long long>(gEnemyRuntimeWindowStats.killDebugEnemyEnemyWallContact),
                    gEnemyRuntimeWindowStats.killDebugEnemyEnemyMinDistance,
                    killDistAvg,
                    gEnemyRuntimeWindowStats.killDebugEnemyEnemyMaxDistance);
            } else {
                std::printf(
                    "[ENEMY_KILL_DEBUG] enemyEnemy{events=0 frontal=0 separation=0 reenterEither=0 reenterBoth=0 wallContact=0 dist[min=0.000 avg=0.000 max=0.000]}\n");
            }
            const std::uint64_t flowTotalAttempts =
                gEnemyRuntimeWindowStats.navFlowHeadingSelections + gEnemyRuntimeWindowStats.navFlowMisses;
            const float flowHitPct = flowTotalAttempts > 0
                ? (100.0F * static_cast<float>(gEnemyRuntimeWindowStats.navFlowHeadingSelections) /
                    static_cast<float>(flowTotalAttempts))
                : 0.0F;
            const float pathBuildSuccessPct = gEnemyRuntimeWindowStats.navPathBuildCalls > 0
                ? (100.0F * static_cast<float>(gEnemyRuntimeWindowStats.navPathBuildSuccesses) /
                    static_cast<float>(gEnemyRuntimeWindowStats.navPathBuildCalls))
                : 0.0F;
            std::printf(
                "[ENEMY_NAV_CACHE] playerCell{changes=%llu flowRebuilds=%llu} flow{hit=%llu miss=%llu hit%%=%.1f} pathFallback{calls=%llu ok=%llu ok%%=%.1f}\n",
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.navPlayerCellChanges),
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.navFlowRebuilds),
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.navFlowHeadingSelections),
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.navFlowMisses),
                flowHitPct,
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.navPathBuildCalls),
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.navPathBuildSuccesses),
                pathBuildSuccessPct);
            std::printf(
                "[ENEMY_SAP] updates=%llu active=%llu pairs=%llu repairs{x=%llu y=%llu}\n",
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.sapUpdateCalls),
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.sapActiveItems),
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.sapCandidatePairs),
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.sapXRepairs),
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.sapYRepairs));
            std::printf("[ENEMY_PAIR_TYPES] frontal");
            for (int i = 0; i < kEnemyTypeTelemetryCount; ++i) {
                for (int j = i; j < kEnemyTypeTelemetryCount; ++j) {
                    const std::size_t bucket = static_cast<std::size_t>(i * kEnemyTypeTelemetryCount + j);
                    std::printf(
                        " %s%s=%llu",
                        EnemyTypeTelemetryLabel(i),
                        EnemyTypeTelemetryLabel(j),
                        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.frontalPairsByType[bucket]));
                }
            }
            std::printf(" separation");
            for (int i = 0; i < kEnemyTypeTelemetryCount; ++i) {
                for (int j = i; j < kEnemyTypeTelemetryCount; ++j) {
                    const std::size_t bucket = static_cast<std::size_t>(i * kEnemyTypeTelemetryCount + j);
                    std::printf(
                        " %s%s=%llu",
                        EnemyTypeTelemetryLabel(i),
                        EnemyTypeTelemetryLabel(j),
                        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.separationPairsByType[bucket]));
                }
            }
            std::printf("\n");

            if (gEnemyRuntimeWindowStats.torpedoHeadingEvalCalls > 0) {
                const float evalPerSec =
                    static_cast<float>(gEnemyRuntimeWindowStats.torpedoHeadingEvalCalls) / windowSeconds;
                const float retreatPerSec =
                    static_cast<float>(gEnemyRuntimeWindowStats.torpedoHeadingRetreatStarts) / windowSeconds;
                const float totalChosen = static_cast<float>(
                    gEnemyRuntimeWindowStats.torpedoHeadingChosenStraight +
                    gEnemyRuntimeWindowStats.torpedoHeadingChosenLeft +
                    gEnemyRuntimeWindowStats.torpedoHeadingChosenRight);
                const float straightPct = totalChosen > 0.0F
                    ? (100.0F * static_cast<float>(gEnemyRuntimeWindowStats.torpedoHeadingChosenStraight) / totalChosen)
                    : 0.0F;
                const float leftPct = totalChosen > 0.0F
                    ? (100.0F * static_cast<float>(gEnemyRuntimeWindowStats.torpedoHeadingChosenLeft) / totalChosen)
                    : 0.0F;
                const float rightPct = totalChosen > 0.0F
                    ? (100.0F * static_cast<float>(gEnemyRuntimeWindowStats.torpedoHeadingChosenRight) / totalChosen)
                    : 0.0F;
                const float avgBestClear = static_cast<float>(
                    gEnemyRuntimeWindowStats.torpedoHeadingBestClearSum /
                    static_cast<double>(gEnemyRuntimeWindowStats.torpedoHeadingEvalCalls));
                const float avgChosenClear = static_cast<float>(
                    gEnemyRuntimeWindowStats.torpedoHeadingChosenClearSum /
                    static_cast<double>(gEnemyRuntimeWindowStats.torpedoHeadingEvalCalls));
                std::printf(
                    "[TORPEDO_HEADING] eval/s=%.2f retreat/s=%.2f pick[straight=%.1f%% left=%.1f%% right=%.1f%%] clear[best=%.2f chosen=%.2f]\n",
                    evalPerSec,
                    retreatPerSec,
                    straightPct,
                    leftPct,
                    rightPct,
                    avgBestClear,
                    avgChosenClear);
            }

            ResetEnemyWindowStats();
        }
        std::fflush(stdout);
    }
}

const EnemyRuntimeStats& GetEnemyRuntimeStats() {
    return gEnemyRuntimeStats;
}
