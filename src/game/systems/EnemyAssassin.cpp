#include "game/systems/EnemyAssassin.h"

#include <cmath>
#include "core/AngleMath.h"
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
    if (alongEdgeCoord < alongEdgeMin || alongEdgeCoord > alongEdgeMax) {
        return false;
    }

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
    EnemyTank& enemy) {
    if (!flowField.HasBuild()) {
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

    const float flowHeading = core::angle::QuantizeToEightDirections(
        std::atan2(static_cast<float>(flowDx), -static_cast<float>(flowDy)));
    const Vec2f flowDirection = core::angle::DirectionFromHeading(flowHeading);
    float segmentHeading = flowHeading;
    Vec2f segmentTarget{};

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
                        kAssassinCheapSegmentCornerMarginUnits,
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
                        return false;
                    }
                    segmentHeading = core::angle::QuantizeToEightDirections(
                        std::atan2(toMidpoint.x, -toMidpoint.y));
                    const Vec2f centerBiasedDirection = core::angle::DirectionFromHeading(segmentHeading);
                    if (!BuildSegmentExitPointOnFlowEdge(
                            cellCache,
                            enemyCell,
                            enemy.position,
                            centerBiasedDirection,
                            flowDirection,
                            kAssassinCheapSegmentOutOfCellUnits,
                            kAssassinCheapSegmentCornerMarginUnits,
                            segmentTarget)) {
                        gEnemyRuntimeWindowStats.navFlowMisses += 1;
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
                    kAssassinCheapSegmentCornerMarginUnits,
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
                    return false;
                }
                segmentHeading = core::angle::QuantizeToEightDirections(
                    std::atan2(toMidpoint.x, -toMidpoint.y));
                const Vec2f centerBiasedDirection = core::angle::DirectionFromHeading(segmentHeading);
                if (!BuildSegmentExitPointOnFlowEdge(
                        cellCache,
                        enemyCell,
                        enemy.position,
                        centerBiasedDirection,
                        flowDirection,
                        kAssassinCheapSegmentOutOfCellUnits,
                        kAssassinCheapSegmentCornerMarginUnits,
                        segmentTarget)) {
                    gEnemyRuntimeWindowStats.navFlowMisses += 1;
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
                kAssassinCheapSegmentCornerMarginUnits,
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
                return false;
            }
            segmentHeading = core::angle::QuantizeToEightDirections(
                std::atan2(toMidpoint.x, -toMidpoint.y));
            const Vec2f centerBiasedDirection = core::angle::DirectionFromHeading(segmentHeading);
            if (!BuildSegmentExitPointOnFlowEdge(
                    cellCache,
                    enemyCell,
                    enemy.position,
                    centerBiasedDirection,
                    flowDirection,
                    kAssassinCheapSegmentOutOfCellUnits,
                    kAssassinCheapSegmentCornerMarginUnits,
                    segmentTarget)) {
                gEnemyRuntimeWindowStats.navFlowMisses += 1;
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
