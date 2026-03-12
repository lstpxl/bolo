#include "game/systems/EnemyAssassin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include "core/AngleMath.h"
#include "core/Log.h"
#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"
#include "game/navigation/CellCoordCache.h"
#include "game/navigation/PlayerFlowField.h"
#include "game/systems/EnemySystemHelpers.h"
#include "game/systems/EnemySystemInternal.h"

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kEightDirectionStep = kPi / 4.0F;
constexpr float kAssassinCheapSegmentOutOfCellUnits = 1.0F;
constexpr float kAssassinCheapSegmentCornerMarginUnits = 1.0F;

bool BuildSegmentExitPointOnFlowEdge(
    const game::navigation::CellCoordCache& cellCache,
    const game::navigation::MazeCellCoord& cell,
    const Vec2f& from,
    const Vec2f& segmentDirection,
    const Vec2f& flowDirection,
    float outOfCellUnits,
    float cornerMarginUnits,
    float edgeToleranceUnits,
    Vec2f& outTarget) {
    constexpr float kEpsilon = 0.0001F;
    const float cellSize = static_cast<float>(cellCache.CellSizeUnits());
    const float cellMinX = static_cast<float>(cell.x) * cellSize;
    const float cellMinY = static_cast<float>(cell.y) * cellSize;
    const float cellMaxX = cellMinX + cellSize;
    const float cellMaxY = cellMinY + cellSize;

    float distanceToEdge = 0.0F;
    float alongEdgeCoord = 0.0F;
    float alongEdgeMin = 0.0F;
    float alongEdgeMax = 0.0F;
    if (std::fabs(flowDirection.x) > 0.5F) {
        if (std::fabs(segmentDirection.x) <= kEpsilon) {
            return false;
        }
        const float edgeX = (flowDirection.x > 0.0F) ? cellMaxX : cellMinX;
        distanceToEdge = (edgeX - from.x) / segmentDirection.x;
        alongEdgeCoord = from.y + segmentDirection.y * distanceToEdge;
        alongEdgeMin = cellMinY + cornerMarginUnits;
        alongEdgeMax = cellMaxY - cornerMarginUnits;
    } else if (std::fabs(flowDirection.y) > 0.5F) {
        if (std::fabs(segmentDirection.y) <= kEpsilon) {
            return false;
        }
        const float edgeY = (flowDirection.y > 0.0F) ? cellMaxY : cellMinY;
        distanceToEdge = (edgeY - from.y) / segmentDirection.y;
        alongEdgeCoord = from.x + segmentDirection.x * distanceToEdge;
        alongEdgeMin = cellMinX + cornerMarginUnits;
        alongEdgeMax = cellMaxX - cornerMarginUnits;
    } else {
        return false;
    }

    if (!std::isfinite(distanceToEdge) || distanceToEdge <= kEpsilon) {
        return false;
    }
    if (alongEdgeCoord < (alongEdgeMin - edgeToleranceUnits) ||
        alongEdgeCoord > (alongEdgeMax + edgeToleranceUnits)) {
        return false;
    }
    alongEdgeCoord = std::min(alongEdgeMax, std::max(alongEdgeMin, alongEdgeCoord));

    const float distanceToTarget = distanceToEdge + outOfCellUnits;
    outTarget = Vec2f{
        .x = from.x + segmentDirection.x * distanceToTarget,
        .y = from.y + segmentDirection.y * distanceToTarget,
    };
    return true;
}

}  // namespace

