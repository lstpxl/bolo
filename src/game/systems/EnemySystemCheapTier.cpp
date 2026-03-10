#include "game/systems/EnemySystemCheapTier.h"

#include <algorithm>
#include <array>
#include <cmath>
#include "core/AngleMath.h"
#include "core/Profiling.h"
#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"
#include "game/systems/EnemyAssassin.h"
#include "game/systems/EnemySystemHelpers.h"
#include "game/systems/EnemySystemInternal.h"
#include "game/systems/EnemySystemUncouple.h"

namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kEightDirectionStep = kPi / 4.0F;
constexpr float kSegmentBuildProbeMaxUnits = 15.0F;
constexpr float kSegmentBuildSafetyReduceUnits = 4.0F;
constexpr float kSegmentBuildMinLengthUnits = 2.0F;
constexpr float kOffscreenSegmentLengthUnits = 8.0F;
constexpr float kOffscreenTorpedoSegmentLengthUnits = 12.0F;
constexpr float kOffscreenTorpedoDetectIntervalSeconds = 0.6F;

float NormalizeAngle(float angleRadians) {
    return core::angle::NormalizeAngle(angleRadians);
}

float QuantizeToEightDirections(float angleRadians) {
    return core::angle::QuantizeToEightDirections(angleRadians);
}

Vec2f DirectionFromHeading(float headingRadians) {
    return core::angle::DirectionFromHeading(headingRadians);
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
}  // namespace

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

    enemy.fireCooldownSeconds = std::max(0.0F, enemy.fireCooldownSeconds - deltaSeconds);
    if (enemy.aiMode == EnemyAiMode::Uncouple) {
        enemy.aiStateTimerSeconds = std::max(0.0F, enemy.aiStateTimerSeconds - deltaSeconds);
        if (enemy.aiStateTimerSeconds <= 0.0F) {
            RestoreFromUncoupleMode(enemy);
        }
    }
    (void)view;
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

    if (enemy.type == EnemyType::Assassin) {
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
