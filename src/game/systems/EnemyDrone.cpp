#include "game/systems/EnemyDrone.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include "core/AngleMath.h"
#include "core/ExpDecayA1K02.h"
#include "core/ExpDecayA1K07.h"
#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"
#include "game/navigation/BaseFlowField.h"
#include "game/systems/EnemySystemHelpers.h"

namespace {

constexpr float kEightDirectionStep = 3.14159265358979323846F / 4.0F;
constexpr float kDroneReturnRequiredClearRunUnits = 6.0F;
constexpr float kEnemyPlanningClearanceScale = 1.5F;
constexpr int kDirectionCount = 8;
constexpr std::array<int, kDirectionCount> kDirectionDx{0, 1, 1, 1, 0, -1, -1, -1};
constexpr std::array<int, kDirectionCount> kDirectionDy{-1, -1, 0, 1, 1, 1, 0, -1};

void EnsureBaseDistanceAndFlowBuilt(WorldState& world) {
    NavigationRuntimeCache& nav = world.navigationCache;
    nav.cellCoords.ConfigureFromMaze(world.maze);
    nav.baseDistanceField.EnsureCapacity(world.maze);
    if (!nav.baseDistanceField.HasBuild()) {
        nav.baseDistanceField.Rebuild(world.maze, nav.cellCoords, world.enemyBases);
    }
    nav.baseFlowField.EnsureCapacity(world.maze);
    if (!nav.baseFlowField.HasBuild()) {
        nav.baseFlowField.Rebuild(world.maze, nav.cellCoords, nav.baseDistanceField);
    }
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

int RelativeHeadingStepsInt(int fromDirIndex, int toDirIndex)
{
    const int delta = ((toDirIndex - fromDirIndex) % kDirectionCount + kDirectionCount) % kDirectionCount;
    const int steps = delta <= 4 ? delta : kDirectionCount - delta;
    return steps;
}

float SelectDroneResetHeading(WorldState& world, EnemyTank& enemy, Random& random)
{
    EnsureBaseDistanceAndFlowBuilt(world);
    const game::navigation::CellCoordCache& cellCache = world.navigationCache.cellCoords;
    const game::navigation::BaseFlowField& baseFlow = world.navigationCache.baseFlowField;
    const game::navigation::MazeCellCoord fromCell = cellCache.WorldToCell(enemy.position);
    const int currentDirIndex = HeadingRadiansToDirIndex(enemy.headingRadians);

    int flowDirIndex = -1;
    if (baseFlow.HasBuild() && cellCache.IsValidCell(fromCell.x, fromCell.y)) {
        const int cellHash = cellCache.CellHash(fromCell.x, fromCell.y);
        const int nextHash = baseFlow.NextCellHash(cellHash);
        if (nextHash >= 0 && cellCache.WidthCells() > 0) {
            const int nextX = nextHash % cellCache.WidthCells();
            const int nextY = nextHash / cellCache.WidthCells();
            const int fdx = nextX - fromCell.x;
            const int fdy = nextY - fromCell.y;
            for (int i = 0; i < kDirectionCount; ++i) {
                if (kDirectionDx[static_cast<std::size_t>(i)] == fdx &&
                    kDirectionDy[static_cast<std::size_t>(i)] == fdy) {
                    flowDirIndex = i;
                    break;
                }
            }
        }
    }

    std::array<float, kDirectionCount> weights{};
    float totalWeight = 0.0F;
    for (int i = 0; i < kDirectionCount; ++i) {
        const int dx = kDirectionDx[static_cast<std::size_t>(i)];
        const int dy = kDirectionDy[static_cast<std::size_t>(i)];
        const int runCells = MeasureDirectionalRunCells(world, cellCache, fromCell, dx, dy);
        if (runCells <= 0) {
            weights[static_cast<std::size_t>(i)] = 0.0F;
            continue;
        }
        const int turnSteps = RelativeHeadingStepsInt(currentDirIndex, i);
        const int decayTurnSteps = std::clamp(
            turnSteps,
            core::math::kExpDecayA1K02MinX,
            core::math::kExpDecayA1K02MaxX);
        const float turnWeight = static_cast<float>(core::math::ExpDecayA1K02(decayTurnSteps));
        int enemiesInFirstCell = 0;
        const int nextCellX = fromCell.x + dx;
        const int nextCellY = fromCell.y + dy;
        if (cellCache.IsValidCell(nextCellX, nextCellY)) {
            for (const EnemyTank& other : world.enemies) {
                if (!other.alive) {
                    continue;
                }
                if (other.cellCoord.x == nextCellX && other.cellCoord.y == nextCellY) {
                    enemiesInFirstCell += 1;
                }
            }
        }
        float flowWeight = 1.0F;
        if (flowDirIndex >= 0) {
            const int flowTurnSteps = RelativeHeadingStepsInt(i, flowDirIndex);
            const int decayFlowTurnSteps = std::clamp(
                flowTurnSteps,
                core::math::kExpDecayA1K07MinX,
                core::math::kExpDecayA1K07MaxX);
            flowWeight = static_cast<float>(core::math::ExpDecayA1K07(decayFlowTurnSteps));
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
        const float weight = (1.0F - runDecay) * enemyDecay * turnWeight * flowWeight;
        weights[static_cast<std::size_t>(i)] = weight;
        totalWeight += weight;
    }

    int chosenIndex = 0;
    if (totalWeight > 1.0e-6F) {
        const float pick = random.NextFloat(0.0F, totalWeight);
        float cumulative = 0.0F;
        for (int i = 0; i < kDirectionCount; ++i) {
            cumulative += weights[static_cast<std::size_t>(i)];
            if (pick <= cumulative || i == kDirectionCount - 1) {
                chosenIndex = i;
                break;
            }
        }
    } else {
        std::array<int, kDirectionCount> runIndices{};
        int runCount = 0;
        for (int i = 0; i < kDirectionCount; ++i) {
            const int dx = kDirectionDx[static_cast<std::size_t>(i)];
            const int dy = kDirectionDy[static_cast<std::size_t>(i)];
            if (MeasureDirectionalRunCells(world, cellCache, fromCell, dx, dy) > 0) {
                runIndices[static_cast<std::size_t>(runCount)] = i;
                ++runCount;
            }
        }
        if (runCount > 0) {
            chosenIndex = runIndices[static_cast<std::size_t>(random.NextInt(0, runCount - 1))];
        } else {
            chosenIndex = random.NextInt(0, kDirectionCount - 1);
        }
    }

    return core::angle::QuantizeToEightDirections(DirIndexToHeadingRadians(chosenIndex));
}

}  // namespace

bool DroneIsFarEnoughForReturnToBase(WorldState& world, const Vec2f& position) {
    EnsureBaseDistanceAndFlowBuilt(world);
    const game::navigation::CellCoordCache& cellCache = world.navigationCache.cellCoords;
    const game::navigation::BaseDistanceField& baseDistance = world.navigationCache.baseDistanceField;
    if (!baseDistance.HasBuild()) {
        return false;
    }
    const game::navigation::MazeCellCoord cell = cellCache.WorldToCell(position);
    if (!cellCache.IsValidCell(cell.x, cell.y)) {
        return false;
    }
    const int graphDist = baseDistance.DistanceAtCell(cell.x, cell.y);
    if (graphDist == std::numeric_limits<int>::max()) {
        return false;
    }
    return graphDist >= GameplayConstants::kDroneReturnToBaseMinBaseDistanceCells;
}

bool DroneTryHeadingTowardBaseAlongFlow(WorldState& world, const Vec2f& position, float& outHeading) {
    EnsureBaseDistanceAndFlowBuilt(world);
    const game::navigation::CellCoordCache& cellCache = world.navigationCache.cellCoords;
    const game::navigation::BaseFlowField& baseFlow = world.navigationCache.baseFlowField;
    if (!baseFlow.HasBuild()) {
        return false;
    }
    const game::navigation::MazeCellCoord cell = cellCache.WorldToCell(position);
    if (!cellCache.IsValidCell(cell.x, cell.y)) {
        return false;
    }
    const int cellHash = cellCache.CellHash(cell.x, cell.y);
    if (baseFlow.NextCellHash(cellHash) < 0) {
        return false;
    }
    const Vec2f nextCenter = baseFlow.NextCellCenter(cellHash, cellCache);
    const Vec2f toNext{
        .x = nextCenter.x - position.x,
        .y = nextCenter.y - position.y,
    };
    if (std::fabs(toNext.x) <= 0.001F && std::fabs(toNext.y) <= 0.001F) {
        return false;
    }
    outHeading = core::angle::QuantizeToEightDirections(std::atan2(toNext.x, -toNext.y));
    return true;
}

void EnterDroneWatchMode(WorldState& world, EnemyTank& enemy, Random& random) {
    enemy.droneWatchAlignToHeading = false;
    enemy.aiMode = EnemyAiMode::Watch;
    enemy.aiModeElapsedSeconds = 0.0F;
    enemy.watchRotateDirection = RandomRotateDirection(random);
    enemy.returnToBase = DroneIsFarEnoughForReturnToBase(world, enemy.position);
}

void TryDroneSelfAwarenessReset(WorldState& world, EnemyTank& enemy, Random& random) {
    if (enemy.type != EnemyType::Drone) {
        return;
    }
    if (!DroneIsFarEnoughForReturnToBase(world, enemy.position)) {
        return;
    }
    float headingToBase = 0.0F;
    if (!DroneTryHeadingTowardBaseAlongFlow(world, enemy.position, headingToBase)) {
        return;
    }
    const float relativeBearing =
        core::angle::AngleDistance(enemy.headingRadians, headingToBase);
    if (relativeBearing >= GameplayConstants::kDroneSelfAwarenessOffFlowBearingThresholdRadians) {
        enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        DroneReset(world, enemy, random);
    }
}

void DroneReset(WorldState& world, EnemyTank& enemy, Random& random) {
    const float heading = SelectDroneResetHeading(world, enemy, random);

    enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
    enemy.offscreenSegmentActive = false;
    if (enemy.simTier == EnemySimTier::Cheap) {
        enemy.headingRadians = heading;
        enemy.aiMode = EnemyAiMode::Wander;
        enemy.aiModeElapsedSeconds = 0.0F;
        enemy.droneWatchAlignToHeading = false;
    } else {
        enemy.droneWatchAlignHeadingRadians = heading;
        enemy.droneWatchAlignToHeading = true;
        enemy.aiMode = EnemyAiMode::Watch;
        enemy.aiModeElapsedSeconds = 0.0F;
        enemy.watchRotateDirection = RandomRotateDirection(random);
        enemy.returnToBase = false;
    }
}

bool SelectDroneReturnToBaseHeading(
    WorldState& world,
    const EnemyTank& enemy,
    Random& random,
    float& selectedHeading) {
    float desiredHeading = 0.0F;
    if (!DroneTryHeadingTowardBaseAlongFlow(world, enemy.position, desiredHeading)) {
        return false;
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

    std::array<float, 8> candidateHeadings{};
    int candidateCount = 0;
    int bestCandidateIndex = -1;
    float bestAlignment = std::numeric_limits<float>::infinity();
    for (float offset : offsets) {
        const float candidate = core::angle::QuantizeToEightDirections(desiredHeading + offset);
        const float clearDistance = game::geometry::FreeDistanceAhead(
            world,
            enemy.position,
            candidate,
            kDroneReturnRequiredClearRunUnits,
            GameplayConstants::kWallClearanceForAvoidance,
            kEnemyPlanningClearanceScale);
        if (clearDistance < kDroneReturnRequiredClearRunUnits) {
            continue;
        }
        const float alignment = core::angle::AngleDistance(candidate, desiredHeading);
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
        const float candidateHeading =
            core::angle::QuantizeToEightDirections(self.headingRadians + offset);
        const float clearDistance = game::geometry::FreeDistanceAhead(
            world,
            self.position,
            candidateHeading,
            GameplayConstants::kEnemyRequiredClearRunUnits + 0.5F,
            GameplayConstants::kWallClearanceForAvoidance,
            kEnemyPlanningClearanceScale);
        if (clearDistance <= GameplayConstants::kEnemyRequiredClearRunUnits) {
            continue;
        }

        const Vec2f dir = core::angle::DirectionFromHeading(candidateHeading);
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
        const float awayAlignment =
            core::angle::AngleDistance(candidateHeading, awayHeading);

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

bool SelectDronePursuitHeading(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    const Vec2f& playerPosition,
    float stepDistance,
    float playerAvoidDistanceUnits,
    float& selectedHeading) {
    if (selfIndex < 0 || selfIndex >= static_cast<int>(enemies.size())) {
        return false;
    }
    const EnemyTank& self = enemies[static_cast<std::size_t>(selfIndex)];
    const Vec2f toPlayer{
        .x = playerPosition.x - self.position.x,
        .y = playerPosition.y - self.position.y,
    };
    if (std::fabs(toPlayer.x) < 0.0001F && std::fabs(toPlayer.y) < 0.0001F) {
        return false;
    }

    const float desiredHeading = std::atan2(toPlayer.x, -toPlayer.y);
    const float playerAvoidSq = playerAvoidDistanceUnits * playerAvoidDistanceUnits;
    const std::array<float, 8> offsets{
        0.0F,
        kEightDirectionStep,
        -kEightDirectionStep,
        kEightDirectionStep * 2.0F,
        -kEightDirectionStep * 2.0F,
        kEightDirectionStep * 3.0F,
        -kEightDirectionStep * 3.0F,
        kEightDirectionStep * 4.0F};

    bool found = false;
    float bestHeading = self.headingRadians;
    float bestError = std::numeric_limits<float>::infinity();
    for (float offset : offsets) {
        const float candidateHeading =
            core::angle::QuantizeToEightDirections(desiredHeading + offset);
        const float clearDistance = game::geometry::FreeDistanceAhead(
            world,
            self.position,
            candidateHeading,
            std::max(stepDistance, 0.5F),
            GameplayConstants::kWallClearanceForAvoidance,
            kEnemyPlanningClearanceScale);
        if (clearDistance < stepDistance) {
            continue;
        }
        const Vec2f dir = core::angle::DirectionFromHeading(candidateHeading);
        const Vec2f candidatePosition{
            .x = self.position.x + dir.x * stepDistance,
            .y = self.position.y + dir.y * stepDistance,
        };
        if (DistanceSq(candidatePosition, playerPosition) < playerAvoidSq) {
            continue;
        }

        bool collidesWithEnemy = false;
        const float preferredSeparationSq = GameplayConstants::kEnemyPreferredSeparationUnits *
            GameplayConstants::kEnemyPreferredSeparationUnits;
        for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
            if (i == selfIndex) {
                continue;
            }
            const EnemyTank& other = enemies[static_cast<std::size_t>(i)];
            if (!other.alive) {
                continue;
            }
            if (DistanceSq(candidatePosition, other.position) < preferredSeparationSq) {
                collidesWithEnemy = true;
                break;
            }
        }
        if (collidesWithEnemy) {
            continue;
        }

        const float headingError = core::angle::AngleDistance(candidateHeading, desiredHeading);
        if (!found || headingError < bestError) {
            found = true;
            bestError = headingError;
            bestHeading = candidateHeading;
        }
    }

    if (!found) {
        return false;
    }
    selectedHeading = bestHeading;
    return true;
}
