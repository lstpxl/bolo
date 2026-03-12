#include "game/systems/EnemySystemCheapTier.h"

#include <algorithm>
#include <array>
#include <cmath>
#include "core/AngleMath.h"
#include "core/Log.h"
#include "core/Profiling.h"
#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"
#include "game/systems/EnemyAssassin.h"
#include "game/systems/EnemyHunter.h"
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
constexpr int kAssassinWallPhaseCheap = 0;

int StageBucketIndex(int stage) {
    if (stage <= 0) {
        return 0;
    }
    if (stage == 1) {
        return 1;
    }
    return 2;
}

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
            enemy.torpedoPlayerDetected = false;
        }
    }

    enemy.fireCooldownSeconds = std::max(0.0F, enemy.fireCooldownSeconds - deltaSeconds);
    if (enemy.aiMode == EnemyAiMode::Uncouple) {
        enemy.aiStateTimerSeconds = std::max(0.0F, enemy.aiStateTimerSeconds - deltaSeconds);
        if (enemy.aiStateTimerSeconds <= 0.0F) {
            RestoreFromUncoupleMode(enemy);
        }
    }
    (void)state;
    (void)playerInvisible;
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
    Random& random) {
    const Vec2f cheapStartPosition = enemy.position;
    bool lastInsideWallAvoid = enemy.cheapSegmentInsideWallAvoidLastFrame;
    auto updateAssassinWallState = [&](const char* phaseLabel, int phaseBucket) {
        if (enemy.type != EnemyType::Assassin) {
            return;
        }
        const bool nowInsideWallAvoid = game::geometry::IsPointInWall(
            state.world, enemy.position, GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
        if (!lastInsideWallAvoid && nowInsideWallAvoid) {
            if (phaseBucket == kAssassinWallPhaseCheap) {
                gEnemyRuntimeWindowStats
                    .assassinWallAvoidEntriesByPhase[static_cast<std::size_t>(phaseBucket)] += 1;
            }
            const game::navigation::MazeCellCoord wallCell = cellCache.WorldToCell(enemy.position);
            const int wallCellHash = cellCache.CellHash(wallCell.x, wallCell.y);
            const int flowNextHash = flowField.HasBuild()
                ? flowField.NextCellHash(wallCellHash)
                : -1;
            bool insideWallTank = game::geometry::IsPointInWall(
                state.world, enemy.position, GameplayConstants::kTankCollisionRadiusUnits);
            bool insideBase = game::geometry::IsPointInUndestroyedBase(
                state.world, enemy.position, GameplayConstants::kTankCollisionRadiusUnits);
            int overlapCount = 0;
            float nearestEnemyDistance = 9999.0F;
            for (std::size_t i = 0; i < state.world.enemies.size(); ++i) {
                if (static_cast<int>(i) == enemyIndex) {
                    continue;
                }
                const EnemyTank& other = state.world.enemies[i];
                if (!other.alive) {
                    continue;
                }
                const float dist = Distance(enemy.position, other.position);
                nearestEnemyDistance = std::min(nearestEnemyDistance, dist);
                if (dist < GameplayConstants::kEnemyPreferredSeparationUnits) {
                    overlapCount += 1;
                }
            }
            if (nearestEnemyDistance > 9998.0F) {
                nearestEnemyDistance = -1.0F;
            }
            bolt::log::Profile(
                "[ENEMY_ASSASSIN_WALL_STATE_CHANGE] id=%d phase=%s insideWallAvoid=1 "
                "posFrom=(%.3f,%.3f) posTo=(%.3f,%.3f) cell=(%d,%d) cellHash=%d flowNextHash=%d "
                "offscreenActive=%d segEnd=(%.3f,%.3f) heading=%.3f "
                "failCount=%d methodStage=%d failReason=%d "
                "insideWallTank=%d insideBase=%d overlapCount=%d nearestEnemyDist=%.3f\n",
                enemyIndex,
                phaseLabel,
                cheapStartPosition.x,
                cheapStartPosition.y,
                enemy.position.x,
                enemy.position.y,
                wallCell.x,
                wallCell.y,
                wallCellHash,
                flowNextHash,
                enemy.offscreenSegmentActive ? 1 : 0,
                enemy.offscreenSegmentEnd.x,
                enemy.offscreenSegmentEnd.y,
                enemy.offscreenCachedHeadingRadians,
                enemy.cheapSegmentBuildFailCount,
                enemy.cheapSegmentBuildMethodStage,
                static_cast<int>(enemy.cheapSegmentLastFailReason),
                insideWallTank ? 1 : 0,
                insideBase ? 1 : 0,
                overlapCount,
                nearestEnemyDistance);
        }
        lastInsideWallAvoid = nowInsideWallAvoid;
        enemy.cheapSegmentInsideWallAvoidLastFrame = nowInsideWallAvoid;
    };
    bool usingHunterScoutPath = false;
    Vec2f hunterScoutTargetPoint{};
    float hunterScoutHeading = enemy.headingRadians;

    float segmentLength = kOffscreenSegmentLengthUnits;
    if (enemy.type == EnemyType::Torpedo) {
        segmentLength = kOffscreenTorpedoSegmentLengthUnits;
    }

    if (enemy.type == EnemyType::Assassin) {
        // Cheap-tier assassins must not drift on stale segments without flow guidance.
        // This is expected while invisibility is enabled and flow cache is inactive.
        if (!flowField.HasBuild()) {
            enemy.offscreenSegmentActive = false;
            enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
            enemy.cheapSegmentBuildFailCount = 0;
            enemy.cheapSegmentLastFailCellHash = -1;
            enemy.cheapSegmentLastFailReason = CheapSegmentFailReason::None;
            enemy.cheapSegmentBuildMethodStage = 0;
            gEnemyRuntimeWindowStats.assassinCheapNoFlowSkips += 1;
            updateAssassinWallState("cheap_no_flow_idle", kAssassinWallPhaseCheap);
            return;
        }

        const game::navigation::MazeCellCoord& enemyCell = enemy.cellCoord;
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
            const bool wasSegmentActive = enemy.offscreenSegmentActive;
            int methodStage = 0;
            if (enemy.cheapSegmentBuildFailCount >= 6) {
                methodStage = 2;
            } else if (enemy.cheapSegmentBuildFailCount >= 3) {
                methodStage = 1;
            }
            enemy.cheapSegmentBuildMethodStage = methodStage;
            if (!BuildAssassinCheapFlowSegment(
                    state.world,
                    cellCache,
                    flowField,
                    enemy,
                    enemyIndex,
                    methodStage)) {
                enemy.offscreenSegmentActive = false;
                enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
                if (enemy.cheapSegmentLastFailReason != CheapSegmentFailReason::None) {
                    enemy.cheapSegmentBuildFailCount += 1;
                    enemy.cheapSegmentLastFailCellHash = enemyCellHash;
                    const int stageBucket = StageBucketIndex(methodStage);
                    gEnemyRuntimeWindowStats
                        .assassinCheapBuildFailsByStage[static_cast<std::size_t>(stageBucket)] +=
                        1;
                    bolt::log::Profile(
                        "[ENEMY_ASSASSIN_SEGMENT_DROP] id=%d reason=cheap_flow_build_failed "
                        "wasActive=%d enteredNewCell=%d reachedSegmentEnd=%d "
                        "pos=(%.3f,%.3f) cell=(%d,%d) failCount=%d methodStage=%d failReason=%d\n",
                        enemyIndex,
                        wasSegmentActive ? 1 : 0,
                        enteredNewCell ? 1 : 0,
                        reachedSegmentEnd ? 1 : 0,
                        enemy.position.x,
                        enemy.position.y,
                        enemy.cellCoord.x,
                        enemy.cellCoord.y,
                        enemy.cheapSegmentBuildFailCount,
                        enemy.cheapSegmentBuildMethodStage,
                        static_cast<int>(enemy.cheapSegmentLastFailReason));
                } else {
                    // Expected no-flow state (for example while player is dead): no escalation/log spam.
                    enemy.cheapSegmentBuildFailCount = 0;
                    enemy.cheapSegmentLastFailCellHash = -1;
                    enemy.cheapSegmentBuildMethodStage = 0;
                    gEnemyRuntimeWindowStats.assassinCheapNoFlowSkips += 1;
                }
            } else {
                if (enemy.cheapSegmentBuildFailCount > 0) {
                    const int stageBucket = StageBucketIndex(methodStage);
                    gEnemyRuntimeWindowStats.assassinCheapBuildRecoveriesByStage
                        [static_cast<std::size_t>(stageBucket)] += 1;
                    bolt::log::Profile(
                        "[ENEMY_ASSASSIN_SEGMENT_RECOVER] id=%d reason=build_recovered "
                        "cellHash=%d recoveredAfter=%d methodStage=%d\n",
                        enemyIndex,
                        enemyCellHash,
                        enemy.cheapSegmentBuildFailCount,
                        methodStage);
                }
                enemy.cheapSegmentBuildFailCount = 0;
                enemy.cheapSegmentLastFailCellHash = -1;
                enemy.cheapSegmentLastFailReason = CheapSegmentFailReason::None;
                enemy.cheapSegmentBuildMethodStage = 0;
            }
        }
    } else if (enemy.type == EnemyType::Hunter && enemy.aiMode == EnemyAiMode::Scout) {
        if (SelectHunterScoutMotion(
                state.world,
                cellCache,
                enemy,
                random,
                hunterScoutHeading,
                hunterScoutTargetPoint)) {
            usingHunterScoutPath = true;
        } else {
            InvalidateHunterScoutPath(enemy);
        }
    } else {
        const bool reachedSegmentEnd =
            !enemy.offscreenSegmentActive ||
            DistanceSq(enemy.position, enemy.offscreenSegmentEnd) <= 0.04F;
        if (reachedSegmentEnd) {
            BuildOffscreenSegment(state.world, enemy, segmentLength, random);
        }
    }

    const bool hasMovementPath = usingHunterScoutPath || enemy.offscreenSegmentActive;
    if (!hasMovementPath || std::fabs(speed) <= 0.0001F) {
        enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        updateAssassinWallState("cheap_early_return", kAssassinWallPhaseCheap);
        return;
    }

    float effectiveSpeed = speed;
    if (enemy.type == EnemyType::Assassin && enemy.cheapTierCrowdedSlowMode) {
        effectiveSpeed *= 0.5F;
    }
    const float activeHeading =
        usingHunterScoutPath ? hunterScoutHeading : enemy.offscreenCachedHeadingRadians;
    const Vec2f activeTargetPoint =
        usingHunterScoutPath ? hunterScoutTargetPoint : enemy.offscreenSegmentEnd;
    const Vec2f dir = DirectionFromHeading(activeHeading);
    const float maxStep = std::max(0.0F, effectiveSpeed * deltaSeconds);
    const float remaining = Distance(enemy.position, activeTargetPoint);
    const float step = std::min(maxStep, remaining);
    const Vec2f candidatePosition{
        .x = enemy.position.x + dir.x * step,
        .y = enemy.position.y + dir.y * step,
    };
    if (enemy.type == EnemyType::Assassin) {
        const bool startInsideWallAvoid = game::geometry::IsPointInWall(
            state.world, enemy.position, GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
        const bool candidateInsideWallAvoid = game::geometry::IsPointInWall(
            state.world, candidatePosition, GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
        if (!startInsideWallAvoid && candidateInsideWallAvoid) {
            const float clearAhead = game::geometry::FreeDistanceAhead(
                state.world,
                enemy.position,
                activeHeading,
                std::max(1.5F, step + 0.5F),
                GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                1.0F);
            bolt::log::Profile(
                "[ENEMY_ASSASSIN_WALL_CAUSE] id=%d source=cheap_segment "
                "posPrev=(%.3f,%.3f) posCandidate=(%.3f,%.3f) segEnd=(%.3f,%.3f) "
                "heading=%.3f speed=%.3f delta=%.3f step=%.3f clearAhead=%.3f "
                "insideWallAvoidPrev=%d insideWallAvoidCandidate=%d failCount=%d methodStage=%d\n",
                enemyIndex,
                enemy.position.x,
                enemy.position.y,
                candidatePosition.x,
                candidatePosition.y,
                enemy.offscreenSegmentEnd.x,
                enemy.offscreenSegmentEnd.y,
                enemy.offscreenCachedHeadingRadians,
                effectiveSpeed,
                deltaSeconds,
                step,
                clearAhead,
                startInsideWallAvoid ? 1 : 0,
                candidateInsideWallAvoid ? 1 : 0,
                enemy.cheapSegmentBuildFailCount,
                enemy.cheapSegmentBuildMethodStage);
        }
    }
    enemy.position = candidatePosition;
    enemy.headingRadians = activeHeading;
    enemy.velocity = Vec2f{
        .x = dir.x * effectiveSpeed,
        .y = dir.y * effectiveSpeed,
    };

    if (enemy.type == EnemyType::Torpedo && enemy.torpedoMoveMode == TorpedoMoveMode::Move) {
        enemy.torpedoStraightDistanceSinceTurnUnits += step;
    }
    updateAssassinWallState("cheap_post_move", kAssassinWallPhaseCheap);
    (void)enemyIndex;
}