bool TrySelectAssassinFlowNextStep(
    const game::navigation::CellCoordCache& cellCache,
    const game::navigation::PlayerFlowField& flowField,
    EnemyTank& enemy,
    float& outHeadingRadians) {
    if (!flowField.HasBuild()) {
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
    outHeadingRadians = core::angle::QuantizeToEightDirections(std::atan2(stepDir.x, -stepDir.y));
    enemy.cachedFlowFromCellHash = enemyCellHash;
    enemy.cachedFlowHeadingRadians = outHeadingRadians;
    gEnemyRuntimeWindowStats.navFlowHeadingSelections += 1;
    return true;
}

bool BuildAssassinCheapFlowSegment(
    WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    const game::navigation::PlayerFlowField& flowField,
    EnemyTank& enemy,
    int enemyIndex,
    int methodStage) {
    auto logFail = [&](const char* reason, CheapSegmentFailReason reasonCode) {
        enemy.cheapSegmentLastFailReason = reasonCode;
        enemy.cheapSegmentBuildMethodStage = methodStage;
        const game::navigation::MazeCellCoord failCell = cellCache.WorldToCell(enemy.position);
        const int failCellHash = cellCache.CellHash(failCell.x, failCell.y);
        const int flowNextHash = flowField.HasBuild() ? flowField.NextCellHash(failCellHash) : -1;
        bolt::log::Profile(
            "[ENEMY_ASSASSIN_SEGMENT_FAIL] id=%d reason=%s pos=(%.3f,%.3f) "
            "cell=(%d,%d) cellHash=%d expectedPathHash=%d cachedFlowFromHash=%d "
            "cachedFlowHeading=%.3f flowHasBuild=%d flowNextHash=%d methodStage=%d failCount=%d\n",
            enemyIndex,
            reason,
            enemy.position.x,
            enemy.position.y,
            failCell.x,
            failCell.y,
            failCellHash,
            enemy.expectedPathCellHash,
            enemy.cachedFlowFromCellHash,
            enemy.cachedFlowHeadingRadians,
            flowField.HasBuild() ? 1 : 0,
            flowNextHash,
            methodStage,
            enemy.cheapSegmentBuildFailCount);
    };
    const float cornerMarginUnits =
        methodStage >= 1 ? 0.75F : kAssassinCheapSegmentCornerMarginUnits;
    const float edgeToleranceUnits = methodStage >= 1 ? 0.10F : 0.0F;
    if (!flowField.HasBuild()) {
        // Expected while flow cache is inactive (for example player death state).
        // Keep this non-actionable: no failure log, no fail-counter escalation.
        enemy.cheapSegmentLastFailReason = CheapSegmentFailReason::None;
        enemy.cheapSegmentBuildMethodStage = methodStage;
        return false;
    }

    const game::navigation::MazeCellCoord enemyCell = cellCache.WorldToCell(enemy.position);
    const int enemyCellHash = cellCache.CellHash(enemyCell.x, enemyCell.y);
    const int nextCellHash = flowField.NextCellHash(enemyCellHash);
    if (nextCellHash < 0 || nextCellHash == enemyCellHash || cellCache.WidthCells() <= 0) {
        // Expected while player cell has no valid flow target (for example player death state).
        // Do not treat as actionable segment-build failure.
        enemy.cheapSegmentLastFailReason = CheapSegmentFailReason::None;
        enemy.cheapSegmentBuildMethodStage = methodStage;
        return false;
    }

    const int nextCellX = nextCellHash % cellCache.WidthCells();
    const int nextCellY = nextCellHash / cellCache.WidthCells();
    const int flowDx = nextCellX - enemyCell.x;
    const int flowDy = nextCellY - enemyCell.y;
    if (std::abs(flowDx) + std::abs(flowDy) != 1) {
        gEnemyRuntimeWindowStats.navFlowMisses += 1;
        logFail("non_cardinal_flow_step", CheapSegmentFailReason::NonCardinalFlowStep);
        return false;
    }

    const float flowHeading = core::angle::QuantizeToEightDirections(
        std::atan2(static_cast<float>(flowDx), -static_cast<float>(flowDy)));
    const Vec2f flowDirection = core::angle::DirectionFromHeading(flowHeading);
    float segmentHeading = flowHeading;
    Vec2f segmentTarget{};
    auto commitSuccess = [&](const Vec2f& target, float fallbackHeadingRadians) {
        Vec2f toTarget{
            .x = target.x - enemy.position.x,
            .y = target.y - enemy.position.y,
        };
        Vec2f targetDir = NormalizeOrZero(toTarget);
        float headingRadians = fallbackHeadingRadians;
        if (targetDir.x != 0.0F || targetDir.y != 0.0F) {
            headingRadians = core::angle::QuantizeToEightDirections(
                std::atan2(targetDir.x, -targetDir.y));
        }
        const Vec2f headingDir = core::angle::DirectionFromHeading(headingRadians);
        const float headingToTargetDot = headingDir.x * targetDir.x + headingDir.y * targetDir.y;
        const bool segmentIntersectsWall = game::geometry::SegmentIntersectsWall(
            world,
            enemy.position,
            target,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
        const bool commitPosInsideWallTank = game::geometry::IsPointInWall(
            world,
            enemy.position,
            GameplayConstants::kTankCollisionRadiusUnits);
        const bool targetPosInsideWallTank = game::geometry::IsPointInWall(
            world,
            target,
            GameplayConstants::kTankCollisionRadiusUnits);
        const bool commitPosInsideWallAvoid = game::geometry::IsPointInWall(
            world,
            enemy.position,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
        const bool targetPosInsideWallAvoid = game::geometry::IsPointInWall(
            world,
            target,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
        bolt::log::Profile(
            "[ENEMY_ASSASSIN_SEGMENT_COMMIT_DIAG] id=%d stage=%d pos=(%.3f,%.3f) "
            "segEnd=(%.3f,%.3f) heading=%.3f dirFromHeading=(%.3f,%.3f) "
            "dirToTarget=(%.3f,%.3f) dot=%.3f segmentIntersectsWall=%d "
            "insideWall{commitTank=%d targetTank=%d commitAvoid=%d targetAvoid=%d}\n",
            enemyIndex,
            methodStage,
            enemy.position.x,
            enemy.position.y,
            target.x,
            target.y,
            headingRadians,
            headingDir.x,
            headingDir.y,
            targetDir.x,
            targetDir.y,
            headingToTargetDot,
            segmentIntersectsWall ? 1 : 0,
            commitPosInsideWallTank ? 1 : 0,
            targetPosInsideWallTank ? 1 : 0,
            commitPosInsideWallAvoid ? 1 : 0,
            targetPosInsideWallAvoid ? 1 : 0);
        const int typeIdx = EnemyTypeTelemetryIndex(enemy.type);
        enemy.offscreenCachedHeadingRadians = headingRadians;
        enemy.offscreenSegmentEnd = target;
        enemy.offscreenSegmentActive = true;
        enemy.expectedPathCellHash = nextCellHash;
        enemy.cachedFlowFromCellHash = enemyCellHash;
        enemy.cachedFlowHeadingRadians = flowHeading;
        enemy.cheapSegmentLastFailReason = CheapSegmentFailReason::None;
        enemy.cheapSegmentBuildMethodStage = methodStage;
        gEnemyRuntimeWindowStats.navFlowHeadingSelections += 1;
        gEnemyRuntimeWindowStats.segmentsBuiltByType[static_cast<std::size_t>(typeIdx)] += 1;
        gEnemyRuntimeWindowStats.segmentLengthSumByType[static_cast<std::size_t>(typeIdx)] +=
            Distance(enemy.position, target);
    };
    auto tryEmergencyFallback = [&]() {
        gEnemyRuntimeWindowStats.assassinCheapEmergencyAttempts += 1;
        const float cellSizeUnits = static_cast<float>(cellCache.CellSizeUnits());
        const Vec2f currentCellCenter = cellCache.CellCenter(enemyCell.x, enemyCell.y);
        const Vec2f nextCenter = flowField.NextCellCenter(enemyCellHash, cellCache);
        const Vec2f emergencyTarget{
            .x = nextCenter.x + flowDirection.x * 0.5F,
            .y = nextCenter.y + flowDirection.y * 0.5F,
        };
        const bool startInsideWallAvoid = game::geometry::IsPointInWall(
            world, enemy.position, GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
        auto isSafeEmergencyTarget = [&](const Vec2f& target) {
            if (!game::geometry::IsPointInsideMaze(
                    world, target, GameplayConstants::kTankCollisionRadiusUnits)) {
                return false;
            }
            if (game::geometry::IsPointInUndestroyedBase(
                    world, target, GameplayConstants::kTankCollisionRadiusUnits)) {
                return false;
            }
            if (game::geometry::IsPointInWall(
                    world, target, GameplayConstants::kEnemyWallAvoidanceRadiusUnits)) {
                return false;
            }
            return true;
        };
        Vec2f chosenTarget = emergencyTarget;
        if (startInsideWallAvoid) {
            // Prefer nearest guaranteed-safe points when already embedded in wall clearance.
            // This gives stage-2 a deterministic local "escape first, then resume flow" behavior.
            const std::array<Vec2f, 3> candidates{{
                currentCellCenter,
                nextCenter,
                emergencyTarget,
            }};
            float bestDistanceSq = 0.0F;
            bool foundSafeTarget = false;
            for (const Vec2f& candidate : candidates) {
                if (!isSafeEmergencyTarget(candidate)) {
                    continue;
                }
                const float distSq = DistanceSq(enemy.position, candidate);
                if (!foundSafeTarget || distSq < bestDistanceSq) {
                    chosenTarget = candidate;
                    bestDistanceSq = distSq;
                    foundSafeTarget = true;
                }
            }
            if (!foundSafeTarget) {
                chosenTarget = emergencyTarget;
            }
        }
        auto logEmergencyBlocked = [&](const char* reason) {
            bool insideWallTank = game::geometry::IsPointInWall(
                world, enemy.position, GameplayConstants::kTankCollisionRadiusUnits);
            bool insideWallAvoidance = game::geometry::IsPointInWall(
                world, enemy.position, GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
            bool insideBase = game::geometry::IsPointInUndestroyedBase(
                world, enemy.position, GameplayConstants::kTankCollisionRadiusUnits);
            bool insideMaze = game::geometry::IsPointInsideMaze(
                world, enemy.position, GameplayConstants::kTankCollisionRadiusUnits);
            int overlapCount = 0;
            float nearestEnemyDistance = 9999.0F;
            for (std::size_t i = 0; i < world.enemies.size(); ++i) {
                if (static_cast<int>(i) == enemyIndex) {
                    continue;
                }
                const EnemyTank& other = world.enemies[i];
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

            const float toTargetHeading = core::angle::QuantizeToEightDirections(
                std::atan2(chosenTarget.x - enemy.position.x, -(chosenTarget.y - enemy.position.y)));
            const float clearAhead = game::geometry::FreeDistanceAhead(
                world,
                enemy.position,
                toTargetHeading,
                6.0F,
                GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                1.0F);
            const float clearLeft = game::geometry::FreeDistanceAhead(
                world,
                enemy.position,
                core::angle::NormalizeAngle(toTargetHeading - (kPi * 0.5F)),
                6.0F,
                GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                1.0F);
            const float clearRight = game::geometry::FreeDistanceAhead(
                world,
                enemy.position,
                core::angle::NormalizeAngle(toTargetHeading + (kPi * 0.5F)),
                6.0F,
                GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                1.0F);

            bolt::log::Profile(
                "[ENEMY_ASSASSIN_EMERGENCY_BLOCKED_DIAG] id=%d reason=%s pos=(%.3f,%.3f) "
                "target=(%.3f,%.3f) cell=(%d,%d) cellHash=%d nextHash=%d "
                "insideMaze=%d insideWallTank=%d insideWallAvoid=%d insideBase=%d "
                "overlapCount=%d nearestEnemyDist=%.3f clear{ahead=%.3f left=%.3f right=%.3f}\n",
                enemyIndex,
                reason,
                enemy.position.x,
                enemy.position.y,
                chosenTarget.x,
                chosenTarget.y,
                enemyCell.x,
                enemyCell.y,
                enemyCellHash,
                nextCellHash,
                insideMaze ? 1 : 0,
                insideWallTank ? 1 : 0,
                insideWallAvoidance ? 1 : 0,
                insideBase ? 1 : 0,
                overlapCount,
                nearestEnemyDistance,
                clearAhead,
                clearLeft,
                clearRight);
        };
        const bool targetInsideWallAvoid = game::geometry::IsPointInWall(
            world, chosenTarget, GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
        const float targetDistance = Distance(enemy.position, chosenTarget);
        const bool allowEmbeddedEscapeCrossing = startInsideWallAvoid &&
            !targetInsideWallAvoid &&
            targetDistance <= (cellSizeUnits * 1.5F);
        if (game::geometry::SegmentIntersectsWall(
                world,
                enemy.position,
                chosenTarget,
                GameplayConstants::kEnemyWallAvoidanceRadiusUnits) &&
            !allowEmbeddedEscapeCrossing) {
            gEnemyRuntimeWindowStats.navFlowMisses += 1;
            logFail("emergency_fallback_blocked", CheapSegmentFailReason::EmergencyFallbackBlocked);
            logEmergencyBlocked("segment_intersects_wall");
            return false;
        }
        const Vec2f toTarget{
            .x = chosenTarget.x - enemy.position.x,
            .y = chosenTarget.y - enemy.position.y,
        };
        const Vec2f dir = NormalizeOrZero(toTarget);
        if (dir.x == 0.0F && dir.y == 0.0F) {
            gEnemyRuntimeWindowStats.navFlowMisses += 1;
            logFail("emergency_fallback_blocked", CheapSegmentFailReason::EmergencyFallbackBlocked);
            logEmergencyBlocked("zero_direction_to_target");
            return false;
        }
        const float heading = core::angle::QuantizeToEightDirections(std::atan2(dir.x, -dir.y));
        commitSuccess(chosenTarget, heading);
        gEnemyRuntimeWindowStats.assassinCheapEmergencySuccesses += 1;
        bolt::log::Profile(
            "[ENEMY_ASSASSIN_SEGMENT_RECOVER] id=%d stage=%d method=emergency "
            "pos=(%.3f,%.3f) target=(%.3f,%.3f) cellHash=%d nextHash=%d "
            "startInsideWallAvoid=%d embeddedCrossing=%d\n",
            enemyIndex,
            methodStage,
            enemy.position.x,
            enemy.position.y,
            chosenTarget.x,
            chosenTarget.y,
            enemyCellHash,
            nextCellHash,
            startInsideWallAvoid ? 1 : 0,
            allowEmbeddedEscapeCrossing ? 1 : 0);
        return true;
    };
    if (methodStage >= 2) {
        return tryEmergencyFallback();
    }

    const bool hasPreviousFlow = enemy.expectedPathCellHash >= 0;
    if (hasPreviousFlow) {
        const float previousFlowHeading = enemy.cachedFlowHeadingRadians;
        const float delta = core::angle::SignedAngleDelta(previousFlowHeading, flowHeading);
        const float absDelta = std::fabs(delta);
        constexpr float kTurnEpsilon = 0.001F;
        const bool sameDirection = absDelta <= kTurnEpsilon;
        const bool perpendicularTurn = std::fabs(absDelta - (kPi * 0.5F)) <= kTurnEpsilon;

        if (perpendicularTurn) {
            const float turnSign = (delta >= 0.0F) ? 1.0F : -1.0F;
            segmentHeading = core::angle::QuantizeToEightDirections(
                previousFlowHeading + turnSign * kEightDirectionStep);
            const Vec2f diagonalDirection = core::angle::DirectionFromHeading(segmentHeading);
            const Vec2f cellCenter = cellCache.CellCenter(enemyCell.x, enemyCell.y);
            const Vec2f destinationEdgeMidpoint{
                .x = cellCenter.x +
                    flowDirection.x * (static_cast<float>(cellCache.CellSizeUnits()) * 0.5F),
                .y = cellCenter.y +
                    flowDirection.y * (static_cast<float>(cellCache.CellSizeUnits()) * 0.5F),
            };
            segmentTarget = Vec2f{
                .x = destinationEdgeMidpoint.x +
                    diagonalDirection.x * kAssassinCheapSegmentOutOfCellUnits,
                .y = destinationEdgeMidpoint.y +
                    diagonalDirection.y * kAssassinCheapSegmentOutOfCellUnits,
            };

            const Vec2f toTarget{
                .x = segmentTarget.x - enemy.position.x,
                .y = segmentTarget.y - enemy.position.y,
            };
            const Vec2f targetDir = NormalizeOrZero(toTarget);
            if (targetDir.x == 0.0F || targetDir.y == 0.0F ||
                targetDir.x * diagonalDirection.x + targetDir.y * diagonalDirection.y < 0.95F) {
                if (!BuildSegmentExitPointOnFlowEdge(
                        cellCache,
                        enemyCell,
                        enemy.position,
                        diagonalDirection,
                        flowDirection,
                        kAssassinCheapSegmentOutOfCellUnits,
                        cornerMarginUnits,
                        edgeToleranceUnits,
                        segmentTarget)) {
                    const Vec2f destEdgeMid{
                        .x = cellCenter.x +
                            flowDirection.x * (static_cast<float>(cellCache.CellSizeUnits()) * 0.5F),
                        .y = cellCenter.y +
                            flowDirection.y * (static_cast<float>(cellCache.CellSizeUnits()) * 0.5F),
                    };
                    const Vec2f toMidpoint{
                        .x = destEdgeMid.x - enemy.position.x,
                        .y = destEdgeMid.y - enemy.position.y,
                    };
                    if (toMidpoint.x * toMidpoint.x + toMidpoint.y * toMidpoint.y <= 0.000001F) {
                        gEnemyRuntimeWindowStats.navFlowMisses += 1;
                        logFail("perpendicular_turn_midpoint_too_close", CheapSegmentFailReason::MidpointTooClose);
                        return false;
                    }
                    segmentHeading = core::angle::QuantizeToEightDirections(
                        std::atan2(toMidpoint.x, -toMidpoint.y));
                    const Vec2f centerBiasedDirection = methodStage >= 1
                        ? NormalizeOrZero(toMidpoint)
                        : core::angle::DirectionFromHeading(segmentHeading);
                    if (!BuildSegmentExitPointOnFlowEdge(
                            cellCache,
                            enemyCell,
                            enemy.position,
                            centerBiasedDirection,
                            flowDirection,
                            kAssassinCheapSegmentOutOfCellUnits,
                            cornerMarginUnits,
                            edgeToleranceUnits,
                            segmentTarget)) {
                        gEnemyRuntimeWindowStats.navFlowMisses += 1;
                        logFail("perpendicular_turn_edge_exit_failed", CheapSegmentFailReason::EdgeExitFailed);
                        return false;
                    }
                }
            }
        } else {
            segmentHeading = flowHeading;
            if (!BuildSegmentExitPointOnFlowEdge(
                    cellCache,
                    enemyCell,
                    enemy.position,
                    flowDirection,
                    flowDirection,
                    kAssassinCheapSegmentOutOfCellUnits,
                    cornerMarginUnits,
                    edgeToleranceUnits,
                    segmentTarget)) {
                const Vec2f cellCenter = cellCache.CellCenter(enemyCell.x, enemyCell.y);
                const Vec2f destinationEdgeMidpoint{
                    .x = cellCenter.x +
                        flowDirection.x * (static_cast<float>(cellCache.CellSizeUnits()) * 0.5F),
                    .y = cellCenter.y +
                        flowDirection.y * (static_cast<float>(cellCache.CellSizeUnits()) * 0.5F),
                };
                const Vec2f toMidpoint{
                    .x = destinationEdgeMidpoint.x - enemy.position.x,
                    .y = destinationEdgeMidpoint.y - enemy.position.y,
                };
                if (toMidpoint.x * toMidpoint.x + toMidpoint.y * toMidpoint.y <= 0.000001F) {
                    gEnemyRuntimeWindowStats.navFlowMisses += 1;
                    logFail("same_dir_midpoint_too_close", CheapSegmentFailReason::MidpointTooClose);
                    return false;
                }
                segmentHeading = core::angle::QuantizeToEightDirections(
                    std::atan2(toMidpoint.x, -toMidpoint.y));
                const Vec2f centerBiasedDirection = methodStage >= 1
                    ? NormalizeOrZero(toMidpoint)
                    : core::angle::DirectionFromHeading(segmentHeading);
                if (!BuildSegmentExitPointOnFlowEdge(
                        cellCache,
                        enemyCell,
                        enemy.position,
                        centerBiasedDirection,
                        flowDirection,
                        kAssassinCheapSegmentOutOfCellUnits,
                        cornerMarginUnits,
                        edgeToleranceUnits,
                        segmentTarget)) {
                    gEnemyRuntimeWindowStats.navFlowMisses += 1;
                    logFail("same_dir_edge_exit_failed", CheapSegmentFailReason::EdgeExitFailed);
                    return false;
                }
            }
            (void)sameDirection;
        }
    } else {
        segmentHeading = flowHeading;
        if (!BuildSegmentExitPointOnFlowEdge(
                cellCache,
                enemyCell,
                enemy.position,
                flowDirection,
                flowDirection,
                kAssassinCheapSegmentOutOfCellUnits,
                cornerMarginUnits,
                edgeToleranceUnits,
                segmentTarget)) {
            const Vec2f cellCenter = cellCache.CellCenter(enemyCell.x, enemyCell.y);
            const Vec2f destinationEdgeMidpoint{
                .x = cellCenter.x +
                    flowDirection.x * (static_cast<float>(cellCache.CellSizeUnits()) * 0.5F),
                .y = cellCenter.y +
                    flowDirection.y * (static_cast<float>(cellCache.CellSizeUnits()) * 0.5F),
            };
            const Vec2f toMidpoint{
                .x = destinationEdgeMidpoint.x - enemy.position.x,
                .y = destinationEdgeMidpoint.y - enemy.position.y,
            };
            if (toMidpoint.x * toMidpoint.x + toMidpoint.y * toMidpoint.y <= 0.000001F) {
                gEnemyRuntimeWindowStats.navFlowMisses += 1;
                logFail("initial_midpoint_too_close", CheapSegmentFailReason::MidpointTooClose);
                return false;
            }
            segmentHeading = core::angle::QuantizeToEightDirections(
                std::atan2(toMidpoint.x, -toMidpoint.y));
            const Vec2f centerBiasedDirection = methodStage >= 1
                ? NormalizeOrZero(toMidpoint)
                : core::angle::DirectionFromHeading(segmentHeading);
            if (!BuildSegmentExitPointOnFlowEdge(
                    cellCache,
                    enemyCell,
                    enemy.position,
                    centerBiasedDirection,
                    flowDirection,
                    kAssassinCheapSegmentOutOfCellUnits,
                    cornerMarginUnits,
                    edgeToleranceUnits,
                    segmentTarget)) {
                gEnemyRuntimeWindowStats.navFlowMisses += 1;
                logFail("initial_edge_exit_failed", CheapSegmentFailReason::EdgeExitFailed);
                return false;
            }
        }
    }

    if (game::geometry::SegmentIntersectsWall(
            world,
            enemy.position,
            segmentTarget,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits)) {
        gEnemyRuntimeWindowStats.navFlowMisses += 1;
        logFail("segment_intersects_wall", CheapSegmentFailReason::SegmentIntersectsWall);
        return false;
    }

    commitSuccess(segmentTarget, segmentHeading);
    return true;
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
    outHeadingRadians = core::angle::QuantizeToEightDirections(
        std::atan2(static_cast<float>(flowDx), -static_cast<float>(flowDy)));
    return true;
}
