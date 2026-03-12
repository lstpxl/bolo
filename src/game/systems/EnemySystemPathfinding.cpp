#include "game/systems/EnemySystemPathfinding.h"

#include <algorithm>
#include <array>
#include <limits>
#include <vector>
#include "core/Profiling.h"
#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"
#include "game/systems/EnemySystemHelpers.h"
#include "game/systems/EnemySystemInternal.h"

namespace {
int ClampCellX(const game::navigation::CellCoordCache& cellCache, float x) {
    return cellCache.ClampCellX(x);
}

int ClampCellY(const game::navigation::CellCoordCache& cellCache, float y) {
    return cellCache.ClampCellY(y);
}

int CellIndex(const game::navigation::CellCoordCache& cellCache, int x, int y) {
    return cellCache.CellIndex(x, y);
}

Vec2f CellCenter(const game::navigation::CellCoordCache& cellCache, int x, int y) {
    return cellCache.CellCenter(x, y);
}

bool IsPointInUndestroyedBase(const WorldState& world, const Vec2f& point, float clearanceUnits) {
    return game::geometry::IsPointInUndestroyedBase(world, point, clearanceUnits);
}

bool CanStepToNeighbor(
    const WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    int x,
    int y,
    int nx,
    int ny) {
    if (nx < 0 || ny < 0 || nx >= world.maze.widthCells || ny >= world.maze.heightCells) {
        return false;
    }
    const bool startInsideBase =
        IsPointInUndestroyedBase(world, CellCenter(cellCache, x, y), GameplayConstants::kTankCollisionRadiusUnits);
    if (!startInsideBase &&
        IsPointInUndestroyedBase(world, CellCenter(cellCache, nx, ny), GameplayConstants::kTankCollisionRadiusUnits)) {
        return false;
    }
    const MazeCell& cell = world.maze.cells[static_cast<std::size_t>(CellIndex(cellCache, x, y))];
    if (nx == x + 1) {
        return !cell.eastWall;
    }
    if (nx == x - 1) {
        return !cell.westWall;
    }
    if (ny == y + 1) {
        return !cell.southWall;
    }
    if (ny == y - 1) {
        return !cell.northWall;
    }
    return false;
}

struct AStarNode {
    int parent = -1;
    float g = std::numeric_limits<float>::infinity();
    float f = std::numeric_limits<float>::infinity();
    bool open = false;
    bool closed = false;
};

struct OpenNode {
    int index = 0;
    float f = 0.0F;
};

struct OpenNodeCompare {
    bool operator()(const OpenNode& a, const OpenNode& b) const {
        return a.f > b.f;
    }
};

struct PathfindingPool {
    std::vector<bool> occupied{};
    std::vector<AStarNode> nodes{};
    std::vector<OpenNode> openHeap{};
    std::vector<int> pathCells{};

    void EnsureCapacity(int totalCells) {
        const auto n = static_cast<std::size_t>(totalCells);
        if (occupied.size() < n) {
            occupied.resize(n);
        }
        if (nodes.size() < n) {
            nodes.resize(n);
        }
        if (openHeap.capacity() < static_cast<std::size_t>(std::min(1024, totalCells))) {
            openHeap.reserve(static_cast<std::size_t>(std::min(1024, totalCells)));
        }
        if (pathCells.capacity() < static_cast<std::size_t>(std::min(512, totalCells))) {
            pathCells.reserve(static_cast<std::size_t>(std::min(512, totalCells)));
        }
    }
};

PathfindingPool& GetPathfindingPool() {
    static PathfindingPool pool;
    return pool;
}

float HeuristicManhattan(int x, int y, int tx, int ty) {
    return static_cast<float>(game::navigation::CellDistance(
        game::navigation::MazeCellCoord{.x = x, .y = y},
        game::navigation::MazeCellCoord{.x = tx, .y = ty}));
}

void BuildEnemyOccupancy(
    const GameState& state,
    const game::navigation::CellCoordCache& cellCache,
    int ignoreEnemyIndex,
    std::vector<bool>& occupied) {
    const int totalCells = state.world.maze.widthCells * state.world.maze.heightCells;
    occupied.assign(static_cast<std::size_t>(totalCells), false);
    for (int i = 0; i < static_cast<int>(state.world.enemies.size()); ++i) {
        if (i == ignoreEnemyIndex) {
            continue;
        }
        const EnemyTank& enemy = state.world.enemies[static_cast<std::size_t>(i)];
        if (!enemy.alive) {
            continue;
        }
        const int cx = ClampCellX(cellCache, enemy.position.x);
        const int cy = ClampCellY(cellCache, enemy.position.y);
        occupied[static_cast<std::size_t>(CellIndex(cellCache, cx, cy))] = true;
    }
}

bool BuildAssassinPathToTarget(
    GameState& state,
    const game::navigation::CellCoordCache& cellCache,
    EnemyTank& enemy,
    int enemyIndex,
    const Vec2f& target) {
    const Vec2f previousPlayerPosition = state.world.player.position;
    state.world.player.position = target;
    const bool built = BuildAssassinPath(state, cellCache, enemy, enemyIndex);
    state.world.player.position = previousPlayerPosition;
    return built;
}

Vec2f RandomMazePoint(
    const WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    Random& random) {
    const int cellX = random.NextInt(0, world.maze.widthCells - 1);
    const int cellY = random.NextInt(0, world.maze.heightCells - 1);
    return CellCenter(cellCache, cellX, cellY);
}
}  // namespace

