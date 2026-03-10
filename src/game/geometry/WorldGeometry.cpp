#include "game/geometry/WorldGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include "core/AngleMath.h"
#include "game/model/GameplayConstants.h"
#include "game/spatial/EnemyCellOccupancy.h"

namespace game::geometry {

namespace {
constexpr float kAdaptiveFineSpacing = 0.08F;    // near origin (precise collision)
constexpr float kAdaptiveCoarseSpacing = 0.20F; // far from origin (detection only)

/// Returns the next step size for adaptive sampling. Step grows with distance from origin.
float AdaptiveStepAt(float distFromOrigin, float maxDistance) {
    if (maxDistance <= 0.001F) {
        return kAdaptiveFineSpacing;
    }
    const float t = std::min(1.0F, distFromOrigin / maxDistance);
    return kAdaptiveFineSpacing + t * (kAdaptiveCoarseSpacing - kAdaptiveFineSpacing);
}

bool RayAabbFirstHitInRange(const Vec2f& origin, const Vec2f& dir,
    float minX, float minY, float maxX, float maxY,
    float tMin, float tMax, float& outT) {
    constexpr float kEps = 0.000001F;
    float entry = tMin;
    float exit = tMax;

    if (std::fabs(dir.x) <= kEps) {
        if (origin.x < minX || origin.x > maxX) {
            return false;
        }
    } else {
        const float invDx = 1.0F / dir.x;
        float tx1 = (minX - origin.x) * invDx;
        float tx2 = (maxX - origin.x) * invDx;
        if (tx1 > tx2) {
            std::swap(tx1, tx2);
        }
        entry = std::max(entry, tx1);
        exit = std::min(exit, tx2);
        if (entry > exit) {
            return false;
        }
    }

    if (std::fabs(dir.y) <= kEps) {
        if (origin.y < minY || origin.y > maxY) {
            return false;
        }
    } else {
        const float invDy = 1.0F / dir.y;
        float ty1 = (minY - origin.y) * invDy;
        float ty2 = (maxY - origin.y) * invDy;
        if (ty1 > ty2) {
            std::swap(ty1, ty2);
        }
        entry = std::max(entry, ty1);
        exit = std::min(exit, ty2);
        if (entry > exit) {
            return false;
        }
    }

    if (entry > tMax || exit < tMin) {
        return false;
    }

    outT = std::max(entry, tMin);
    return true;
}

float BaseHitDistance(const WorldState& world, const Vec2f& from, const Vec2f& dir,
    float maxDistance, float planningClearance, bool startsInsideBase) {
    if (startsInsideBase) {
        return maxDistance;
    }

    const float halfBase = GameplayConstants::kEnemyBaseSizeUnits * 0.5F + planningClearance;
    float best = maxDistance;
    for (const EnemyBase& base : world.enemyBases) {
        if (base.destroyed) {
            continue;
        }
        float t = best;
        if (RayAabbFirstHitInRange(
                from,
                dir,
                base.position.x - halfBase,
                base.position.y - halfBase,
                base.position.x + halfBase,
                base.position.y + halfBase,
                0.0F,
                best,
                t)) {
            best = std::min(best, t);
        }
    }
    return best;
}

float MazeBoundaryHitDistance(const WorldState& world, const Vec2f& from, const Vec2f& dir,
    float maxDistance, float planningClearance) {
    const float mazeWidthUnits = static_cast<float>(world.maze.widthCells * world.maze.cellSizeUnits);
    const float mazeHeightUnits = static_cast<float>(world.maze.heightCells * world.maze.cellSizeUnits);
    float entry = 0.0F;
    float exit = maxDistance;
    if (!RayAabbFirstHitInRange(
            from,
            dir,
            planningClearance,
            planningClearance,
            mazeWidthUnits - planningClearance,
            mazeHeightUnits - planningClearance,
            0.0F,
            maxDistance,
            entry)) {
        // Outside valid bounds at origin or ray never enters valid area: considered blocked.
        return 0.0F;
    }

    // Compute where the ray exits the valid maze interior.
    constexpr float kEps = 0.000001F;
    if (std::fabs(dir.x) > kEps) {
        const float invDx = 1.0F / dir.x;
        float tx1 = (planningClearance - from.x) * invDx;
        float tx2 = (mazeWidthUnits - planningClearance - from.x) * invDx;
        if (tx1 > tx2) {
            std::swap(tx1, tx2);
        }
        exit = std::min(exit, tx2);
    }
    if (std::fabs(dir.y) > kEps) {
        const float invDy = 1.0F / dir.y;
        float ty1 = (planningClearance - from.y) * invDy;
        float ty2 = (mazeHeightUnits - planningClearance - from.y) * invDy;
        if (ty1 > ty2) {
            std::swap(ty1, ty2);
        }
        exit = std::min(exit, ty2);
    }

    return std::max(0.0F, std::min(maxDistance, exit));
}

float CellWallHitDistance(const WorldState& world, int cellX, int cellY,
    const Vec2f& from, const Vec2f& dir, float tCellMin, float tCellMax, float wallLimit) {
    if (cellX < 0 || cellY < 0 || cellX >= world.maze.widthCells || cellY >= world.maze.heightCells) {
        return std::numeric_limits<float>::infinity();
    }

    const MazeCell& cell = world.maze.cells[static_cast<std::size_t>(cellY * world.maze.widthCells + cellX)];
    const float cellSize = static_cast<float>(world.maze.cellSizeUnits);
    const float minX = static_cast<float>(cellX) * cellSize;
    const float minY = static_cast<float>(cellY) * cellSize;
    const float maxX = minX + cellSize;
    const float maxY = minY + cellSize;

    float best = std::numeric_limits<float>::infinity();
    float t = tCellMax;

    if (cell.northWall &&
        RayAabbFirstHitInRange(from, dir, minX, minY, maxX, std::min(maxY, minY + wallLimit), tCellMin, tCellMax, t)) {
        best = std::min(best, t);
    }
    if (cell.southWall &&
        RayAabbFirstHitInRange(from, dir, minX, std::max(minY, maxY - wallLimit), maxX, maxY, tCellMin, tCellMax, t)) {
        best = std::min(best, t);
    }
    if (cell.westWall &&
        RayAabbFirstHitInRange(from, dir, minX, minY, std::min(maxX, minX + wallLimit), maxY, tCellMin, tCellMax, t)) {
        best = std::min(best, t);
    }
    if (cell.eastWall &&
        RayAabbFirstHitInRange(from, dir, std::max(minX, maxX - wallLimit), minY, maxX, maxY, tCellMin, tCellMax, t)) {
        best = std::min(best, t);
    }

    return best;
}
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
    const float invLen = 1.0F / length;
    float dist = kAdaptiveFineSpacing;
    while (dist < length) {
        const float t = dist * invLen;
        const Vec2f sample{.x = from.x + dx * t, .y = from.y + dy * t};
        if (IsPointInWall(world, sample, 0.0F)) {
            return true;
        }
        dist += AdaptiveStepAt(dist, length);
    }
    return false;
}

float FreeDistanceAheadContinuous(const WorldState& world, const Vec2f& from, float headingRadians,
    float maxDistance, float clearanceUnits, float planningClearanceScale) {
    const float planningClearance =
        clearanceUnits > 0.0F ? clearanceUnits * planningClearanceScale : clearanceUnits;
    const Vec2f dir = core::angle::DirectionFromHeading(headingRadians);
    const bool startsInsideBase = IsPointInUndestroyedBase(world, from, planningClearance);
    float dist = kAdaptiveFineSpacing;
    while (dist <= maxDistance) {
        const float clampedDist = std::min(dist, maxDistance);
        const Vec2f sample{.x = from.x + dir.x * clampedDist, .y = from.y + dir.y * clampedDist};
        if (IsPointInWall(world, sample, planningClearance)) {
            return clampedDist;
        }
        if (!startsInsideBase && IsPointInUndestroyedBase(world, sample, planningClearance)) {
            return clampedDist;
        }
        if (clampedDist >= maxDistance) {
            break;
        }
        dist += AdaptiveStepAt(dist, maxDistance);
    }
    return maxDistance;
}

float FreeDistanceAheadGridImpl(const WorldState& world, const Vec2f& from, float headingRadians,
    float maxDistance, float clearanceUnits, float planningClearanceScale, bool includeBases) {
    if (maxDistance <= 0.0F) {
        return 0.0F;
    }

    const float planningClearance =
        clearanceUnits > 0.0F ? clearanceUnits * planningClearanceScale : clearanceUnits;
    if (!IsPointInsideMaze(world, from, planningClearance)) {
        return 0.0F;
    }

    const Vec2f dir = core::angle::DirectionFromHeading(headingRadians);
    const bool startsInsideBase = includeBases && IsPointInUndestroyedBase(world, from, planningClearance);

    float best = maxDistance;
    best = std::min(best, MazeBoundaryHitDistance(world, from, dir, best, planningClearance));
    if (includeBases) {
        best = std::min(best, BaseHitDistance(world, from, dir, best, planningClearance, startsInsideBase));
    }

    const float cellSize = static_cast<float>(world.maze.cellSizeUnits);
    int cellX = std::clamp(static_cast<int>(from.x / cellSize), 0, world.maze.widthCells - 1);
    int cellY = std::clamp(static_cast<int>(from.y / cellSize), 0, world.maze.heightCells - 1);
    const float wallLimit = GameplayConstants::kWallThicknessUnits + planningClearance;

    // Fast path: if the whole candidate segment stays in one cell, avoid DDA stepping.
    const Vec2f end{
        .x = from.x + dir.x * best,
        .y = from.y + dir.y * best,
    };
    const int endCellX = std::clamp(static_cast<int>(end.x / cellSize), 0, world.maze.widthCells - 1);
    const int endCellY = std::clamp(static_cast<int>(end.y / cellSize), 0, world.maze.heightCells - 1);
    if (cellX == endCellX && cellY == endCellY) {
        const float cellHit = CellWallHitDistance(world, cellX, cellY, from, dir, 0.0F, best, wallLimit);
        if (cellHit <= best) {
            best = cellHit;
        }
        return std::max(0.0F, std::min(maxDistance, best));
    }

    constexpr float kEps = 0.000001F;
    const int stepX = (dir.x > kEps) ? 1 : ((dir.x < -kEps) ? -1 : 0);
    const int stepY = (dir.y > kEps) ? 1 : ((dir.y < -kEps) ? -1 : 0);
    const float inf = std::numeric_limits<float>::infinity();

    const float nextBoundaryX = (stepX > 0) ? (static_cast<float>(cellX + 1) * cellSize)
                                             : (static_cast<float>(cellX) * cellSize);
    const float nextBoundaryY = (stepY > 0) ? (static_cast<float>(cellY + 1) * cellSize)
                                             : (static_cast<float>(cellY) * cellSize);
    float tMaxX = (stepX != 0) ? ((nextBoundaryX - from.x) / dir.x) : inf;
    float tMaxY = (stepY != 0) ? ((nextBoundaryY - from.y) / dir.y) : inf;
    float tDeltaX = (stepX != 0) ? (cellSize / std::fabs(dir.x)) : inf;
    float tDeltaY = (stepY != 0) ? (cellSize / std::fabs(dir.y)) : inf;
    tMaxX = std::max(0.0F, tMaxX);
    tMaxY = std::max(0.0F, tMaxY);

    float tCellMin = 0.0F;
    while (tCellMin <= best) {
        const float tCellMax = std::min(best, std::min(tMaxX, tMaxY));
        const float cellHit = CellWallHitDistance(world, cellX, cellY, from, dir, tCellMin, tCellMax, wallLimit);
        if (cellHit <= best) {
            best = cellHit;
            break;
        }

        if (tMaxX < tMaxY) {
            cellX += stepX;
            tCellMin = tMaxX;
            tMaxX += tDeltaX;
        } else if (tMaxY < tMaxX) {
            cellY += stepY;
            tCellMin = tMaxY;
            tMaxY += tDeltaY;
        } else {
            cellX += stepX;
            cellY += stepY;
            tCellMin = tMaxX;
            tMaxX += tDeltaX;
            tMaxY += tDeltaY;
        }

        if (cellX < 0 || cellY < 0 || cellX >= world.maze.widthCells || cellY >= world.maze.heightCells) {
            break;
        }
    }

    return std::max(0.0F, std::min(maxDistance, best));
}

float FreeDistanceAheadGrid(const WorldState& world, const Vec2f& from, float headingRadians,
    float maxDistance, float clearanceUnits, float planningClearanceScale) {
    return FreeDistanceAheadGridImpl(
        world, from, headingRadians, maxDistance, clearanceUnits, planningClearanceScale, true);
}

float FreeDistanceAheadGridWallsOnly(const WorldState& world, const Vec2f& from, float headingRadians,
    float maxDistance, float clearanceUnits, float planningClearanceScale) {
    return FreeDistanceAheadGridImpl(
        world, from, headingRadians, maxDistance, clearanceUnits, planningClearanceScale, false);
}

float FreeDistanceAhead(const WorldState& world, const Vec2f& from, float headingRadians,
    float maxDistance, float clearanceUnits, float planningClearanceScale) {
    return FreeDistanceAheadGrid(
        world, from, headingRadians, maxDistance, clearanceUnits, planningClearanceScale);
}

float FreeDistanceAheadWithEnemies(const WorldState& world,
    const std::vector<EnemyTank>& enemies, int selfIndex, const Vec2f& from,
    float headingRadians, float maxDistance, float clearanceUnits,
    float planningClearanceScale, const game::spatial::EnemyCellOccupancy* rayQueryOccupancy) {
    const float staticObstacleDistance =
        FreeDistanceAhead(world, from, headingRadians, maxDistance, clearanceUnits, planningClearanceScale);
    const float probeDistance = std::min(maxDistance, staticObstacleDistance);
    const float separationRadius = GameplayConstants::kEnemyPreferredSeparationUnits;

    std::vector<int> candidateIndices;
    if (rayQueryOccupancy != nullptr) {
        const Vec2f dir = core::angle::DirectionFromHeading(headingRadians);
        rayQueryOccupancy->GetEnemiesAlongRay(enemies, selfIndex, from, dir, probeDistance, candidateIndices);
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
    float dist = kAdaptiveFineSpacing;
    while (dist <= probeDistance) {
        const float clampedDist = std::min(dist, probeDistance);
        const Vec2f sample{.x = from.x + dir.x * clampedDist, .y = from.y + dir.y * clampedDist};
        for (int ei : candidateIndices) {
            const EnemyTank& other = enemies[static_cast<std::size_t>(ei)];
            const float sdx = sample.x - other.position.x;
            const float sdy = sample.y - other.position.y;
            const float distSq = sdx * sdx + sdy * sdy;
            if (distSq < sepSq) {
                return clampedDist;
            }
        }
        if (clampedDist >= probeDistance) {
            break;
        }
        dist += AdaptiveStepAt(dist, probeDistance);
    }
    return probeDistance;
}
}  // namespace game::geometry
