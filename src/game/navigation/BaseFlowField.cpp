#include "game/navigation/BaseFlowField.h"

#include <array>
#include <chrono>
#include <limits>
#include "core/Log.h"

namespace game::navigation {

void BaseFlowField::EnsureCapacity(const MazeState& maze) {
    widthCells_ = maze.widthCells;
    heightCells_ = maze.heightCells;
    const int totalCells = widthCells_ * heightCells_;
    if (totalCells <= 0) {
        nextCellHash_.clear();
        hasBuild_ = false;
        return;
    }
    if (static_cast<int>(nextCellHash_.size()) != totalCells) {
        nextCellHash_.assign(static_cast<std::size_t>(totalCells), -1);
    }
}

bool BaseFlowField::CanTraverse(const MazeState& maze, int fromX, int fromY, int toX, int toY) const {
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

void BaseFlowField::Rebuild(
    const MazeState& maze,
    const CellCoordCache& cellCache,
    const BaseDistanceField& baseDistanceField) {
    EnsureCapacity(maze);
    if (widthCells_ <= 0 || heightCells_ <= 0 || !baseDistanceField.HasBuild()) {
        return;
    }

    const auto timeBuildStart = std::chrono::steady_clock::now();

    const int totalCells = widthCells_ * heightCells_;
    for (int i = 0; i < totalCells; ++i) {
        nextCellHash_[static_cast<std::size_t>(i)] = -1;
    }

    constexpr std::array<int, 4> kDx{1, -1, 0, 0};
    constexpr std::array<int, 4> kDy{0, 0, 1, -1};

    for (int y = 0; y < heightCells_; ++y) {
        for (int x = 0; x < widthCells_; ++x) {
            const int cellDistance = baseDistanceField.DistanceAtCell(x, y);
            if (cellDistance <= 0 || cellDistance == std::numeric_limits<int>::max()) {
                continue;
            }

            const int fromHash = cellCache.CellHash(x, y);
            for (int i = 0; i < 4; ++i) {
                const int nx = x + kDx[static_cast<std::size_t>(i)];
                const int ny = y + kDy[static_cast<std::size_t>(i)];
                if (!cellCache.IsValidCell(nx, ny)) {
                    continue;
                }
                if (!CanTraverse(maze, x, y, nx, ny)) {
                    continue;
                }
                const int neighborDistance = baseDistanceField.DistanceAtCell(nx, ny);
                if (neighborDistance != cellDistance - 1) {
                    continue;
                }
                nextCellHash_[static_cast<std::size_t>(fromHash)] = cellCache.CellHash(nx, ny);
                break;
            }
        }
    }

    hasBuild_ = true;

    const auto timeBuildEnd = std::chrono::steady_clock::now();
    const double buildTimeMs =
        std::chrono::duration<double, std::milli>(timeBuildEnd - timeBuildStart).count();
    bolt::log::Debug(
        "[NAV] BaseFlowField rebuild %dx%d cells in %.3f ms",
        widthCells_,
        heightCells_,
        buildTimeMs);
}

void BaseFlowField::Invalidate() {
    hasBuild_ = false;
}

int BaseFlowField::NextCellHash(int fromCellHash) const {
    if (!hasBuild_ || fromCellHash < 0 || fromCellHash >= static_cast<int>(nextCellHash_.size())) {
        return -1;
    }
    return nextCellHash_[static_cast<std::size_t>(fromCellHash)];
}

Vec2f BaseFlowField::NextCellCenter(int fromCellHash, const CellCoordCache& cellCache) const {
    const int nextHash = NextCellHash(fromCellHash);
    if (nextHash < 0 || cellCache.WidthCells() <= 0) {
        return Vec2f{.x = 0.0F, .y = 0.0F};
    }
    const int cellX = nextHash % cellCache.WidthCells();
    const int cellY = nextHash / cellCache.WidthCells();
    return cellCache.CellCenter(cellX, cellY);
}

}  // namespace game::navigation
