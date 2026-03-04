#include "game/geometry/WorldGeometry.h"

#include <algorithm>
#include <cmath>

namespace game::geometry {
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
}  // namespace game::geometry
