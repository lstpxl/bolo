#include "game/spatial/EnemyCellOccupancy.h"

#include <algorithm>

namespace game::spatial {

void EnemyCellOccupancy::Reserve(int widthCells, int heightCells, int maxEnemies) {
    widthCells_ = std::max(0, widthCells);
    heightCells_ = std::max(0, heightCells);
    maxEnemies_ = std::max(0, maxEnemies);

    const std::size_t cellCount =
        static_cast<std::size_t>(widthCells_) * static_cast<std::size_t>(heightCells_);
    const std::size_t enemyCount = static_cast<std::size_t>(maxEnemies_);

    cellFirst_.resize(cellCount, kInvalid);
    enemyNext_.resize(enemyCount, kInvalid);
    enemyPrev_.resize(enemyCount, kInvalid);
    enemyCell_.resize(enemyCount, kInvalid);
}

void EnemyCellOccupancy::SetCell(int enemyIndex, int cellX, int cellY) {
    if (enemyIndex < 0 || enemyIndex >= maxEnemies_) {
        return;
    }
    const int cx = std::max(0, std::min(widthCells_ - 1, cellX));
    const int cy = std::max(0, std::min(heightCells_ - 1, cellY));
    const int cellIdx = CellIndex(cx, cy);

    if (enemyCell_[static_cast<std::size_t>(enemyIndex)] == cellIdx) {
        return;  // already in this cell
    }
    Unlink(enemyIndex);
    Link(enemyIndex, cellIdx);
}

void EnemyCellOccupancy::Remove(int enemyIndex) {
    if (enemyIndex < 0 || enemyIndex >= maxEnemies_) {
        return;
    }
    Unlink(enemyIndex);
}

void EnemyCellOccupancy::ForEachInCell(int cellX, int cellY, int selfIndex,
    std::function<void(int enemyIndex)> fn) const {
    if (!IsValidCell(cellX, cellY) || !fn) {
        return;
    }
    const int cellIdx = CellIndex(cellX, cellY);
    for (int i = cellFirst_[static_cast<std::size_t>(cellIdx)]; i != kInvalid;
         i = enemyNext_[static_cast<std::size_t>(i)]) {
        if (i != selfIndex) {
            fn(i);
        }
    }
}

bool EnemyCellOccupancy::HasOtherInCell(int cellX, int cellY, int selfIndex) const {
    if (!IsValidCell(cellX, cellY)) {
        return false;
    }
    const int cellIdx = CellIndex(cellX, cellY);
    for (int i = cellFirst_[static_cast<std::size_t>(cellIdx)]; i != kInvalid;
         i = enemyNext_[static_cast<std::size_t>(i)]) {
        if (i != selfIndex) {
            return true;
        }
    }
    return false;
}

int EnemyCellOccupancy::CountInCell(int cellX, int cellY) const {
    if (!IsValidCell(cellX, cellY)) {
        return 0;
    }
    int count = 0;
    const int cellIdx = CellIndex(cellX, cellY);
    for (int i = cellFirst_[static_cast<std::size_t>(cellIdx)]; i != kInvalid;
         i = enemyNext_[static_cast<std::size_t>(i)]) {
        count += 1;
    }
    return count;
}

int EnemyCellOccupancy::CellIndex(int cx, int cy) const {
    return cy * widthCells_ + cx;
}

bool EnemyCellOccupancy::IsValidCell(int cx, int cy) const {
    return cx >= 0 && cx < widthCells_ && cy >= 0 && cy < heightCells_;
}

void EnemyCellOccupancy::Unlink(int enemyIndex) {
    const int c = enemyCell_[static_cast<std::size_t>(enemyIndex)];
    if (c < 0) {
        return;
    }
    const std::size_t idx = static_cast<std::size_t>(enemyIndex);
    const int p = enemyPrev_[idx];
    const int n = enemyNext_[idx];
    if (p >= 0) {
        enemyNext_[static_cast<std::size_t>(p)] = n;
    } else {
        cellFirst_[static_cast<std::size_t>(c)] = n;
    }
    if (n >= 0) {
        enemyPrev_[static_cast<std::size_t>(n)] = p;
    }
    enemyCell_[idx] = kInvalid;
}

void EnemyCellOccupancy::Link(int enemyIndex, int cellIdx) {
    const std::size_t idx = static_cast<std::size_t>(enemyIndex);
    enemyNext_[idx] = cellFirst_[static_cast<std::size_t>(cellIdx)];
    enemyPrev_[idx] = kInvalid;
    const int prevHead = cellFirst_[static_cast<std::size_t>(cellIdx)];
    if (prevHead >= 0) {
        enemyPrev_[static_cast<std::size_t>(prevHead)] = enemyIndex;
    }
    cellFirst_[static_cast<std::size_t>(cellIdx)] = enemyIndex;
    enemyCell_[idx] = cellIdx;
}

}  // namespace game::spatial
