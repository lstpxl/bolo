#include "game/systems/MazeSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stack>
#include <vector>
#include "core/Random.h"

namespace {
constexpr float kRespawnMinDistanceFromBaseUnits = 30.0F;

float RandomBaseGenerationInterval(Random& random) {
    const float base = GameplayConstants::kBaseSpawnCooldownSeconds;
    return random.NextFloat(base * 0.5F, base * 1.5F);
}

struct CellCoord {
    int x;
    int y;
};

struct MazeEdge {
    CellCoord a;
    CellCoord b;
};

int ToIndex(const MazeState& maze, int x, int y) {
    return y * maze.widthCells + x;
}

bool IsInBounds(const MazeState& maze, int x, int y) {
    return x >= 0 && y >= 0 && x < maze.widthCells && y < maze.heightCells;
}

void RemoveWallBetween(MazeState& maze, const CellCoord& a, const CellCoord& b) {
    MazeCell& cellA = maze.cells[ToIndex(maze, a.x, a.y)];
    MazeCell& cellB = maze.cells[ToIndex(maze, b.x, b.y)];
    if (a.x == b.x) {
        if (a.y < b.y) {
            cellA.southWall = false;
            cellB.northWall = false;
        } else {
            cellA.northWall = false;
            cellB.southWall = false;
        }
        return;
    }

    if (a.x < b.x) {
        cellA.eastWall = false;
        cellB.westWall = false;
    } else {
        cellA.westWall = false;
        cellB.eastWall = false;
    }
}

std::vector<CellCoord> ReachableNeighbors(const MazeState& maze, const CellCoord& cell) {
    const MazeCell& source = maze.cells[ToIndex(maze, cell.x, cell.y)];
    std::vector<CellCoord> neighbors{};
    neighbors.reserve(4);
    if (!source.northWall && IsInBounds(maze, cell.x, cell.y - 1)) {
        neighbors.push_back(CellCoord{.x = cell.x, .y = cell.y - 1});
    }
    if (!source.eastWall && IsInBounds(maze, cell.x + 1, cell.y)) {
        neighbors.push_back(CellCoord{.x = cell.x + 1, .y = cell.y});
    }
    if (!source.southWall && IsInBounds(maze, cell.x, cell.y + 1)) {
        neighbors.push_back(CellCoord{.x = cell.x, .y = cell.y + 1});
    }
    if (!source.westWall && IsInBounds(maze, cell.x - 1, cell.y)) {
        neighbors.push_back(CellCoord{.x = cell.x - 1, .y = cell.y});
    }
    return neighbors;
}

bool IsMazeFullyAccessible(const MazeState& maze) {
    if (maze.cells.empty()) {
        return false;
    }

    std::vector<bool> visited(static_cast<std::size_t>(maze.widthCells * maze.heightCells), false);
    std::stack<CellCoord> toVisit{};
    toVisit.push(CellCoord{.x = 0, .y = 0});
    visited[0] = true;
    int visitedCount = 1;
    while (!toVisit.empty()) {
        const CellCoord current = toVisit.top();
        toVisit.pop();
        const std::vector<CellCoord> neighbors = ReachableNeighbors(maze, current);
        for (const CellCoord& neighbor : neighbors) {
            const int index = ToIndex(maze, neighbor.x, neighbor.y);
            if (visited[static_cast<std::size_t>(index)]) {
                continue;
            }
            visited[static_cast<std::size_t>(index)] = true;
            ++visitedCount;
            toVisit.push(neighbor);
        }
    }
    return visitedCount == maze.widthCells * maze.heightCells;
}

bool IsMazeWallTopologyValid(const MazeState& maze) {
    for (int y = 0; y < maze.heightCells; ++y) {
        for (int x = 0; x < maze.widthCells; ++x) {
            const MazeCell& current = maze.cells[ToIndex(maze, x, y)];
            if (x + 1 < maze.widthCells) {
                const MazeCell& east = maze.cells[ToIndex(maze, x + 1, y)];
                if (current.eastWall != east.westWall) {
                    return false;
                }
            }
            if (y + 1 < maze.heightCells) {
                const MazeCell& south = maze.cells[ToIndex(maze, x, y + 1)];
                if (current.southWall != south.northWall) {
                    return false;
                }
            }
        }
    }
    return true;
}

void GenerateConnectedMaze(MazeState& maze, Random& random, int density) {
    const int totalCells = maze.widthCells * maze.heightCells;
    maze.cells.assign(static_cast<std::size_t>(totalCells), MazeCell{});

    std::vector<bool> visited(static_cast<std::size_t>(totalCells), false);
    std::stack<CellCoord> stack{};
    const CellCoord start{
        .x = random.NextInt(0, maze.widthCells - 1),
        .y = random.NextInt(0, maze.heightCells - 1),
    };
    stack.push(start);
    visited[static_cast<std::size_t>(ToIndex(maze, start.x, start.y))] = true;

    constexpr std::array<CellCoord, 4> offsets{{
        {.x = 0, .y = -1},
        {.x = 1, .y = 0},
        {.x = 0, .y = 1},
        {.x = -1, .y = 0},
    }};

    while (!stack.empty()) {
        const CellCoord current = stack.top();
        std::vector<CellCoord> unvisitedNeighbors{};
        unvisitedNeighbors.reserve(4);
        for (const CellCoord& offset : offsets) {
            const int nextX = current.x + offset.x;
            const int nextY = current.y + offset.y;
            if (!IsInBounds(maze, nextX, nextY)) {
                continue;
            }
            const int nextIndex = ToIndex(maze, nextX, nextY);
            if (!visited[static_cast<std::size_t>(nextIndex)]) {
                unvisitedNeighbors.push_back(CellCoord{.x = nextX, .y = nextY});
            }
        }

        if (unvisitedNeighbors.empty()) {
            stack.pop();
            continue;
        }

        const int picked = random.NextInt(0, static_cast<int>(unvisitedNeighbors.size()) - 1);
        const CellCoord next = unvisitedNeighbors[static_cast<std::size_t>(picked)];
        RemoveWallBetween(maze, current, next);
        visited[static_cast<std::size_t>(ToIndex(maze, next.x, next.y))] = true;
        stack.push(next);
    }

    // Build a concrete, measurable density target:
    // internal wall segments per 100 cells.
    // density=1 and density=5 are defined in GameplayConstants.
    const int clampedDensity = std::max(1, std::min(5, density));
    const int horizontalConnections = maze.widthCells * (maze.heightCells - 1);
    const int verticalConnections = (maze.widthCells - 1) * maze.heightCells;
    const int totalInternalEdges = horizontalConnections + verticalConnections;
    const float densityT = static_cast<float>(clampedDensity - 1) / 4.0F;
    const float targetWallSegmentsPer100Cells =
        GameplayConstants::kDensity1WallsPer100Cells +
        (GameplayConstants::kDensity5WallsPer100Cells - GameplayConstants::kDensity1WallsPer100Cells) * densityT;
    int targetInternalWalls = static_cast<int>(
        std::round(targetWallSegmentsPer100Cells * static_cast<float>(totalCells) / 100.0F));
    targetInternalWalls = std::max(0, std::min(totalInternalEdges, targetInternalWalls));

    // Connected graph requires at least (V - 1) open edges.
    int targetOpenEdges = totalInternalEdges - targetInternalWalls;
    targetOpenEdges = std::max(targetOpenEdges, totalCells - 1);

    // DFS opened exactly (totalCells - 1) edges already.
    int extraOpeningsNeeded = targetOpenEdges - (totalCells - 1);
    if (extraOpeningsNeeded <= 0) {
        return;
    }

    std::vector<MazeEdge> internalEdges{};
    internalEdges.reserve(static_cast<std::size_t>(totalInternalEdges));
    for (int y = 0; y < maze.heightCells; ++y) {
        for (int x = 0; x < maze.widthCells; ++x) {
            if (x + 1 < maze.widthCells) {
                internalEdges.push_back(MazeEdge{
                    .a = CellCoord{.x = x, .y = y},
                    .b = CellCoord{.x = x + 1, .y = y},
                });
            }
            if (y + 1 < maze.heightCells) {
                internalEdges.push_back(MazeEdge{
                    .a = CellCoord{.x = x, .y = y},
                    .b = CellCoord{.x = x, .y = y + 1},
                });
            }
        }
    }

    for (std::size_t i = 0; i < internalEdges.size() && extraOpeningsNeeded > 0; ++i) {
        const int swapIndex = random.NextInt(static_cast<int>(i), static_cast<int>(internalEdges.size() - 1));
        std::swap(internalEdges[i], internalEdges[static_cast<std::size_t>(swapIndex)]);
        const MazeEdge& edge = internalEdges[i];
        const MazeCell& a = maze.cells[ToIndex(maze, edge.a.x, edge.a.y)];
        const MazeCell& b = maze.cells[ToIndex(maze, edge.b.x, edge.b.y)];
        const bool alreadyOpen = edge.a.x == edge.b.x ? (!a.southWall && !b.northWall) : (!a.eastWall && !b.westWall);
        if (alreadyOpen) {
            continue;
        }
        RemoveWallBetween(maze, edge.a, edge.b);
        --extraOpeningsNeeded;
    }
}

float CellCenterUnit(int cellIndex, int cellSizeUnits) {
    return static_cast<float>(cellIndex * cellSizeUnits) + static_cast<float>(cellSizeUnits) * 0.5F;
}

Vec2f CellCenterPosition(const MazeState& maze, int cellX, int cellY) {
    return Vec2f{
        .x = CellCenterUnit(cellX, maze.cellSizeUnits),
        .y = CellCenterUnit(cellY, maze.cellSizeUnits),
    };
}

bool IsBaseVisibleFromPosition(
    const EnemyBase& base,
    const Vec2f& candidatePosition,
    float visibleWidthUnits,
    float visibleHeightUnits) {
    const float halfWidth = visibleWidthUnits * 0.5F;
    const float halfHeight = visibleHeightUnits * 0.5F;
    const float viewLeft = candidatePosition.x - halfWidth;
    const float viewRight = candidatePosition.x + halfWidth;
    const float viewTop = candidatePosition.y - halfHeight;
    const float viewBottom = candidatePosition.y + halfHeight;

    const float baseHalfSize = GameplayConstants::kEnemyBaseSizeUnits * 0.5F;
    const float baseLeft = base.position.x - baseHalfSize;
    const float baseRight = base.position.x + baseHalfSize;
    const float baseTop = base.position.y - baseHalfSize;
    const float baseBottom = base.position.y + baseHalfSize;

    return baseLeft < viewRight && baseRight > viewLeft && baseTop < viewBottom && baseBottom > viewTop;
}

bool TryPlacePlayer(
    GameState& state,
    const GameplayView& view,
    Random& random,
    bool disallowBaseInView,
    float minBaseDistanceUnits) {
    for (int attempts = 0; attempts < 5000; ++attempts) {
        const int cellX = random.NextInt(0, state.world.maze.widthCells - 1);
        const int cellY = random.NextInt(0, state.world.maze.heightCells - 1);
        const Vec2f candidate = CellCenterPosition(state.world.maze, cellX, cellY);

        bool overlapsBase = false;
        float nearestBaseDistanceSq = std::numeric_limits<float>::infinity();
        for (const EnemyBase& base : state.world.enemyBases) {
            if (base.destroyed) {
                continue;
            }
            const float dx = std::fabs(base.position.x - candidate.x);
            const float dy = std::fabs(base.position.y - candidate.y);
            const float minSeparation = (GameplayConstants::kEnemyBaseSizeUnits * 0.5F) +
                (GameplayConstants::kEntitySizeUnits * 0.5F);
            if (dx < minSeparation && dy < minSeparation) {
                overlapsBase = true;
                break;
            }
            const float distSq = dx * dx + dy * dy;
            nearestBaseDistanceSq = std::min(nearestBaseDistanceSq, distSq);
        }
        if (overlapsBase) {
            continue;
        }
        if (minBaseDistanceUnits > 0.0F &&
            nearestBaseDistanceSq < (minBaseDistanceUnits * minBaseDistanceUnits)) {
            continue;
        }

        if (disallowBaseInView) {
            bool seesAnyBase = false;
            for (const EnemyBase& base : state.world.enemyBases) {
                if (base.destroyed) {
                    continue;
                }
                if (IsBaseVisibleFromPosition(base, candidate, view.viewportWidthUnits, view.viewportHeightUnits)) {
                    seesAnyBase = true;
                    break;
                }
            }
            if (seesAnyBase) {
                continue;
            }
        }

        state.world.player.position = candidate;
        state.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        state.world.player.hullHeadingRadians = 0.0F;
        state.world.player.turretHeadingRadians = 0.0F;
        state.world.player.turnHoldDirection = 0;
        state.world.player.turnHoldElapsedSeconds = 0.0F;
        state.world.player.throttleNormalized = 0.0F;
        state.world.player.fireCooldownSeconds = 0.0F;
        state.world.player.alive = true;
        return true;
    }
    return false;
}
}  // namespace

