#include "game/spatial/EnemySpatialGrid.h"

#include <algorithm>
#include <cmath>
#include "game/model/WorldState.h"

namespace game::spatial {

void EnemySpatialGrid::BuildFromPositions(const WorldState& world,
    const std::vector<Vec2f>* positionsOverride,
    const std::vector<std::uint8_t>* includeMask) {
    widthCells_ = world.maze.widthCells;
    heightCells_ = world.maze.heightCells;
    const int total = widthCells_ * heightCells_;
    if (total <= 0) {
        return;
    }
    cells_.resize(static_cast<std::size_t>(total));
    for (auto& cell : cells_) {
        cell.clear();
    }
    activeCells_.clear();

    const bool useOverride = positionsOverride != nullptr &&
        positionsOverride->size() == world.enemies.size();
    const bool useMask = includeMask != nullptr &&
        includeMask->size() == world.enemies.size();
    for (int i = 0; i < static_cast<int>(world.enemies.size()); ++i) {
        if (useMask && (*includeMask)[static_cast<std::size_t>(i)] == 0U) {
            continue;
        }
        const EnemyTank& e = world.enemies[static_cast<std::size_t>(i)];
        if (!e.alive) {
            continue;
        }
        float x, y;
        if (useOverride) {
            x = (*positionsOverride)[static_cast<std::size_t>(i)].x;
            y = (*positionsOverride)[static_cast<std::size_t>(i)].y;
        } else {
            x = e.position.x;
            y = e.position.y;
        }
        InsertEnemyAt(i, x, y);
    }
}

void EnemySpatialGrid::BuildFromSegments(const WorldState& world,
    const std::vector<Vec2f>& frameStartPositions,
    const std::vector<std::uint8_t>* includeMask) {
    widthCells_ = world.maze.widthCells;
    heightCells_ = world.maze.heightCells;
    const int total = widthCells_ * heightCells_;
    if (total <= 0) {
        return;
    }
    cells_.resize(static_cast<std::size_t>(total));
    for (auto& cell : cells_) {
        cell.clear();
    }
    activeCells_.clear();

    const bool useMask = includeMask != nullptr &&
        includeMask->size() == world.enemies.size();
    for (int i = 0; i < static_cast<int>(world.enemies.size()); ++i) {
        if (useMask && (*includeMask)[static_cast<std::size_t>(i)] == 0U) {
            continue;
        }
        const EnemyTank& e = world.enemies[static_cast<std::size_t>(i)];
        if (!e.alive) {
            continue;
        }
        const Vec2f& start = frameStartPositions[static_cast<std::size_t>(i)];
        InsertEnemyAt(i, start.x, start.y);
        const int startCx = WorldToCellX(start.x);
        const int startCy = WorldToCellY(start.y);
        const int endCx = WorldToCellX(e.position.x);
        const int endCy = WorldToCellY(e.position.y);
        if (startCx != endCx || startCy != endCy) {
            InsertEnemyAt(i, e.position.x, e.position.y);
        }
    }
}

void EnemySpatialGrid::ForEachPairInSameOrAdjacentCell(
    const std::vector<EnemyTank>& enemies,
    std::function<void(int i, int j)> fn) const {
    const int maxN = static_cast<int>(enemies.size());
    if (maxN <= 0) {
        return;
    }
    std::vector<bool> pairChecked(static_cast<std::size_t>(maxN * maxN), false);

    for (int cellIdx : activeCells_) {
        const std::vector<int>& indices = cells_[static_cast<std::size_t>(cellIdx)];
        for (std::size_t a = 0; a < indices.size(); ++a) {
            const int i = indices[a];
            if (i >= maxN || !enemies[static_cast<std::size_t>(i)].alive) {
                continue;
            }
            for (std::size_t b = a + 1; b < indices.size(); ++b) {
                const int j = indices[b];
                if (i == j) {
                    continue;
                }
                if (j >= maxN || !enemies[static_cast<std::size_t>(j)].alive) {
                    continue;
                }
                const int lo = std::min(i, j);
                const int hi = std::max(i, j);
                if (lo == hi) {
                    continue;
                }
                const std::size_t key = static_cast<std::size_t>(lo * maxN + hi);
                if (pairChecked[key]) {
                    continue;
                }
                pairChecked[key] = true;
                fn(lo, hi);
            }
        }

        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const int cx = (cellIdx % widthCells_) + dx;
                const int cy = (cellIdx / widthCells_) + dy;
                if (!IsValidCell(cx, cy)) {
                    continue;
                }
                const int neighborIdx = CellIndex(cx, cy);
                if (neighborIdx <= cellIdx) {
                    continue;
                }
                const std::vector<int>& curr = cells_[static_cast<std::size_t>(cellIdx)];
                const std::vector<int>& neigh = cells_[static_cast<std::size_t>(neighborIdx)];
                for (int i : curr) {
                    if (i >= maxN || !enemies[static_cast<std::size_t>(i)].alive) {
                        continue;
                    }
                    for (int j : neigh) {
                        if (i == j) {
                            continue;
                        }
                        if (j >= maxN || !enemies[static_cast<std::size_t>(j)].alive) {
                            continue;
                        }
                        const int lo = std::min(i, j);
                        const int hi = std::max(i, j);
                        if (lo == hi) {
                            continue;
                        }
                        const std::size_t key = static_cast<std::size_t>(lo * maxN + hi);
                        if (pairChecked[key]) {
                            continue;
                        }
                        pairChecked[key] = true;
                        fn(lo, hi);
                    }
                }
            }
        }
    }
}

