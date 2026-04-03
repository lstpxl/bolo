#include "game/systems/EnemyHunter.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/AngleMath.h"
#include "core/Math.h"
#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"
#include "game/systems/EnemySystemHelpers.h"

namespace
{

constexpr float kEightDirectionStep = 3.14159265358979323846F / 4.0F;
constexpr float kHunterPathReachedDistanceSq = 0.04F;
constexpr float kHeadingPenaltyPerTurnStep = 0.2F;
constexpr float kScoutHeadingSwitchHysteresisRadians = 0.12F;
constexpr float kDiagonalAdjustRadiusUnits = 2.0F;
constexpr float kEnemyPlanningClearanceScale = 1.5F;
constexpr int kDirectionCount = 8;
constexpr std::array<int, kDirectionCount> kDirectionDx{0, 1, 1, 1, 0, -1, -1, -1};
constexpr std::array<int, kDirectionCount> kDirectionDy{-1, -1, 0, 1, 1, 1, 0, -1};

int HeadingRadiansToDirIndex(float headingRadians)
{
    const float normalized = core::angle::NormalizeAngle(headingRadians);
    const int step = static_cast<int>(std::round(normalized / kEightDirectionStep));
    return (step % kDirectionCount + kDirectionCount) % kDirectionCount;
}

float DirIndexToHeadingRadians(int dirIndex)
{
    return static_cast<float>((dirIndex % kDirectionCount + kDirectionCount) % kDirectionCount) *
           kEightDirectionStep;
}

float QuantizeScoutHeadingWithHysteresis(float rawHeading, float previousHeading)
{
    const int previousDirIndex = HeadingRadiansToDirIndex(previousHeading);
    const int quantizedDirIndex = HeadingRadiansToDirIndex(rawHeading);
    if (quantizedDirIndex == previousDirIndex) {
        return DirIndexToHeadingRadians(quantizedDirIndex);
    }

    const float previousDirHeading = DirIndexToHeadingRadians(previousDirIndex);
    const float angleDeltaFromPrevious =
        std::fabs(core::angle::SignedAngleDelta(previousDirHeading, rawHeading));
    const float switchThreshold =
        (kEightDirectionStep * 0.5F) + kScoutHeadingSwitchHysteresisRadians;
    if (angleDeltaFromPrevious <= switchThreshold) {
        return previousDirHeading;
    }
    return DirIndexToHeadingRadians(quantizedDirIndex);
}

int RelativeHeadingStepsInt(int fromDirIndex, int toDirIndex)
{
    const int delta = ((toDirIndex - fromDirIndex) % kDirectionCount + kDirectionCount) % kDirectionCount;
    const int steps = delta <= 4 ? delta : kDirectionCount - delta;
    return steps;
}

bool IsCellBlockedByAliveBase(
    const WorldState& world, const game::navigation::CellCoordCache& cellCache, int cellX, int cellY)
{
    for (const EnemyBase& base : world.enemyBases) {
        if (base.destroyed) {
            continue;
        }
        const game::navigation::MazeCellCoord baseCell = cellCache.WorldToCell(base.position);
        if (baseCell.x == cellX && baseCell.y == cellY) {
            return true;
        }
    }
    return false;
}

bool CanTraverseCardinal(
    const WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    int fromX,
    int fromY,
    int toX,
    int toY)
{
    if (!cellCache.IsValidCell(toX, toY) || IsCellBlockedByAliveBase(world, cellCache, toX, toY)) {
        return false;
    }
    const MazeCell& from =
        world.maze.cells[static_cast<std::size_t>(fromY * world.maze.widthCells + fromX)];
    if (toX == fromX + 1 && toY == fromY) {
        return !from.eastWall;
    }
    if (toX == fromX - 1 && toY == fromY) {
        return !from.westWall;
    }
    if (toY == fromY + 1 && toX == fromX) {
        return !from.southWall;
    }
    if (toY == fromY - 1 && toX == fromX) {
        return !from.northWall;
    }
    return false;
}

bool CanTraverseStep(
    const WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    int fromX,
    int fromY,
    int toX,
    int toY)
{
    if (!cellCache.IsValidCell(toX, toY) || IsCellBlockedByAliveBase(world, cellCache, toX, toY)) {
        return false;
    }
    const int stepDx = toX - fromX;
    const int stepDy = toY - fromY;
    if (std::abs(stepDx) + std::abs(stepDy) == 1) {
        return CanTraverseCardinal(world, cellCache, fromX, fromY, toX, toY);
    }
    if (std::abs(stepDx) != 1 || std::abs(stepDy) != 1) {
        return false;
    }

    const int horizontalX = fromX + stepDx;
    const int horizontalY = fromY;
    const int verticalX = fromX;
    const int verticalY = fromY + stepDy;
    const bool horizontalThenVertical =
        CanTraverseCardinal(world, cellCache, fromX, fromY, horizontalX, horizontalY) &&
        CanTraverseCardinal(world, cellCache, horizontalX, horizontalY, toX, toY);
    const bool verticalThenHorizontal =
        CanTraverseCardinal(world, cellCache, fromX, fromY, verticalX, verticalY) &&
        CanTraverseCardinal(world, cellCache, verticalX, verticalY, toX, toY);
    return horizontalThenVertical || verticalThenHorizontal;
}

int MeasureDirectionalRunCells(
    const WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    const game::navigation::MazeCellCoord& fromCell,
    int dx,
    int dy)
{
    int runCells = 0;
    int currentX = fromCell.x;
    int currentY = fromCell.y;
    while (true) {
        const int nextX = currentX + dx;
        const int nextY = currentY + dy;
        if (!CanTraverseStep(world, cellCache, currentX, currentY, nextX, nextY)) {
            break;
        }
        runCells += 1;
        currentX = nextX;
        currentY = nextY;
    }
    return runCells;
}

bool SegmentIntersectsWall(
    const WorldState& world, const Vec2f& from, const Vec2f& to)
{
    return game::geometry::SegmentIntersectsWall(
        world, from, to, GameplayConstants::kWallClearanceForAvoidance);
}

bool BuildHunterScoutSegments(
    const WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    const game::navigation::MazeCellCoord& fromCell,
    const game::navigation::MazeCellCoord& targetCell,
    const Vec2f& startPosition,
    std::array<Vec2f, 2>& outPoints,
    int& outCount)
{
    outCount = 0;
    const Vec2f targetCenter = cellCache.CellCenter(targetCell.x, targetCell.y);
    if (!SegmentIntersectsWall(world, startPosition, targetCenter) &&
        IsValidSegmentEndpoint(world, targetCenter)) {
        outPoints[0] = targetCenter;
        outCount = 1;
        return true;
    }

    const int moveDx = targetCell.x - fromCell.x;
    const int moveDy = targetCell.y - fromCell.y;
    const bool isDiagonal = std::abs(moveDx) == 1 && std::abs(moveDy) == 1;
    if (!isDiagonal) {
        return false;
    }

    std::array<Vec2f, 9> adjustedTargets{};
    adjustedTargets[0] = targetCenter;
    adjustedTargets[1] = Vec2f{.x = targetCenter.x + kDiagonalAdjustRadiusUnits, .y = targetCenter.y};
    adjustedTargets[2] = Vec2f{.x = targetCenter.x - kDiagonalAdjustRadiusUnits, .y = targetCenter.y};
    adjustedTargets[3] = Vec2f{.x = targetCenter.x, .y = targetCenter.y + kDiagonalAdjustRadiusUnits};
    adjustedTargets[4] = Vec2f{.x = targetCenter.x, .y = targetCenter.y - kDiagonalAdjustRadiusUnits};
    adjustedTargets[5] = Vec2f{
        .x = targetCenter.x + kDiagonalAdjustRadiusUnits * 0.7F,
        .y = targetCenter.y + kDiagonalAdjustRadiusUnits * 0.7F};
    adjustedTargets[6] = Vec2f{
        .x = targetCenter.x + kDiagonalAdjustRadiusUnits * 0.7F,
        .y = targetCenter.y - kDiagonalAdjustRadiusUnits * 0.7F};
    adjustedTargets[7] = Vec2f{
        .x = targetCenter.x - kDiagonalAdjustRadiusUnits * 0.7F,
        .y = targetCenter.y + kDiagonalAdjustRadiusUnits * 0.7F};
    adjustedTargets[8] = Vec2f{
        .x = targetCenter.x - kDiagonalAdjustRadiusUnits * 0.7F,
        .y = targetCenter.y - kDiagonalAdjustRadiusUnits * 0.7F};

    for (const Vec2f& endpoint : adjustedTargets) {
        if (!IsValidSegmentEndpoint(world, endpoint)) {
            continue;
        }
        if (!SegmentIntersectsWall(world, startPosition, endpoint)) {
            outPoints[0] = endpoint;
            outCount = 1;
            return true;
        }
    }

    const game::navigation::MazeCellCoord bendA{.x = fromCell.x + moveDx, .y = fromCell.y};
    const game::navigation::MazeCellCoord bendB{.x = fromCell.x, .y = fromCell.y + moveDy};
    const std::array<game::navigation::MazeCellCoord, 2> bendCells{bendA, bendB};
    for (const game::navigation::MazeCellCoord& bendCell : bendCells) {
        if (!CanTraverseCardinal(world, cellCache, fromCell.x, fromCell.y, bendCell.x, bendCell.y) ||
            !CanTraverseCardinal(world, cellCache, bendCell.x, bendCell.y, targetCell.x, targetCell.y)) {
            continue;
        }
        const Vec2f bendPoint = cellCache.CellCenter(bendCell.x, bendCell.y);
        if (SegmentIntersectsWall(world, startPosition, bendPoint)) {
            continue;
        }
        for (const Vec2f& endpoint : adjustedTargets) {
            if (!IsValidSegmentEndpoint(world, endpoint)) {
                continue;
            }
            if (SegmentIntersectsWall(world, bendPoint, endpoint)) {
                continue;
            }
            outPoints[0] = bendPoint;
            outPoints[1] = endpoint;
            outCount = 2;
            return true;
        }
    }
    return false;
}

void AdvanceReachedHunterScoutPoints(EnemyTank& enemy)
{
    while (enemy.hunterScoutPathActive &&
           enemy.hunterScoutSegmentIndex < enemy.hunterScoutSegmentCount &&
           DistanceSq(
               enemy.position,
               enemy.hunterScoutSegmentPoints[static_cast<std::size_t>(enemy.hunterScoutSegmentIndex)]) <=
               kHunterPathReachedDistanceSq) {
        enemy.hunterScoutSegmentIndex += 1;
    }
    if (enemy.hunterScoutSegmentIndex >= enemy.hunterScoutSegmentCount) {
        enemy.hunterScoutPathActive = false;
        enemy.hunterScoutTargetCellHash = -1;
        enemy.hunterScoutSegmentCount = 0;
        enemy.hunterScoutSegmentIndex = 0;
    }
}

bool EnsureHunterScoutPath(
    const WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    EnemyTank& enemy,
    Random& random)
{
    AdvanceReachedHunterScoutPoints(enemy);
    if (enemy.hunterScoutPathActive && enemy.hunterScoutSegmentIndex < enemy.hunterScoutSegmentCount) {
        return true;
    }

    const game::navigation::MazeCellCoord fromCell = cellCache.WorldToCell(enemy.position);
    const int currentDirIndex = HeadingRadiansToDirIndex(enemy.headingRadians);

    struct DirectionCandidate {
        int directionIndex = 0;
        int runCells = 0;
        int headingStepCost = 0;
        float score = 0.0F;
    };
    std::array<DirectionCandidate, kDirectionCount> candidates{};
    for (int i = 0; i < kDirectionCount; ++i) {
        const int dx = kDirectionDx[static_cast<std::size_t>(i)];
        const int dy = kDirectionDy[static_cast<std::size_t>(i)];
        const int nextCellX = fromCell.x + dx;
        const int nextCellY = fromCell.y + dy;
        const int turnSteps = RelativeHeadingStepsInt(currentDirIndex, i);
        const int runCells = MeasureDirectionalRunCells(world, cellCache, fromCell, dx, dy);
        const float turnWeight =
            std::max(0.0F, 1.0F - kHeadingPenaltyPerTurnStep * static_cast<float>(turnSteps));
        int enemiesInFirstCell = 0;
        if (runCells > 0 && cellCache.IsValidCell(nextCellX, nextCellY)) {
            for (const EnemyTank& other : world.enemies) {
                if (!other.alive) {
                    continue;
                }
                if (other.cellCoord.x == nextCellX && other.cellCoord.y == nextCellY) {
                    enemiesInFirstCell += 1;
                }
            }
        }
        const int decayRunCells = std::clamp(
            runCells,
            core::math::kExpDecayA1K07MinX,
            core::math::kExpDecayA1K07MaxX);
        const int decayFirstCellEnemies = std::clamp(
            enemiesInFirstCell,
            core::math::kExpDecayA1K07MinX,
            core::math::kExpDecayA1K07MaxX);
        const float runDecay = static_cast<float>(core::math::ExpDecayA1K07(decayRunCells));
        const float enemyDecay = static_cast<float>(core::math::ExpDecayA1K07(decayFirstCellEnemies));
        const float weight = (1.0F - runDecay) * enemyDecay * turnWeight;
        candidates[static_cast<std::size_t>(i)] = DirectionCandidate{
            .directionIndex = i,
            .runCells = runCells,
            .headingStepCost = turnSteps,
            .score = weight};
    }
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const DirectionCandidate& a, const DirectionCandidate& b) {
            if (a.headingStepCost != b.headingStepCost) {
                return a.headingStepCost < b.headingStepCost;
            }
            return a.directionIndex < b.directionIndex;
        });

    float bestScore = -1.0F;
    std::array<int, kDirectionCount> bestIndices{};
    int bestCount = 0;
    constexpr float kTieEpsilon = 0.0001F;
    for (const DirectionCandidate& candidate : candidates) {
        if (candidate.runCells <= 0) {
            continue;
        }
        if (candidate.score > bestScore + kTieEpsilon) {
            bestScore = candidate.score;
            bestCount = 0;
            bestIndices[static_cast<std::size_t>(bestCount)] = candidate.directionIndex;
            bestCount += 1;
        } else if (std::fabs(candidate.score - bestScore) <= kTieEpsilon) {
            bestIndices[static_cast<std::size_t>(bestCount)] = candidate.directionIndex;
            bestCount += 1;
        }
    }
    if (bestCount <= 0) {
        return false;
    }

    const int chosenDirectionIndex =
        bestIndices[static_cast<std::size_t>(random.NextInt(0, std::max(0, bestCount - 1)))];
    const int chosenDx = kDirectionDx[static_cast<std::size_t>(chosenDirectionIndex)];
    const int chosenDy = kDirectionDy[static_cast<std::size_t>(chosenDirectionIndex)];
    const game::navigation::MazeCellCoord targetCell{
        .x = fromCell.x + chosenDx,
        .y = fromCell.y + chosenDy,
    };
    if (!CanTraverseStep(world, cellCache, fromCell.x, fromCell.y, targetCell.x, targetCell.y)) {
        return false;
    }

    std::array<Vec2f, 2> points{};
    int pointCount = 0;
    if (!BuildHunterScoutSegments(
            world, cellCache, fromCell, targetCell, enemy.position, points, pointCount)) {
        return false;
    }
    enemy.hunterScoutPathActive = true;
    enemy.hunterScoutTargetCellHash = cellCache.CellHash(targetCell.x, targetCell.y);
    enemy.hunterScoutSegmentPoints = points;
    enemy.hunterScoutSegmentCount = pointCount;
    enemy.hunterScoutSegmentIndex = 0;
    enemy.hunterScoutCachedHeadingRadians = DirIndexToHeadingRadians(chosenDirectionIndex);
    return true;
}

}  // namespace