void InitializeMazeWorld(GameState& state, const GameplayView& view, Random& random) {
    state.world.navigationCache = NavigationRuntimeCache{};
    state.world.maze.widthCells = GameplayConstants::kMazeWidthCells;
    state.world.maze.heightCells = GameplayConstants::kMazeHeightCells;
    state.world.maze.cellSizeUnits = GameplayConstants::kMazeCellSizeUnits;

    do {
        GenerateConnectedMaze(state.world.maze, random, state.menuSettings.mazeDensity);
    } while (
        !IsMazeFullyAccessible(state.world.maze) ||
        !IsMazeWallTopologyValid(state.world.maze));

    state.world.enemyBases.clear();
    state.world.enemyBases.reserve(GameplayConstants::kEnemyBaseCount);
    std::vector<bool> usedCells(
        static_cast<std::size_t>(state.world.maze.widthCells * state.world.maze.heightCells),
        false);

    for (int i = 0; i < GameplayConstants::kEnemyBaseCount; ++i) {
        bool placed = false;
        for (int attempts = 0; attempts < 2000 && !placed; ++attempts) {
            const int cellX = random.NextInt(0, state.world.maze.widthCells - 1);
            const int cellY = random.NextInt(0, state.world.maze.heightCells - 1);
            const int cellIndex = ToIndex(state.world.maze, cellX, cellY);
            if (usedCells[static_cast<std::size_t>(cellIndex)]) {
                continue;
            }
            usedCells[static_cast<std::size_t>(cellIndex)] = true;
            const float generationInterval = RandomBaseGenerationInterval(random);
            state.world.enemyBases.push_back(EnemyBase{
                .position = CellCenterPosition(state.world.maze, cellX, cellY),
                .destroyed = false,
                .enemyGenerationIntervalSeconds = generationInterval,
                .enemyGenerationTimerSeconds = generationInterval,
                .activeEnemies = 0,
            });
            placed = true;
        }
    }
    for (int y = 0; y < state.world.maze.heightCells &&
            static_cast<int>(state.world.enemyBases.size()) < GameplayConstants::kEnemyBaseCount;
         ++y) {
        for (int x = 0; x < state.world.maze.widthCells &&
                static_cast<int>(state.world.enemyBases.size()) < GameplayConstants::kEnemyBaseCount;
             ++x) {
            const int cellIndex = ToIndex(state.world.maze, x, y);
            if (usedCells[static_cast<std::size_t>(cellIndex)]) {
                continue;
            }
            usedCells[static_cast<std::size_t>(cellIndex)] = true;
            const float generationInterval = RandomBaseGenerationInterval(random);
            state.world.enemyBases.push_back(EnemyBase{
                .position = CellCenterPosition(state.world.maze, x, y),
                .destroyed = false,
                .enemyGenerationIntervalSeconds = generationInterval,
                .enemyGenerationTimerSeconds = generationInterval,
                .activeEnemies = 0,
            });
        }
    }

    if (!TryPlacePlayer(state, view, random, true, 0.0F)) {
        state.world.player.position = CellCenterPosition(state.world.maze, 0, 0);
        state.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        state.world.player.hullHeadingRadians = 0.0F;
        state.world.player.turretHeadingRadians = 0.0F;
        state.world.player.turnHoldDirection = 0;
        state.world.player.turnHoldElapsedSeconds = 0.0F;
        state.world.player.throttleNormalized = 0.0F;
        state.world.player.fireCooldownSeconds = 0.0F;
        state.world.player.alive = true;
    }

    state.world.player.fuel = GameplayConstants::kFuelMax;
    state.world.enemies.clear();
    state.world.projectiles.clear();
    state.world.playerTurnLostPending = false;
    state.world.levelCleared = false;
    state.world.levelClearMessageSeconds = 0.0F;
    state.world.panModeActive = false;
    state.world.panTarget = state.world.player.position;
}