void EnemySpatialGrid::GetEnemiesNearPoint(const std::vector<EnemyTank>& enemies,
    int selfIndex, const Vec2f& point, std::vector<int>& out) const {
    const int cx = WorldToCellX(point.x);
    const int cy = WorldToCellY(point.y);
    std::vector<bool> seen(static_cast<std::size_t>(enemies.size()), false);
    seen[static_cast<std::size_t>(selfIndex)] = true;

    VisitCellAndNeighbors(cx, cy, [&](int cellIdx) {
        for (int i : cells_[static_cast<std::size_t>(cellIdx)]) {
            if (seen[static_cast<std::size_t>(i)]) {
                continue;
            }
            if (i < static_cast<int>(enemies.size()) && enemies[static_cast<std::size_t>(i)].alive) {
                seen[static_cast<std::size_t>(i)] = true;
                out.push_back(i);
            }
        }
    });
}

void EnemySpatialGrid::GetEnemiesAlongRay(const std::vector<EnemyTank>& enemies,
    int selfIndex, const Vec2f& from, const Vec2f& dir, float maxDist,
    std::vector<int>& out) const {
    std::vector<bool> seen(static_cast<std::size_t>(enemies.size()), false);
    seen[static_cast<std::size_t>(selfIndex)] = true;

    const float step = kCellSizeUnits * 0.5F;
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
            for (int i : cells_[static_cast<std::size_t>(cellIdx)]) {
                if (seen[static_cast<std::size_t>(i)]) {
                    continue;
                }
                if (i < static_cast<int>(enemies.size()) && enemies[static_cast<std::size_t>(i)].alive) {
                    seen[static_cast<std::size_t>(i)] = true;
                    out.push_back(i);
                }
            }
        });
    }
}

int EnemySpatialGrid::WorldToCellX(float x) const {
    const int cx = static_cast<int>(std::floor(x / kCellSizeUnits));
    return std::max(0, std::min(widthCells_ - 1, cx));
}

int EnemySpatialGrid::WorldToCellY(float y) const {
    const int cy = static_cast<int>(std::floor(y / kCellSizeUnits));
    return std::max(0, std::min(heightCells_ - 1, cy));
}

int EnemySpatialGrid::CellIndex(int cx, int cy) const {
    return cy * widthCells_ + cx;
}

bool EnemySpatialGrid::IsValidCell(int cx, int cy) const {
    return cx >= 0 && cx < widthCells_ && cy >= 0 && cy < heightCells_;
}

void EnemySpatialGrid::InsertEnemyAt(int enemyIndex, float worldX, float worldY) {
    const int cx = WorldToCellX(worldX);
    const int cy = WorldToCellY(worldY);
    const int idx = CellIndex(cx, cy);
    std::vector<int>& cell = cells_[static_cast<std::size_t>(idx)];
    if (cell.empty()) {
        activeCells_.push_back(idx);
    }
    cell.push_back(enemyIndex);
}

void EnemySpatialGrid::VisitCellAndNeighbors(int cx, int cy,
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

}  // namespace game::spatial
