#include "game/navigation/CellCoordCache.h"

#include <algorithm>

namespace game::navigation {

void CellCoordCache::ConfigureFromMaze(const MazeState& maze) {
    widthCells_ = std::max(0, maze.widthCells);
    heightCells_ = std::max(0, maze.heightCells);
    cellSizeUnits_ = std::max(1, maze.cellSizeUnits);
}

int CellCoordCache::ClampCellX(float worldX) const {
    if (widthCells_ <= 0) {
        return 0;
    }
    const int raw = static_cast<int>(worldX / static_cast<float>(cellSizeUnits_));
    return std::max(0, std::min(widthCells_ - 1, raw));
}

int CellCoordCache::ClampCellY(float worldY) const {
    if (heightCells_ <= 0) {
        return 0;
    }
    const int raw = static_cast<int>(worldY / static_cast<float>(cellSizeUnits_));
    return std::max(0, std::min(heightCells_ - 1, raw));
}

MazeCellCoord CellCoordCache::WorldToCell(const Vec2f& worldPos) const {
    return MazeCellCoord{
        .x = ClampCellX(worldPos.x),
        .y = ClampCellY(worldPos.y),
    };
}

int CellCoordCache::CellIndex(int cellX, int cellY) const {
    return cellY * widthCells_ + cellX;
}

int CellCoordCache::CellHash(int cellX, int cellY) const {
    return CellIndex(cellX, cellY);
}

Vec2f CellCoordCache::CellCenter(int cellX, int cellY) const {
    const float cellSize = static_cast<float>(cellSizeUnits_);
    return Vec2f{
        .x = (static_cast<float>(cellX) + 0.5F) * cellSize,
        .y = (static_cast<float>(cellY) + 0.5F) * cellSize,
    };
}

bool CellCoordCache::IsValidCell(int cellX, int cellY) const {
    return cellX >= 0 && cellX < widthCells_ && cellY >= 0 && cellY < heightCells_;
}

bool CellCoordCache::UpdatePlayerCell(const Vec2f& playerPosition) {
    if (widthCells_ <= 0 || heightCells_ <= 0) {
        return false;
    }
    const MazeCellCoord next = WorldToCell(playerPosition);
    const int nextHash = CellHash(next.x, next.y);
    if (!hasPlayerCell_ || nextHash != playerCellHash_) {
        playerCell_ = next;
        playerCellHash_ = nextHash;
        playerCellVersion_ += 1U;
        hasPlayerCell_ = true;
        return true;
    }
    return false;
}

}  // namespace game::navigation
