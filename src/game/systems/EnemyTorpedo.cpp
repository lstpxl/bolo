#include "game/systems/EnemyTorpedo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include "core/AngleMath.h"
#include "core/Math.h"
#include "core/Profiling.h"
#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"
#include "game/navigation/AdjacentCellSegmentPlanner.h"
#include "game/systems/EnemySystemHelpers.h"
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
constexpr float kTorpedoFlyWaypointReachedDistanceSq = 0.04F;
constexpr int kDirectionCount = 8;
constexpr std::array<int, kDirectionCount> kDirectionDx{0, 1, 1, 1, 0, -1, -1, -1};
constexpr std::array<int, kDirectionCount> kDirectionDy{-1, -1, 0, 1, 1, 1, 0, -1};

int HeadingRadiansToDirIndex(float headingRadians) {
    const float normalized = core::angle::NormalizeAngle(headingRadians);
    const int step = static_cast<int>(std::round(normalized / kEightDirectionStep));
    return (step % kDirectionCount + kDirectionCount) % kDirectionCount;
}

float DirIndexToHeadingRadians(int dirIndex) {
    return static_cast<float>((dirIndex % kDirectionCount + kDirectionCount) % kDirectionCount) *
        kEightDirectionStep;
}

int RelativeHeadingStepsInt(int fromDirIndex, int toDirIndex) {
    const int delta = ((toDirIndex - fromDirIndex) % kDirectionCount + kDirectionCount) % kDirectionCount;
    return delta <= 4 ? delta : kDirectionCount - delta;
}