bool PlacePlayerAtSafeSpawn(GameState& state, const GameplayView& view, Random& random) {
    if (TryPlacePlayer(state, view, random, false, kRespawnMinDistanceFromBaseUnits)) {
        return true;
    }
    // Deterministic fallback: choose the cell center farthest from any alive base.
    float bestDistanceSq = -1.0F;
    Vec2f bestPosition = CellCenterPosition(state.world.maze, 0, 0);
    for (int y = 0; y < state.world.maze.heightCells; ++y) {
        for (int x = 0; x < state.world.maze.widthCells; ++x) {
            const Vec2f candidate = CellCenterPosition(state.world.maze, x, y);
            float nearestBaseDistanceSq = std::numeric_limits<float>::infinity();
            for (const EnemyBase& base : state.world.enemyBases) {
                if (base.destroyed) {
                    continue;
                }
                const float dx = base.position.x - candidate.x;
                const float dy = base.position.y - candidate.y;
                const float distSq = dx * dx + dy * dy;
                nearestBaseDistanceSq = std::min(nearestBaseDistanceSq, distSq);
            }
            if (nearestBaseDistanceSq > bestDistanceSq) {
                bestDistanceSq = nearestBaseDistanceSq;
                bestPosition = candidate;
            }
        }
    }
    state.world.player.position = bestPosition;
    state.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
    state.world.player.hullHeadingRadians = 0.0F;
    state.world.player.turretHeadingRadians = 0.0F;
    state.world.player.turnHoldDirection = 0;
    state.world.player.turnHoldElapsedSeconds = 0.0F;
    state.world.player.throttleNormalized = 0.0F;
    state.world.player.fireCooldownSeconds = 0.0F;
    state.world.player.alive = true;
    return bestDistanceSq >= (kRespawnMinDistanceFromBaseUnits * kRespawnMinDistanceFromBaseUnits);
}

void UpdateMazeSystem(GameState& state, float deltaSeconds) {
    (void)state;
    (void)deltaSeconds;
}
