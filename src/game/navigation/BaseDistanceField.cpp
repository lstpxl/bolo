#include "game/navigation/BaseDistanceField.h"

#include <array>
#include <chrono>
#include <deque>
#include <limits>
#include "core/Log.h"

namespace game::navigation {

void BaseDistanceField::EnsureCapacity(const MazeState& maze) {
    widthCells_ = maze.widthCells;
    heightCells_ = maze.heightCells;
    const int totalCells = widthCells_ * heightCells_;
    if (totalCells <= 0) {
        distance_.clear();
        hasBuild_ = false;
        return;
    }
    if (static_cast<int>(distance_.size()) != totalCells) {
        distance_.assign(static_cast<std::size_t>(totalCells), std::numeric_limits<int>::max());
    }
}

bool BaseDistanceField::CanTraverse(const MazeState& maze, int fromX, int fromY, int toX, int toY) const {
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

void BaseDistanceField::Rebuild(const MazeState& maze, const CellCoordCache& cellCache,
    const std::vector<EnemyBase>& bases) {
    EnsureCapacity(maze);
    if (widthCells_ <= 0 || heightCells_ <= 0) {
        return;
    }

    const auto timeBuildStart = std::chrono::steady_clock::now();

    const int totalCells = widthCells_ * heightCells_;
    for (int i = 0; i < totalCells; ++i) {
        distance_[static_cast<std::size_t>(i)] = std::numeric_limits<int>::max();
    }

    std::deque<int> queue{};
    for (const EnemyBase& base : bases) {
        if (base.destroyed) {
            continue;
        }
        const MazeCellCoord baseCell = cellCache.WorldToCell(base.position);
        if (!cellCache.IsValidCell(baseCell.x, baseCell.y)) {
            continue;
        }
        const int baseHash = cellCache.CellHash(baseCell.x, baseCell.y);
        if (distance_[static_cast<std::size_t>(baseHash)] == 0) {
            continue;
        }
        distance_[static_cast<std::size_t>(baseHash)] = 0;
        queue.push_back(baseHash);
    }

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
            if (!CanTraverse(maze, currentX, currentY, neighborX, neighborY)) {
                continue;
            }
            const int neighborHash = cellCache.CellHash(neighborX, neighborY);
            if (distance_[static_cast<std::size_t>(neighborHash)] <= currentDist + 1) {
                continue;
            }
            distance_[static_cast<std::size_t>(neighborHash)] = currentDist + 1;
            queue.push_back(neighborHash);
        }
    }

    hasBuild_ = true;

    const auto timeBuildEnd = std::chrono::steady_clock::now();
    const double buildTimeMs =
        std::chrono::duration<double, std::milli>(timeBuildEnd - timeBuildStart).count();
    bolt::log::Debug(
        "[NAV] BaseDistanceField rebuild %dx%d cells in %.3f ms",
        widthCells_,
        heightCells_,
        buildTimeMs);
}

void BaseDistanceField::Invalidate() {
    hasBuild_ = false;
}

int BaseDistanceField::DistanceAtCell(int cellX, int cellY) const {
    if (!hasBuild_ || cellX < 0 || cellY < 0 || cellX >= widthCells_ || cellY >= heightCells_) {
        return std::numeric_limits<int>::max();
    }
    const int hash = cellY * widthCells_ + cellX;
    return distance_[static_cast<std::size_t>(hash)];
}

int BaseDistanceField::DistanceAtHash(int cellHash) const {
    if (!hasBuild_ || cellHash < 0 || cellHash >= static_cast<int>(distance_.size())) {
        return std::numeric_limits<int>::max();
    }
    return distance_[static_cast<std::size_t>(cellHash)];
}

}  // namespace game::navigation
