#include "game/spatial/EnemyCellOccupancy.h"

#include <algorithm>
#include <cmath>
#include "game/model/WorldState.h"

namespace game::spatial {

void EnemyCellOccupancy::Reserve(int widthCells, int heightCells, int maxEnemies, int cellSizeUnits) {
    widthCells_ = std::max(0, widthCells);
    heightCells_ = std::max(0, heightCells);
    maxEnemies_ = std::max(0, maxEnemies);
    cellSizeUnits_ = std::max(1, cellSizeUnits);

    const std::size_t cellCount =
        static_cast<std::size_t>(widthCells_) * static_cast<std::size_t>(heightCells_);
    const std::size_t enemyCount = static_cast<std::size_t>(maxEnemies_);

    cellFirst_.resize(cellCount, kInvalid);
    enemyNext_.resize(enemyCount, kInvalid);
    enemyPrev_.resize(enemyCount, kInvalid);
    enemyCell_.resize(enemyCount, kInvalid);
}

void EnemyCellOccupancy::BuildFromPositions(const WorldState& world,
    const std::vector<Vec2f>* positionsOverride,
    const std::vector<std::uint8_t>* includeMask) {
    if (!IsConfigured()) {
        return;
    }

    std::fill(cellFirst_.begin(), cellFirst_.end(), kInvalid);
    std::fill(enemyNext_.begin(), enemyNext_.end(), kInvalid);
    std::fill(enemyPrev_.begin(), enemyPrev_.end(), kInvalid);
    std::fill(enemyCell_.begin(), enemyCell_.end(), kInvalid);

    const bool useOverride = positionsOverride != nullptr &&
        positionsOverride->size() == world.enemies.size();
    const bool useMask = includeMask != nullptr &&
        includeMask->size() == world.enemies.size();
    for (int i = 0; i < static_cast<int>(world.enemies.size()); ++i) {
        if (i >= maxEnemies_) {
            break;
        }
        if (useMask && (*includeMask)[static_cast<std::size_t>(i)] == 0U) {
            continue;
        }
        const EnemyTank& enemy = world.enemies[static_cast<std::size_t>(i)];
        if (!enemy.alive) {
            continue;
        }
        const Vec2f pos = useOverride
            ? (*positionsOverride)[static_cast<std::size_t>(i)]
            : enemy.position;
        SetCell(i, WorldToCellX(pos.x), WorldToCellY(pos.y));
    }
}

void EnemyCellOccupancy::SetCell(int enemyIndex, int cellX, int cellY) {
    if (enemyIndex < 0 || enemyIndex >= maxEnemies_ ||
        widthCells_ <= 0 || heightCells_ <= 0) {
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

void EnemyCellOccupancy::GetEnemiesAlongRay(const std::vector<EnemyTank>& enemies,
    int selfIndex, const Vec2f& from, const Vec2f& dir, float maxDist,
    std::vector<int>& out) const {
    if (maxDist <= 0.0F || widthCells_ <= 0 || heightCells_ <= 0 || enemies.empty()) {
        return;
    }

    std::vector<bool> seen(enemies.size(), false);
    if (selfIndex >= 0 && selfIndex < static_cast<int>(enemies.size())) {
        seen[static_cast<std::size_t>(selfIndex)] = true;
    }

    const float step = static_cast<float>(cellSizeUnits_) * 0.5F;
    const int steps = std::max(1, static_cast<int>(std::ceil(maxDist / step)));
    int prevCx = -1;
    int prevCy = -1;

    for (int s = 0; s <= steps; ++s) {
        const float t = std::min(1.0F, static_cast<float>(s) / static_cast<float>(steps));
        const float d = t * maxDist;
        const Vec2f sample{
            .x = from.x + dir.x * d,
            .y = from.y + dir.y * d,
        };
        const int cx = WorldToCellX(sample.x);
        const int cy = WorldToCellY(sample.y);
        if (cx == prevCx && cy == prevCy) {
            continue;
        }
        prevCx = cx;
        prevCy = cy;

        VisitCellAndNeighbors(cx, cy, [&](int cellIdx) {
            for (int i = cellFirst_[static_cast<std::size_t>(cellIdx)];
                 i != kInvalid;
                 i = enemyNext_[static_cast<std::size_t>(i)]) {
                if (i < 0 || i >= static_cast<int>(enemies.size())) {
                    continue;
                }
                if (seen[static_cast<std::size_t>(i)]) {
                    continue;
                }
                if (!enemies[static_cast<std::size_t>(i)].alive) {
                    continue;
                }
                seen[static_cast<std::size_t>(i)] = true;
                out.push_back(i);
            }
        });
    }
}

int EnemyCellOccupancy::WorldToCellX(float x) const {
    const float cellSize = static_cast<float>(cellSizeUnits_);
    const int cx = static_cast<int>(std::floor(x / cellSize));
    return std::max(0, std::min(widthCells_ - 1, cx));
}

int EnemyCellOccupancy::WorldToCellY(float y) const {
    const float cellSize = static_cast<float>(cellSizeUnits_);
    const int cy = static_cast<int>(std::floor(y / cellSize));
    return std::max(0, std::min(heightCells_ - 1, cy));
}

int EnemyCellOccupancy::CellIndex(int cx, int cy) const {
    return cy * widthCells_ + cx;
}

bool EnemyCellOccupancy::IsValidCell(int cx, int cy) const {
    return cx >= 0 && cx < widthCells_ && cy >= 0 && cy < heightCells_;
}

void EnemyCellOccupancy::VisitCellAndNeighbors(int cx, int cy,
    std::function<void(int cellIdx)> visitor) const {
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int ncx = cx + dx;
            const int ncy = cy + dy;
            if (IsValidCell(ncx, ncy)) {
                visitor(CellIndex(ncx, ncy));
            }
        }
    }
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
