#include "game/geometry/WorldGeometry.h"

#include <algorithm>
#include <cmath>
#include "core/AngleMath.h"
#include "game/model/GameplayConstants.h"
#include "game/spatial/EnemySpatialGrid.h"

namespace game::geometry {

namespace {
constexpr float kRaySampleSpacing = 0.12F;
}  // namespace

bool IsPointInsideMaze(const WorldState& world, const Vec2f& point, float clearanceUnits) {
    const float mazeWidthUnits = static_cast<float>(world.maze.widthCells * world.maze.cellSizeUnits);
    const float mazeHeightUnits = static_cast<float>(world.maze.heightCells * world.maze.cellSizeUnits);
    return point.x >= clearanceUnits && point.x <= mazeWidthUnits - clearanceUnits &&
        point.y >= clearanceUnits && point.y <= mazeHeightUnits - clearanceUnits;
}

bool IsPointInWall(const WorldState& world, const Vec2f& point, float clearanceUnits) {
    if (!IsPointInsideMaze(world, point, clearanceUnits)) {
        return true;
    }
    const float cellSize = static_cast<float>(world.maze.cellSizeUnits);
    const int cellX = static_cast<int>(point.x / cellSize);
    const int cellY = static_cast<int>(point.y / cellSize);
    if (cellX < 0 || cellY < 0 || cellX >= world.maze.widthCells || cellY >= world.maze.heightCells) {
        return true;
    }
    const MazeCell& cell = world.maze.cells[static_cast<std::size_t>(cellY * world.maze.widthCells + cellX)];
    const float localX = point.x - static_cast<float>(cellX) * cellSize;
    const float localY = point.y - static_cast<float>(cellY) * cellSize;
    const float wallLimit = GameplayConstants::kWallThicknessUnits + clearanceUnits;
    return (cell.northWall && localY <= wallLimit) || (cell.southWall && localY >= cellSize - wallLimit) ||
        (cell.westWall && localX <= wallLimit) || (cell.eastWall && localX >= cellSize - wallLimit);
}

bool IsPointInUndestroyedBase(const WorldState& world, const Vec2f& point, float clearanceUnits) {
    const float halfBase = GameplayConstants::kEnemyBaseSizeUnits * 0.5F;
    for (const EnemyBase& base : world.enemyBases) {
        if (base.destroyed) {
            continue;
        }
        const float dx = std::fabs(point.x - base.position.x);
        const float dy = std::fabs(point.y - base.position.y);
        if (dx <= halfBase + clearanceUnits && dy <= halfBase + clearanceUnits) {
            return true;
        }
    }
    return false;
}

bool SegmentIntersectsWall(const WorldState& world, const Vec2f& from, const Vec2f& to, float clearanceUnits) {
    const bool startsInsideBase = IsPointInUndestroyedBase(world, from, clearanceUnits);
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= 0.001F) {
        return IsPointInWall(world, to, clearanceUnits) ||
            (!startsInsideBase && IsPointInUndestroyedBase(world, to, clearanceUnits));
    }
    const float sampleSpacing = std::max(0.02F, clearanceUnits * 0.5F);
    const int steps = std::max(1, static_cast<int>(std::ceil(distance / sampleSpacing)));
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const Vec2f sample{
            .x = from.x + dx * t,
            .y = from.y + dy * t,
        };
        if (IsPointInWall(world, sample, clearanceUnits)) {
            return true;
        }
        if (!startsInsideBase && IsPointInUndestroyedBase(world, sample, clearanceUnits)) {
            return true;
        }
    }
    return false;
}

bool IsSegmentObscuredByWall(const WorldState& world, const Vec2f& from, const Vec2f& to) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.001F) {
        return false;
    }
    const int steps = std::max(1, static_cast<int>(std::ceil(length / kRaySampleSpacing)));
    for (int i = 1; i < steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const Vec2f sample{.x = from.x + dx * t, .y = from.y + dy * t};
        if (IsPointInWall(world, sample, 0.0F)) {
            return true;
        }
    }
    return false;
}

float FreeDistanceAhead(const WorldState& world, const Vec2f& from, float headingRadians,
    float maxDistance, float clearanceUnits, float planningClearanceScale) {
    const float planningClearance =
        clearanceUnits > 0.0F ? clearanceUnits * planningClearanceScale : clearanceUnits;
    const Vec2f dir = core::angle::DirectionFromHeading(headingRadians);
    const bool startsInsideBase = IsPointInUndestroyedBase(world, from, planningClearance);
    const int steps = std::max(1, static_cast<int>(std::ceil(maxDistance / kRaySampleSpacing)));
    for (int i = 1; i <= steps; ++i) {
        const float dist = std::min(maxDistance, static_cast<float>(i) * kRaySampleSpacing);
        const Vec2f sample{.x = from.x + dir.x * dist, .y = from.y + dir.y * dist};
        if (IsPointInWall(world, sample, planningClearance)) {
            return dist;
        }
        if (!startsInsideBase && IsPointInUndestroyedBase(world, sample, planningClearance)) {
            return dist;
        }
    }
    return maxDistance;
}

float FreeDistanceAheadWithEnemies(const WorldState& world,
    const std::vector<EnemyTank>& enemies, int selfIndex, const Vec2f& from,
    float headingRadians, float maxDistance, float clearanceUnits,
    float planningClearanceScale, const game::spatial::EnemySpatialGrid* spatialGrid) {
    const float staticObstacleDistance =
        FreeDistanceAhead(world, from, headingRadians, maxDistance, clearanceUnits, planningClearanceScale);
    const float probeDistance = std::min(maxDistance, staticObstacleDistance);
    const float separationRadius = GameplayConstants::kEnemyPreferredSeparationUnits;

    std::vector<int> candidateIndices;
    if (spatialGrid != nullptr) {
        const Vec2f dir = core::angle::DirectionFromHeading(headingRadians);
        spatialGrid->GetEnemiesAlongRay(enemies, selfIndex, from, dir, probeDistance, candidateIndices);
    } else {
        const float filterRadius = probeDistance + separationRadius;
        const float filterRadiusSq = filterRadius * filterRadius;
        candidateIndices.reserve(enemies.size());
        for (int ei = 0; ei < static_cast<int>(enemies.size()); ++ei) {
            if (ei == selfIndex) {
                continue;
            }
            const EnemyTank& other = enemies[static_cast<std::size_t>(ei)];
            if (!other.alive) {
                continue;
            }
            const float dx = from.x - other.position.x;
            const float dy = from.y - other.position.y;
            if (dx * dx + dy * dy <= filterRadiusSq) {
                candidateIndices.push_back(ei);
            }
        }
    }

    const Vec2f dir = core::angle::DirectionFromHeading(headingRadians);
    const float sepSq = separationRadius * separationRadius;
    const int steps = std::max(1, static_cast<int>(std::ceil(probeDistance / kRaySampleSpacing)));
    for (int i = 1; i <= steps; ++i) {
        const float dist = std::min(probeDistance, static_cast<float>(i) * kRaySampleSpacing);
        const Vec2f sample{.x = from.x + dir.x * dist, .y = from.y + dir.y * dist};
        for (int ei : candidateIndices) {
            const EnemyTank& other = enemies[static_cast<std::size_t>(ei)];
            const float sdx = sample.x - other.position.x;
            const float sdy = sample.y - other.position.y;
            const float distSq = sdx * sdx + sdy * sdy;
            if (distSq < sepSq) {
                return dist;
            }
        }
    }
    return probeDistance;
}
}  // namespace game::geometry