bool IsCellBlockedByAliveBase(
    const WorldState& world, const game::navigation::CellCoordCache& cellCache, int cellX, int cellY) {
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
    int toY) {
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
    int toY) {
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
    int dy) {
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

void AdvanceReachedTorpedoFlyPoints(EnemyTank& enemy) {
    while (enemy.torpedoFlyPathActive &&
           enemy.torpedoFlySegmentIndex < enemy.torpedoFlySegmentCount - 1 &&
           DistanceSq(
               enemy.position,
               enemy.torpedoFlySegmentPoints[static_cast<std::size_t>(enemy.torpedoFlySegmentIndex)]) <=
               kTorpedoFlyWaypointReachedDistanceSq) {
        enemy.torpedoFlySegmentIndex += 1;
    }
}

bool EnsureTorpedoFlyPath(
    const WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    EnemyTank& enemy,
    Random& random) {
    AdvanceReachedTorpedoFlyPoints(enemy);
    const game::navigation::MazeCellCoord currentCell = cellCache.WorldToCell(enemy.position);
    const int currentCellHash = cellCache.CellHash(currentCell.x, currentCell.y);
    // Replan when the torpedo enters the target cell for the currently active final segment.
    if (enemy.torpedoFlyPathActive && enemy.torpedoFlyTargetCellHash >= 0 &&
        enemy.torpedoFlySegmentCount > 0 &&
        enemy.torpedoFlySegmentIndex == enemy.torpedoFlySegmentCount - 1 &&
        currentCellHash == enemy.torpedoFlyTargetCellHash) {
        InvalidateTorpedoFlyPath(enemy);
    }
    if (enemy.torpedoFlyPathActive && enemy.torpedoFlySegmentIndex < enemy.torpedoFlySegmentCount) {
        return true;
    }

    const game::navigation::MazeCellCoord fromCell = currentCell;
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
        // Turn weight must be highest at turnSteps==0 (straight). Do not use (1 - ExpDecayA1K09(t)):
        // ExpDecayA1K09(0)==0.9 so that form gives ~0.1 for straight and ~0.95 for large turns — inverted.
        const int decayTurnSteps = std::clamp(
            turnSteps,
            core::math::kExpDecayA1K07MinX,
            core::math::kExpDecayA1K07MaxX);
        const float turnWeight = static_cast<float>(core::math::ExpDecayA1K09(decayTurnSteps));
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
    std::sort(candidates.begin(), candidates.end(), [](const DirectionCandidate& a, const DirectionCandidate& b) {
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
    if (!game::navigation::AdjacentCellSegmentPlanner::Build(
            world, cellCache, fromCell, targetCell, enemy.position, points, pointCount)) {
        return false;
    }
    enemy.torpedoFlyPathActive = true;
    enemy.torpedoFlyTargetCellHash = cellCache.CellHash(targetCell.x, targetCell.y);
    enemy.torpedoFlySegmentPoints = points;
    enemy.torpedoFlySegmentCount = pointCount;
    enemy.torpedoFlySegmentIndex = 0;
    enemy.torpedoFlyCachedHeadingRadians = DirIndexToHeadingRadians(chosenDirectionIndex);
    return true;
}

}  // namespace

bool PlayerAheadForTorpedo(const EnemyTank& enemy, const Vec2f& toPlayerNormalized) {
    const Vec2f forward = core::angle::DirectionFromHeading(enemy.headingRadians);
    return game::geometry::IsWithinForwardCone2D(
        forward, toPlayerNormalized, 1.0F, kCosThirtyDegrees);
}

bool PlayerAheadForTorpedoRam(const EnemyTank& enemy, const Vec2f& toPlayerNormalized) {
    const Vec2f forward = core::angle::DirectionFromHeading(enemy.headingRadians);
    return game::geometry::IsWithinForwardCone2D(
        forward, toPlayerNormalized, 1.0F, kCosTwentyDegrees);
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

void InvalidateTorpedoFlyPath(EnemyTank& enemy) {
    enemy.torpedoFlyPathActive = false;
    enemy.torpedoFlyTargetCellHash = -1;
    enemy.torpedoFlySegmentCount = 0;
    enemy.torpedoFlySegmentIndex = 0;
}

bool SelectTorpedoFlyMotion(
    const WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    EnemyTank& enemy,
    Random& random,
    float& outHeadingRadians,
    Vec2f& outTargetPoint,
    bool snapHeadingToEightDirections) {
    if (!EnsureTorpedoFlyPath(world, cellCache, enemy, random)) {
        return false;
    }
    if (!enemy.torpedoFlyPathActive || enemy.torpedoFlySegmentIndex >= enemy.torpedoFlySegmentCount) {
        return false;
    }

    const Vec2f targetPoint =
        enemy.torpedoFlySegmentPoints[static_cast<std::size_t>(enemy.torpedoFlySegmentIndex)];
    const Vec2f toTarget{
        .x = targetPoint.x - enemy.position.x,
        .y = targetPoint.y - enemy.position.y,
    };
    if (toTarget.x == 0.0F && toTarget.y == 0.0F) {
        return false;
    }
    const float rawHeading = std::atan2(toTarget.x, -toTarget.y);
    if (snapHeadingToEightDirections) {
        const int movementDirIndex = HeadingRadiansToDirIndex(rawHeading);
        outHeadingRadians = DirIndexToHeadingRadians(movementDirIndex);
    } else {
        outHeadingRadians = core::angle::NormalizeAngle(rawHeading);
    }
    outTargetPoint = targetPoint;
    enemy.torpedoFlyCachedHeadingRadians = outHeadingRadians;
    return true;
}

float SelectTorpedoMoveHeading(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    EnemyTank& enemy,
    Random& random,
    bool& startRetreat,
    bool& decidedStraight,
    const game::spatial::EnemyCellOccupancy* rayQueryOccupancy,
    std::vector<int>* candidateIndicesScratch,
    std::vector<std::uint32_t>* raySeenMarksScratch,
    std::uint32_t* raySeenEpochScratch) {
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
        rayQueryOccupancy,
        candidateIndicesScratch,
        raySeenMarksScratch,
        raySeenEpochScratch);
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
        rayQueryOccupancy,
        candidateIndicesScratch,
        raySeenMarksScratch,
        raySeenEpochScratch);
    const float rightClear = game::geometry::FreeDistanceAheadWithEnemies(
        world,
        enemies,
        selfIndex,
        enemy.position,
        rightHeading,
        kSegmentBuildProbeMaxUnits,
        GameplayConstants::kWallClearanceForAvoidance,
        kEnemyPlanningClearanceScale,
        rayQueryOccupancy,
        candidateIndicesScratch,
        raySeenMarksScratch,
        raySeenEpochScratch);

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
    enemy.torpedoStraightDistanceSinceTurnUnits = 0.0F;
    decidedStraight = chosen.heading == straightHeading;

    return chosen.heading;
}

void EnterTorpedoTargetingMode(EnemyTank& enemy) {
    enemy.aiMode = EnemyAiMode::Targeting;
    InvalidateTorpedoFlyPath(enemy);
}

void EnterTorpedoRotateMode(EnemyTank& enemy) {
    enemy.aiMode = EnemyAiMode::Rotate;
    enemy.torpedoRotateTargetHeadingRadians = enemy.torpedoChosenHeadingRadians;
    InvalidateTorpedoFlyPath(enemy);
}

float UpdateTorpedoRotateHeading(EnemyTank& enemy, float deltaSeconds) {
    const float rotateStep =
        GameplayConstants::kTorpedoFullTierTurnSpeedRadiansPerSecond * deltaSeconds;
    const float signedDelta = core::angle::SignedAngleDelta(
        enemy.headingRadians, enemy.torpedoRotateTargetHeadingRadians);
    if (std::fabs(signedDelta) <= rotateStep + 0.0001F) {
        const float heading =
            core::angle::NormalizeAngle(enemy.torpedoRotateTargetHeadingRadians);
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