bool BuildAssassinPath(
    GameState& state,
    const game::navigation::CellCoordCache& cellCache,
    EnemyTank& enemy,
    int enemyIndex) {
    profiling::ScopedProfile totalScope(profiling::Scope::PathfindingTotal, true);
    gEnemyRuntimeWindowStats.navPathBuildCalls += 1;
    const int width = state.world.maze.widthCells;
    const int height = state.world.maze.heightCells;
    const int totalCells = width * height;
    if (totalCells <= 0) {
        return false;
    }

    const int startX = ClampCellX(cellCache, enemy.position.x);
    const int startY = ClampCellY(cellCache, enemy.position.y);
    const int goalX = ClampCellX(cellCache, state.world.player.position.x);
    const int goalY = ClampCellY(cellCache, state.world.player.position.y);
    const int startIndex = CellIndex(cellCache, startX, startY);
    const int goalIndex = CellIndex(cellCache, goalX, goalY);
    if (startIndex == goalIndex) {
        enemy.pathWaypointCount = 0;
        enemy.pathWaypointIndex = 0;
        return false;
    }

    PathfindingPool& pool = GetPathfindingPool();
    pool.EnsureCapacity(totalCells);

    std::vector<bool>& occupied = pool.occupied;
    std::vector<AStarNode>& nodes = pool.nodes;
    std::vector<OpenNode>& openHeap = pool.openHeap;
    OpenNodeCompare cmp{};

    {
        profiling::ScopedProfile occupancyScope(profiling::Scope::PathfindingOccupancy, true);
        BuildEnemyOccupancy(state, cellCache, enemyIndex, occupied);
    }
    occupied[static_cast<std::size_t>(startIndex)] = false;
    occupied[static_cast<std::size_t>(goalIndex)] = false;

    const std::size_t n = static_cast<std::size_t>(totalCells);
    for (std::size_t i = 0; i < n; ++i) {
        nodes[i] = AStarNode{};
    }
    nodes[static_cast<std::size_t>(startIndex)].g = 0.0F;
    nodes[static_cast<std::size_t>(startIndex)].f = HeuristicManhattan(startX, startY, goalX, goalY);
    nodes[static_cast<std::size_t>(startIndex)].open = true;
    openHeap.clear();
    openHeap.push_back(OpenNode{.index = startIndex, .f = nodes[static_cast<std::size_t>(startIndex)].f});
    std::push_heap(openHeap.begin(), openHeap.end(), cmp);

    const std::array<int, 4> dx{1, -1, 0, 0};
    const std::array<int, 4> dy{0, 0, 1, -1};
    bool found = false;
    {
        profiling::ScopedProfile searchScope(profiling::Scope::PathfindingSearch, true);
        while (!openHeap.empty()) {
            std::pop_heap(openHeap.begin(), openHeap.end(), cmp);
            const OpenNode top = openHeap.back();
            openHeap.pop_back();
            AStarNode& current = nodes[static_cast<std::size_t>(top.index)];
            if (current.closed) {
                continue;
            }
            current.closed = true;
            if (top.index == goalIndex) {
                found = true;
                break;
            }

            const int x = top.index % width;
            const int y = top.index / width;
            for (int i = 0; i < 4; ++i) {
                const int nx = x + dx[static_cast<std::size_t>(i)];
                const int ny = y + dy[static_cast<std::size_t>(i)];
                if (!CanStepToNeighbor(state.world, cellCache, x, y, nx, ny)) {
                    continue;
                }
                const int ni = CellIndex(cellCache, nx, ny);
                if (occupied[static_cast<std::size_t>(ni)] && ni != goalIndex) {
                    continue;
                }

                AStarNode& neighbor = nodes[static_cast<std::size_t>(ni)];
                if (neighbor.closed) {
                    continue;
                }
                const float tentativeG = current.g + 1.0F;
                if (tentativeG >= neighbor.g) {
                    continue;
                }
                neighbor.parent = top.index;
                neighbor.g = tentativeG;
                neighbor.f = tentativeG + HeuristicManhattan(nx, ny, goalX, goalY);
                neighbor.open = true;
                openHeap.push_back(OpenNode{.index = ni, .f = neighbor.f});
                std::push_heap(openHeap.begin(), openHeap.end(), cmp);
            }
        }
    }

    if (!found) {
        enemy.pathWaypointCount = 0;
        enemy.pathWaypointIndex = 0;
        return false;
    }

    {
        profiling::ScopedProfile postprocessScope(profiling::Scope::PathfindingPostprocess, true);
        std::vector<int>& pathCells = pool.pathCells;
        pathCells.clear();
        int trace = goalIndex;
        while (trace != -1) {
            pathCells.push_back(trace);
            if (trace == startIndex) {
                break;
            }
            trace = nodes[static_cast<std::size_t>(trace)].parent;
        }
        if (pathCells.empty() || pathCells.back() != startIndex) {
            enemy.pathWaypointCount = 0;
            enemy.pathWaypointIndex = 0;
            return false;
        }
        std::reverse(pathCells.begin(), pathCells.end());

        enemy.pathWaypointCount = 0;
        enemy.pathWaypointIndex = 0;
        int lastStepX = 0;
        int lastStepY = 0;
        for (int i = 1; i < static_cast<int>(pathCells.size()); ++i) {
            const int prev = pathCells[static_cast<std::size_t>(i - 1)];
            const int curr = pathCells[static_cast<std::size_t>(i)];
            const int px = prev % width;
            const int py = prev / width;
            const int cx = curr % width;
            const int cy = curr / width;
            const int stepX = cx - px;
            const int stepY = cy - py;
            const bool turnPoint =
                (i == 1) ||
                (stepX != lastStepX) ||
                (stepY != lastStepY) ||
                (i == static_cast<int>(pathCells.size()) - 1);
            lastStepX = stepX;
            lastStepY = stepY;
            if (!turnPoint) {
                continue;
            }
            if (enemy.pathWaypointCount >= EnemyTank::kMaxPathWaypoints) {
                break;
            }
            enemy.pathWaypoints[static_cast<std::size_t>(enemy.pathWaypointCount)] = CellCenter(cellCache, cx, cy);
            ++enemy.pathWaypointCount;
        }
    }
    const bool success = enemy.pathWaypointCount > 0;
    if (success) {
        gEnemyRuntimeWindowStats.navPathBuildSuccesses += 1;
    }
    return success;
}

bool BuildAssassinPathToFarRandomTarget(
    GameState& state,
    const game::navigation::CellCoordCache& cellCache,
    EnemyTank& enemy,
    int enemyIndex,
    Random& random) {
    profiling::ScopedProfile scope(profiling::Scope::PathfindingFarTarget, true);
    constexpr float kMinRandomTargetDistanceUnits = 24.0F;
    constexpr float kMinRandomTargetDistanceSq = kMinRandomTargetDistanceUnits * kMinRandomTargetDistanceUnits;
    constexpr int kMaxTargetAttempts = 24;
    for (int attempt = 0; attempt < kMaxTargetAttempts; ++attempt) {
        const Vec2f randomTarget = RandomMazePoint(state.world, cellCache, random);
        if (DistanceSq(randomTarget, enemy.position) < kMinRandomTargetDistanceSq) {
            continue;
        }
        if (BuildAssassinPathToTarget(state, cellCache, enemy, enemyIndex, randomTarget)) {
            return true;
        }
    }

    const Vec2f fallbackTarget = RandomMazePoint(state.world, cellCache, random);
    return BuildAssassinPathToTarget(state, cellCache, enemy, enemyIndex, fallbackTarget);
}
