#include "game/navigation/PlayerFlowField.h"

#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <future>
#include <limits>
#include "game/model/GameplayConstants.h"
#include "game/model/WorldState.h"
#include "game/navigation/FlowRebuildWorker.h"

namespace game::navigation {

namespace {
bool IsCellOccupiedByBase(
    const CellCoordCache& cellCache, int cellX, int cellY,
    const std::vector<EnemyBase>& bases) {
    const Vec2f center = cellCache.CellCenter(cellX, cellY);
    const float halfBase = GameplayConstants::kEnemyBaseSizeUnits * 0.5F;
    constexpr float kClearanceUnits = 0.0F;
    for (const EnemyBase& base : bases) {
        if (base.destroyed) {
            continue;
        }
        const float dx = std::fabs(center.x - base.position.x);
        const float dy = std::fabs(center.y - base.position.y);
        if (dx <= halfBase + kClearanceUnits && dy <= halfBase + kClearanceUnits) {
            return true;
        }
    }
    return false;
}

}  // namespace

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

void PlayerFlowField::Rebuild(const MazeState& maze, const CellCoordCache& cellCache,
    const std::vector<EnemyBase>& bases) {
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
    // Do not assign flow to player cell; leave nextCellHash = -1

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
            if (IsCellOccupiedByBase(cellCache, neighborX, neighborY, bases)) {
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

    // Base-occupied cells were skipped in BFS. Assign outward flow direction
    // toward a traversable neighbor whose flow does not point back at this base cell.
    // Base always fits in one cell (kEnemyBaseSizeUnits < cell size); use the cell
    // containing the base center.
    constexpr std::array<int, 4> kDxOut{1, -1, 0, 0};
    constexpr std::array<int, 4> kDyOut{0, 0, 1, -1};
    for (const EnemyBase& base : bases) {
        if (base.destroyed) {
            continue;
        }
        const MazeCellCoord cell = cellCache.WorldToCell(base.position);
        const int cx = cell.x;
        const int cy = cell.y;
        const int cellHash = cellCache.CellHash(cx, cy);
        int bestNeighborHash = -1;
        int bestDist = std::numeric_limits<int>::max();
        for (int i = 0; i < 4; ++i) {
            const int nx = cx + kDxOut[static_cast<std::size_t>(i)];
            const int ny = cy + kDyOut[static_cast<std::size_t>(i)];
            if (!cellCache.IsValidCell(nx, ny)) {
                continue;
            }
            if (!CanTraverse(maze, cx, cy, nx, ny)) {
                continue;
            }
            const int neighborHash = cellCache.CellHash(nx, ny);
            const int neighborNext = nextCellHash_[static_cast<std::size_t>(neighborHash)];
            if (neighborNext < 0 || neighborNext == cellHash) {
                continue;
            }
            const int dist = distance_[static_cast<std::size_t>(neighborHash)];
            if (dist < bestDist) {
                bestDist = dist;
                bestNeighborHash = neighborHash;
            }
        }
        if (bestNeighborHash >= 0) {
            nextCellHash_[static_cast<std::size_t>(cellHash)] = bestNeighborHash;
        }
    }

    builtForPlayerCellVersion_ = cellCache.PlayerCellVersion();
    builtForCellX_ = playerCell.x;
    builtForCellY_ = playerCell.y;
    hasBuild_ = true;
}

void PlayerFlowField::Invalidate() {
    hasBuild_ = false;
    lastCellX_ = -1;
    lastCellY_ = -1;
}

void PlayerFlowField::OverrideNextCellHash(int fromCellHash, int toCellHash) {
    if (!hasBuild_) {
        return;
    }
    if (fromCellHash < 0 || toCellHash < 0) {
        return;
    }
    if (fromCellHash >= static_cast<int>(nextCellHash_.size()) || toCellHash >= static_cast<int>(nextCellHash_.size())) {
        return;
    }
    nextCellHash_[static_cast<std::size_t>(fromCellHash)] = toCellHash;
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

namespace {
constexpr int kMaxFlowFieldAge = 2;

void ScheduleRebuild(game::navigation::FlowRebuildWorker& flowWorker,
    NavigationRuntimeCache& navigationCache,
    const WorldState& world) {
    if (flowWorker.inFlight) {
        return;
    }
    flowWorker.buildGeneration = navigationCache.flowFieldInvalidationGeneration;
    flowWorker.inFlight = true;
    MazeState mazeCopy = world.maze;
    game::navigation::CellCoordCache cacheCopy = navigationCache.cellCoords;
    std::vector<EnemyBase> basesCopy = world.enemyBases;
    flowWorker.future = std::async(
        std::launch::async,
        [&pending = flowWorker.pendingFlowField,
         m = std::move(mazeCopy),
         c = std::move(cacheCopy),
         b = std::move(basesCopy)]() mutable {
            pending.Rebuild(m, c, b);
        });
}
}  // namespace

void PlayerFlowField::OnPlayerCellChanged(int newCellX, int newCellY,
    FlowRebuildWorker& flowWorker,
    ::NavigationRuntimeCache& navigationCache,
    const ::WorldState& world,
    FlowFieldUpdateStats* outStats) {
    if (!cacheActive_) {
        return;
    }
    if (lastCellX_ == newCellX && lastCellY_ == newCellY) {
        return;
    }
    const int prevX = lastCellX_;
    const int prevY = lastCellY_;

    age_ += 1;
    if (outStats) {
        outStats->playerCellChanged = true;
    }

    if (age_ > kMaxFlowFieldAge) {
        if (!flowWorker.inFlight) {
            ScheduleRebuild(flowWorker, navigationCache, world);
            age_ = 0;
            if (outStats) {
                outStats->rebuildScheduled = true;
            }
        }
    } else {
        if (hasBuild_ && widthCells_ > 0 &&
            prevX >= 0 && prevY >= 0 &&
            newCellX >= 0 && newCellY >= 0) {
            const int prevHash = prevY * widthCells_ + prevX;
            const int currHash = newCellY * widthCells_ + newCellX;
            if (prevHash != currHash) {
                OverrideNextCellHash(prevHash, currHash);
            }
        }
    }

    lastCellX_ = newCellX;
    lastCellY_ = newCellY;
}

void PlayerFlowField::Update(FlowRebuildWorker& flowWorker,
    ::NavigationRuntimeCache& navigationCache,
    const ::WorldState& world,
    int currentCellX, int currentCellY,
    FlowFieldUpdateStats* outStats) {
    if (!flowWorker.inFlight ||
        flowWorker.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    } else {
        flowWorker.future.get();
        if (flowWorker.buildGeneration >= navigationCache.flowFieldInvalidationGeneration) {
            std::swap(*this, flowWorker.pendingFlowField);
            lastCellX_ = builtForCellX_;
            lastCellY_ = builtForCellY_;
        }
        flowWorker.inFlight = false;
    }

    if (lastCellX_ != currentCellX || lastCellY_ != currentCellY) {
        OnPlayerCellChanged(currentCellX, currentCellY, flowWorker, navigationCache, world, outStats);
        return;
    }

    if (cacheActive_ && !hasBuild_ && !flowWorker.inFlight) {
        ScheduleRebuild(flowWorker, navigationCache, world);
        age_ = 0;
        if (outStats) {
            outStats->rebuildScheduled = true;
        }
    }
}

}  // namespace game::navigation
