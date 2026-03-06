#include "game/navigation/PlayerFlowField.h"

#include <array>
#include <deque>
#include <limits>

namespace game::navigation {

void PlayerFlowField::EnsureCapacity(const MazeState& maze) {
    widthCells_ = maze.widthCells;
    heightCells_ = maze.heightCells;
    const int totalCells = widthCells_ * heightCells_;
    if (totalCells <= 0) {
        nextCellHash_.clear();
        distance_.clear();
        hasBuild_ = false;
        return;
    }
    if (static_cast<int>(nextCellHash_.size()) != totalCells) {
        nextCellHash_.assign(static_cast<std::size_t>(totalCells), -1);
    }
    if (static_cast<int>(distance_.size()) != totalCells) {
        distance_.assign(static_cast<std::size_t>(totalCells), std::numeric_limits<int>::max());
    }
}

bool PlayerFlowField::CanTraverse(const MazeState& maze, int fromX, int fromY, int toX, int toY) const {
    if (toX < 0 || toX >= maze.widthCells || toY < 0 || toY >= maze.heightCells) {
        return false;
    }
    const MazeCell& from = maze.cells[static_cast<std::size_t>(fromY * maze.widthCells + fromX)];
    if (toX == fromX + 1) {
        return !from.eastWall;
    }
    if (toX == fromX - 1) {
        return !from.westWall;
    }
    if (toY == fromY + 1) {
        return !from.southWall;
    }
    if (toY == fromY - 1) {
        return !from.northWall;
    }
    return false;
}

void PlayerFlowField::Rebuild(const MazeState& maze, const CellCoordCache& cellCache) {
    EnsureCapacity(maze);
    if (widthCells_ <= 0 || heightCells_ <= 0) {
        return;
    }

    const int totalCells = widthCells_ * heightCells_;
    for (int i = 0; i < totalCells; ++i) {
        nextCellHash_[static_cast<std::size_t>(i)] = -1;
        distance_[static_cast<std::size_t>(i)] = std::numeric_limits<int>::max();
    }

    const MazeCellCoord playerCell = cellCache.PlayerCell();
    if (!cellCache.IsValidCell(playerCell.x, playerCell.y)) {
        hasBuild_ = false;
        return;
    }
    const int goalHash = cellCache.PlayerCellHash();
    std::deque<int> queue{};
    queue.push_back(goalHash);
    distance_[static_cast<std::size_t>(goalHash)] = 0;
    nextCellHash_[static_cast<std::size_t>(goalHash)] = goalHash;

    constexpr std::array<int, 4> kDx{1, -1, 0, 0};
    constexpr std::array<int, 4> kDy{0, 0, 1, -1};

    while (!queue.empty()) {
        const int currentHash = queue.front();
        queue.pop_front();
        const int currentX = currentHash % widthCells_;
        const int currentY = currentHash / widthCells_;
        const int currentDist = distance_[static_cast<std::size_t>(currentHash)];
        for (int i = 0; i < 4; ++i) {
            const int neighborX = currentX + kDx[static_cast<std::size_t>(i)];
            const int neighborY = currentY + kDy[static_cast<std::size_t>(i)];
            if (!cellCache.IsValidCell(neighborX, neighborY)) {
                continue;
            }
            if (!CanTraverse(maze, neighborX, neighborY, currentX, currentY)) {
                continue;
            }
            const int neighborHash = cellCache.CellHash(neighborX, neighborY);
            if (distance_[static_cast<std::size_t>(neighborHash)] <= currentDist + 1) {
                continue;
            }
            distance_[static_cast<std::size_t>(neighborHash)] = currentDist + 1;
            nextCellHash_[static_cast<std::size_t>(neighborHash)] = currentHash;
            queue.push_back(neighborHash);
        }
    }

    builtForPlayerCellVersion_ = cellCache.PlayerCellVersion();
    hasBuild_ = true;
}

bool PlayerFlowField::IsBuiltFor(std::uint32_t playerCellVersion) const {
    return hasBuild_ && builtForPlayerCellVersion_ == playerCellVersion;
}

int PlayerFlowField::NextCellHash(int fromCellHash) const {
    if (!hasBuild_ || fromCellHash < 0 || fromCellHash >= static_cast<int>(nextCellHash_.size())) {
        return -1;
    }
    return nextCellHash_[static_cast<std::size_t>(fromCellHash)];
}

Vec2f PlayerFlowField::NextCellCenter(int fromCellHash, const CellCoordCache& cellCache) const {
    const int nextHash = NextCellHash(fromCellHash);
    if (nextHash < 0 || cellCache.WidthCells() <= 0) {
        return Vec2f{.x = 0.0F, .y = 0.0F};
    }
    const int cellX = nextHash % cellCache.WidthCells();
    const int cellY = nextHash / cellCache.WidthCells();
    return cellCache.CellCenter(cellX, cellY);
}

}  // namespace game::navigation