float SelectScoutHeadingWithFallback(
    const WorldState& world, const EnemyTank& enemy, bool allowNinetyTurns, bool& shouldRotate)
{
    const float lookahead = game::geometry::FreeDistanceAhead(
        world, enemy.position, enemy.headingRadians,
        GameplayConstants::kEnemyLookaheadObstacleUnits,
        GameplayConstants::kWallClearanceForAvoidance, kEnemyPlanningClearanceScale);
    if (lookahead >= GameplayConstants::kEnemyLookaheadObstacleUnits) {
        shouldRotate = false;
        return enemy.headingRadians;
    }

    const std::array<float, 4> turns45{-kEightDirectionStep, kEightDirectionStep, 0.0F, 0.0F};
    const float turned45 = ChooseBestTurnHeading(
        world, enemy.position, enemy.headingRadians, turns45, 2,
        GameplayConstants::kEnemyRequiredClearRunUnits);
    if (!std::isnan(turned45)) {
        shouldRotate = false;
        return turned45;
    }

    if (allowNinetyTurns) {
        const std::array<float, 4> turns90{
            -kEightDirectionStep * 2.0F, kEightDirectionStep * 2.0F, 0.0F, 0.0F};
        const float turned90 = ChooseBestTurnHeading(
            world, enemy.position, enemy.headingRadians, turns90, 2,
            GameplayConstants::kEnemyRequiredClearRunUnits);
        if (!std::isnan(turned90)) {
            shouldRotate = false;
            return turned90;
        }
    }

    shouldRotate = true;
    return enemy.headingRadians;
}

