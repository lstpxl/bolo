#include "game/navigation/AdjacentCellSegmentPlanner.h"

#include <cmath>
#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"
#include "game/systems/EnemySystemHelpers.h"

namespace game::navigation::AdjacentCellSegmentPlanner {

namespace {

bool IsCellBlockedByAliveBase(
    const WorldState& world, const CellCoordCache& cellCache, int cellX, int cellY) {
    for (const EnemyBase& base : world.enemyBases) {
        if (base.destroyed) {
            continue;
        }
        const MazeCellCoord baseCell = cellCache.WorldToCell(base.position);
        if (baseCell.x == cellX && baseCell.y == cellY) {
            return true;
        }
    }
    return false;
}

bool CanTraverseCardinal(
    const WorldState& world,
    const CellCoordCache& cellCache,
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

bool SegmentIntersectsWallAvoidance(const WorldState& world, const Vec2f& from, const Vec2f& to) {
    return game::geometry::SegmentIntersectsWall(
        world, from, to, GameplayConstants::kWallClearanceForAvoidance);
}

}  // namespace

bool Build(
    const WorldState& world,
    const CellCoordCache& cellCache,
    const MazeCellCoord& fromCell,
    const MazeCellCoord& targetCell,
    const Vec2f& startPosition,
    std::array<Vec2f, 2>& outPoints,
    int& outCount) {
    outCount = 0;
    const Vec2f targetCenter = cellCache.CellCenter(targetCell.x, targetCell.y);
    const int moveDx = targetCell.x - fromCell.x;
    const int moveDy = targetCell.y - fromCell.y;
    const bool isDiagonal = std::abs(moveDx) == 1 && std::abs(moveDy) == 1;
    if (!isDiagonal) {
        if (!SegmentIntersectsWallAvoidance(world, startPosition, targetCenter) &&
            IsValidSegmentEndpoint(world, targetCenter)) {
            outPoints[0] = targetCenter;
            outCount = 1;
            return true;
        }
        return false;
    }

    const MazeCellCoord bendVertical{
        .x = fromCell.x,
        .y = fromCell.y + moveDy,
    };
    const MazeCellCoord bendHorizontal{
        .x = fromCell.x + moveDx,
        .y = fromCell.y,
    };
    const bool openAB = CanTraverseCardinal(
        world, cellCache, fromCell.x, fromCell.y, bendVertical.x, bendVertical.y);
    const bool openBD = CanTraverseCardinal(
        world, cellCache, bendVertical.x, bendVertical.y, targetCell.x, targetCell.y);
    const bool openAC = CanTraverseCardinal(
        world, cellCache, fromCell.x, fromCell.y, bendHorizontal.x, bendHorizontal.y);
    const bool openCD = CanTraverseCardinal(
        world, cellCache, bendHorizontal.x, bendHorizontal.y, targetCell.x, targetCell.y);

    const bool blockedAB = !openAB;
    const bool blockedBD = !openBD;
    const bool blockedAC = !openAC;
    const bool blockedCD = !openCD;
    const int blockedCount = static_cast<int>(blockedAB) + static_cast<int>(blockedBD) +
        static_cast<int>(blockedAC) + static_cast<int>(blockedCD);
    const bool hardBlocked = blockedCount >= 3 ||
        (blockedAB && blockedAC) ||
        (blockedBD && blockedCD);
    if (hardBlocked) {
        return false;
    }

    const bool bendVerticalUsable = openAB && openBD;
    const bool bendHorizontalUsable = openAC && openCD;
    if (bendVerticalUsable && bendHorizontalUsable) {
        outPoints[0] = targetCenter;
        outCount = 1;
        return true;
    }
    if (!bendVerticalUsable && !bendHorizontalUsable) {
        return false;
    }

    const MazeCellCoord midpointCell = bendVerticalUsable ? bendVertical : bendHorizontal;
    const MazeCellCoord oppositeCell = bendVerticalUsable ? bendHorizontal : bendVertical;
    const Vec2f midpointCellCenter = cellCache.CellCenter(midpointCell.x, midpointCell.y);
    const Vec2f oppositeCellCenter = cellCache.CellCenter(oppositeCell.x, oppositeCell.y);
    const Vec2f towardOpposite{
        .x = oppositeCellCenter.x - midpointCellCenter.x,
        .y = oppositeCellCenter.y - midpointCellCenter.y,
    };
    const float towardOppositeLengthSq = towardOpposite.x * towardOpposite.x + towardOpposite.y * towardOpposite.y;
    if (towardOppositeLengthSq <= 0.0F) {
        return false;
    }
    const float towardOppositeInvLength = 1.0F / std::sqrt(towardOppositeLengthSq);
    const float shift = GameplayConstants::kAdjacentCellDiagonalMidpointShiftUnits;
    const Vec2f midpoint{
        .x = midpointCellCenter.x + towardOpposite.x * towardOppositeInvLength * shift,
        .y = midpointCellCenter.y + towardOpposite.y * towardOppositeInvLength * shift,
    };

    outPoints[0] = midpoint;
    outPoints[1] = targetCenter;
    outCount = 2;
    return true;
}

}  // namespace game::navigation::AdjacentCellSegmentPlanner
