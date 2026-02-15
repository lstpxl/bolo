#include "game/systems/MazeSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stack>
#include <vector>
#include "core/Random.h"
#include "raylib.h"

namespace {
struct CellCoord {
    int x;
    int y;
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

    // Lower density means fewer walls: carve extra openings after building connected backbone.
    const int clampedDensity = std::max(1, std::min(5, density));
    const int extraOpenings = (5 - clampedDensity) * (totalCells / 8);
    for (int i = 0; i < extraOpenings; ++i) {
        const int cellX = random.NextInt(0, maze.widthCells - 1);
        const int cellY = random.NextInt(0, maze.heightCells - 1);
        const CellCoord origin{.x = cellX, .y = cellY};
        const CellCoord offset = offsets[static_cast<std::size_t>(random.NextInt(0, 3))];
        const int neighborX = origin.x + offset.x;
        const int neighborY = origin.y + offset.y;
        if (!IsInBounds(maze, neighborX, neighborY)) {
            continue;
        }
        RemoveWallBetween(maze, origin, CellCoord{.x = neighborX, .y = neighborY});
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
}  // namespace

void InitializeMazeWorld(GameState& state, const AppConfig& config) {
    const std::uint32_t seed = static_cast<std::uint32_t>(GetTime() * 1000.0);
    Random random(seed);

    state.world.maze.widthCells = GameplayConstants::kMazeWidthCells;
    state.world.maze.heightCells = GameplayConstants::kMazeHeightCells;
    state.world.maze.cellSizeUnits = GameplayConstants::kMazeCellSizeUnits;

    do {
        GenerateConnectedMaze(state.world.maze, random, state.menuSettings.mazeDensity);
    } while (!IsMazeFullyAccessible(state.world.maze) || !IsMazeWallTopologyValid(state.world.maze));

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
            state.world.enemyBases.push_back(EnemyBase{
                .position = CellCenterPosition(state.world.maze, cellX, cellY),
                .destroyed = false,
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
            state.world.enemyBases.push_back(EnemyBase{
                .position = CellCenterPosition(state.world.maze, x, y),
                .destroyed = false,
                .activeEnemies = 0,
            });
        }
    }

    const float visibleWidthUnits = static_cast<float>(config.screenWidth - ComputeHudWidth(config)) /
        static_cast<float>(GameplayConstants::kPixelsPerUnit);
    const float visibleHeightUnits = static_cast<float>(config.screenHeight) /
        static_cast<float>(GameplayConstants::kPixelsPerUnit);

    bool playerPlaced = false;
    for (int attempts = 0; attempts < 5000 && !playerPlaced; ++attempts) {
        const int cellX = random.NextInt(0, state.world.maze.widthCells - 1);
        const int cellY = random.NextInt(0, state.world.maze.heightCells - 1);
        const Vec2f candidate = CellCenterPosition(state.world.maze, cellX, cellY);

        bool overlapsBase = false;
        for (const EnemyBase& base : state.world.enemyBases) {
            const float dx = std::fabs(base.position.x - candidate.x);
            const float dy = std::fabs(base.position.y - candidate.y);
            const float minSeparation = (GameplayConstants::kEnemyBaseSizeUnits * 0.5F) +
                (GameplayConstants::kEntitySizeUnits * 0.5F);
            if (dx < minSeparation && dy < minSeparation) {
                overlapsBase = true;
                break;
            }
        }
        if (overlapsBase) {
            continue;
        }

        bool seesAnyBase = false;
        for (const EnemyBase& base : state.world.enemyBases) {
            if (IsBaseVisibleFromPosition(base, candidate, visibleWidthUnits, visibleHeightUnits)) {
                seesAnyBase = true;
                break;
            }
        }
        if (seesAnyBase) {
            continue;
        }

        state.world.player.position = candidate;
        state.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        state.world.player.hullHeadingRadians = 0.0F;
        state.world.player.turretHeadingRadians = 0.0F;
        state.world.player.alive = true;
        playerPlaced = true;
    }

    if (!playerPlaced) {
        state.world.player.position = CellCenterPosition(state.world.maze, 0, 0);
        state.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        state.world.player.hullHeadingRadians = 0.0F;
        state.world.player.turretHeadingRadians = 0.0F;
        state.world.player.alive = true;
    }

    state.world.enemies.clear();
    state.world.score = 0;
}

void UpdateMazeSystem(GameState& state, float deltaSeconds) {
    (void)state;
    (void)deltaSeconds;
}