void InvalidateHunterScoutPath(EnemyTank& enemy)
{
    enemy.hunterScoutPathActive = false;
    enemy.hunterScoutTargetCellHash = -1;
    enemy.hunterScoutSegmentCount = 0;
    enemy.hunterScoutSegmentIndex = 0;
}

bool SelectHunterScoutMotion(
    const WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    EnemyTank& enemy,
    Random& random,
    float& outHeadingRadians,
    Vec2f& outTargetPoint,
    bool snapHeadingToEightDirections)
{
    if (!EnsureHunterScoutPath(world, cellCache, enemy, random)) {
        return false;
    }
    if (!enemy.hunterScoutPathActive || enemy.hunterScoutSegmentIndex >= enemy.hunterScoutSegmentCount) {
        return false;
    }

    const Vec2f targetPoint =
        enemy.hunterScoutSegmentPoints[static_cast<std::size_t>(enemy.hunterScoutSegmentIndex)];
    const Vec2f toTarget{
        .x = targetPoint.x - enemy.position.x,
        .y = targetPoint.y - enemy.position.y,
    };
    if (toTarget.x == 0.0F && toTarget.y == 0.0F) {
        return false;
    }
    const float rawHeading = std::atan2(toTarget.x, -toTarget.y);
    if (snapHeadingToEightDirections) {
        outHeadingRadians = QuantizeScoutHeadingWithHysteresis(
            rawHeading, enemy.hunterScoutCachedHeadingRadians);
    } else {
        outHeadingRadians = core::angle::NormalizeAngle(rawHeading);
    }
    outTargetPoint = targetPoint;
    enemy.hunterScoutCachedHeadingRadians = outHeadingRadians;
    return true;
}
