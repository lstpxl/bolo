#include "game/systems/EnemyDrone.h"

#include <cmath>
#include <limits>
#include "core/AngleMath.h"
#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"
#include "game/systems/EnemySystemHelpers.h"

namespace {

constexpr float kEightDirectionStep = 3.14159265358979323846F / 4.0F;
constexpr float kDroneReturnRequiredClearRunUnits = 6.0F;
constexpr float kEnemyPlanningClearanceScale = 1.5F;

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
    enemy.aiMode = EnemyAiMode::Watch;
    enemy.aiModeElapsedSeconds = 0.0F;
    enemy.watchRotateDirection = RandomRotateDirection(random);
    enemy.returnToBase = DroneIsFarEnoughForReturnToBase(world, enemy.position);
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
