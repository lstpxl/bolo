#include "game/systems/EnemyTorpedo.h"

#include <cmath>
#include "core/AngleMath.h"
#include "core/Profiling.h"
#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"
#include "game/systems/EnemySystemInternal.h"
#include "game/spatial/EnemyCellOccupancy.h"

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kEightDirectionStep = kPi / 4.0F;
constexpr float kCosThirtyDegrees = 0.8660254F;
constexpr float kCosTwentyDegrees = 0.9396926F;
constexpr float kTorpedoLongPathProbeUnits = 24.0F;
constexpr float kTorpedoImmediateObstacleDistanceUnits = 1.0F;
constexpr float kSegmentBuildProbeMaxUnits = 15.0F;
constexpr float kSegmentBuildSafetyReduceUnits = 4.0F;
constexpr float kSegmentBuildMinLengthUnits = 2.0F;
constexpr float kEnemyPlanningClearanceScale = 1.5F;

}  // namespace

bool PlayerAheadForTorpedo(const EnemyTank& enemy, const Vec2f& toPlayerNormalized) {
    const Vec2f forward = core::angle::DirectionFromHeading(enemy.headingRadians);
    const float dot = forward.x * toPlayerNormalized.x + forward.y * toPlayerNormalized.y;
    return dot >= kCosThirtyDegrees;
}

bool PlayerAheadForTorpedoRam(const EnemyTank& enemy, const Vec2f& toPlayerNormalized) {
    const Vec2f forward = core::angle::DirectionFromHeading(enemy.headingRadians);
    const float dot = forward.x * toPlayerNormalized.x + forward.y * toPlayerNormalized.y;
    return dot >= kCosTwentyDegrees;
}

float SelectBestLongStraightHeading(const WorldState& world, const EnemyTank& enemy) {
    float bestHeading = core::angle::QuantizeToEightDirections(enemy.headingRadians);
    float bestClear = -1.0F;
    for (int step = 0; step < 8; ++step) {
        const float candidate =
            core::angle::NormalizeAngle(static_cast<float>(step) * kEightDirectionStep);
        const float clearDist = game::geometry::FreeDistanceAhead(
            world,
            enemy.position,
            candidate,
            kTorpedoLongPathProbeUnits,
            GameplayConstants::kWallClearanceForAvoidance,
            kEnemyPlanningClearanceScale);
        if (clearDist > bestClear) {
            bestClear = clearDist;
            bestHeading = candidate;
        }
    }
    return core::angle::QuantizeToEightDirections(bestHeading);
}

float SelectTorpedoMoveHeading(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    EnemyTank& enemy,
    Random& random,
    bool& startRetreat,
    bool& decidedStraight,
    const game::spatial::EnemyCellOccupancy* rayQueryOccupancy) {
    profiling::ScopedProfile selectScope(profiling::Scope::EnemyTorpedoSelectHeading, true);
    gEnemyRuntimeWindowStats.torpedoHeadingEvalCalls += 1;
    startRetreat = false;
    decidedStraight = true;
    const float straightHeading = core::angle::QuantizeToEightDirections(enemy.headingRadians);
    const float straightClearWithEnemies = game::geometry::FreeDistanceAheadWithEnemies(
        world,
        enemies,
        selfIndex,
        enemy.position,
        straightHeading,
        kSegmentBuildProbeMaxUnits,
        GameplayConstants::kWallClearanceForAvoidance,
        kEnemyPlanningClearanceScale,
        rayQueryOccupancy);
    const float leftHeading = core::angle::QuantizeToEightDirections(straightHeading - kEightDirectionStep);
    const float rightHeading = core::angle::QuantizeToEightDirections(straightHeading + kEightDirectionStep);
    const float leftClear = game::geometry::FreeDistanceAheadWithEnemies(
        world,
        enemies,
        selfIndex,
        enemy.position,
        leftHeading,
        kSegmentBuildProbeMaxUnits,
        GameplayConstants::kWallClearanceForAvoidance,
        kEnemyPlanningClearanceScale,
        rayQueryOccupancy);
    const float rightClear = game::geometry::FreeDistanceAheadWithEnemies(
        world,
        enemies,
        selfIndex,
        enemy.position,
        rightHeading,
        kSegmentBuildProbeMaxUnits,
        GameplayConstants::kWallClearanceForAvoidance,
        kEnemyPlanningClearanceScale,
        rayQueryOccupancy);

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
    enemy.aiMode = EnemyAiMode::Targeting;
}

void EnterTorpedoRotateMode(EnemyTank& enemy) {
    enemy.aiMode = EnemyAiMode::Rotate;
    enemy.torpedoRotateTargetHeadingRadians = enemy.torpedoChosenHeadingRadians;
}

float UpdateTorpedoRotateHeading(EnemyTank& enemy, float deltaSeconds) {
    const float rotateStep =
        (kPi * 2.0F / GameplayConstants::kSlowRotateFullTurnSeconds) * deltaSeconds;
    const float signedDelta = core::angle::SignedAngleDelta(
        enemy.headingRadians, enemy.torpedoRotateTargetHeadingRadians);
    if (std::fabs(signedDelta) <= rotateStep + 0.0001F) {
        const float heading =
            core::angle::QuantizeToEightDirections(enemy.torpedoRotateTargetHeadingRadians);
        enemy.aiMode = EnemyAiMode::Fly;
        enemy.torpedoStraightDistanceSinceTurnUnits = 0.0F;
        enemy.torpedoMoveDecisionHoldRemainingUnits = 0.0F;
        return heading;
    }
    const float direction = signedDelta > 0.0F ? 1.0F : -1.0F;
    return core::angle::NormalizeAngle(enemy.headingRadians + direction * rotateStep);
}

float UpdateTorpedoHeadingToward(
    float currentHeadingRadians,
    float targetHeadingRadians,
    float maxTurnSpeedRadiansPerSecond,
    float deltaSeconds) {
    const float rotateStep = maxTurnSpeedRadiansPerSecond * deltaSeconds;
    const float signedDelta = core::angle::SignedAngleDelta(currentHeadingRadians, targetHeadingRadians);
    if (std::fabs(signedDelta) <= rotateStep + 0.0001F) {
        return core::angle::NormalizeAngle(targetHeadingRadians);
    }
    const float direction = signedDelta > 0.0F ? 1.0F : -1.0F;
    return core::angle::NormalizeAngle(currentHeadingRadians + direction * rotateStep);
}
