#include "game/systems/SpawnerSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "core/Random.h"
#include "raylib.h"

namespace {
constexpr float kPi = 3.14159265358979323846F;

struct EnemySpawnEntry {
    EnemyType type;
    EnemySubtype subtype;
};

constexpr std::array<EnemySpawnEntry, 9> kEnemySpawnTable{{
    {.type = EnemyType::Drone, .subtype = EnemySubtype::Advanced},    // level 1
    {.type = EnemyType::Drone, .subtype = EnemySubtype::Advanced},    // level 2
    {.type = EnemyType::Torpedo, .subtype = EnemySubtype::Advanced},  // level 3
    {.type = EnemyType::Torpedo, .subtype = EnemySubtype::Advanced},  // level 4
    {.type = EnemyType::Hunter, .subtype = EnemySubtype::Advanced},   // level 5
    {.type = EnemyType::Hunter, .subtype = EnemySubtype::Advanced},   // level 6
    {.type = EnemyType::Hunter, .subtype = EnemySubtype::Advanced},   // level 7
    {.type = EnemyType::Assassin, .subtype = EnemySubtype::Advanced}, // level 8
    {.type = EnemyType::Assassin, .subtype = EnemySubtype::Advanced}, // level 9
}};

int AliveEnemyCount(const GameState& state) {
    int count = 0;
    for (const EnemyTank& enemy : state.world.enemies) {
        if (enemy.alive) {
            ++count;
        }
    }
    return count;
}

EnemySpawnEntry PickSpawnEnemyForLevel(int level, Random& random) {
    const int clampedLevel = std::max(1, std::min(level, static_cast<int>(kEnemySpawnTable.size())));
    const int pickedIndex = random.NextInt(0, clampedLevel - 1);
    return kEnemySpawnTable[static_cast<std::size_t>(pickedIndex)];
}

Vec2f DirectionFromHeading(float headingRadians) {
    return Vec2f{
        .x = std::sin(headingRadians),
        .y = -std::cos(headingRadians),
    };
}

bool IsPointInsideMaze(const WorldState& world, const Vec2f& p, float clearanceUnits) {
    const float mazeWidthUnits = static_cast<float>(world.maze.widthCells * world.maze.cellSizeUnits);
    const float mazeHeightUnits = static_cast<float>(world.maze.heightCells * world.maze.cellSizeUnits);
    return p.x >= clearanceUnits && p.x <= mazeWidthUnits - clearanceUnits &&
        p.y >= clearanceUnits && p.y <= mazeHeightUnits - clearanceUnits;
}

bool IsPointInWall(const WorldState& world, const Vec2f& point, float clearanceUnits) {
    if (!IsPointInsideMaze(world, point, clearanceUnits)) {
        return true;
    }
    const float cellSize = static_cast<float>(world.maze.cellSizeUnits);
    const int cellX = static_cast<int>(point.x / cellSize);
    const int cellY = static_cast<int>(point.y / cellSize);
    if (cellX < 0 || cellY < 0 || cellX >= world.maze.widthCells || cellY >= world.maze.heightCells) {
        return true;
    }
    const MazeCell& cell =
        world.maze.cells[static_cast<std::size_t>(cellY * world.maze.widthCells + cellX)];
    const float localX = point.x - static_cast<float>(cellX) * cellSize;
    const float localY = point.y - static_cast<float>(cellY) * cellSize;
    const float wallLimit = GameplayConstants::kWallThicknessUnits + clearanceUnits;
    return (cell.northWall && localY <= wallLimit) || (cell.southWall && localY >= cellSize - wallLimit) ||
        (cell.westWall && localX <= wallLimit) || (cell.eastWall && localX >= cellSize - wallLimit);
}

float FreeDistanceAhead(
    const WorldState& world,
    const Vec2f& from,
    float headingRadians,
    float maxDistance,
    float clearanceUnits) {
    const Vec2f dir = DirectionFromHeading(headingRadians);
    constexpr float sampleSpacing = 0.08F;
    const int steps = std::max(1, static_cast<int>(std::ceil(maxDistance / sampleSpacing)));
    for (int i = 1; i <= steps; ++i) {
        const float dist = std::min(maxDistance, static_cast<float>(i) * sampleSpacing);
        const Vec2f sample{
            .x = from.x + dir.x * dist,
            .y = from.y + dir.y * dist,
        };
        if (IsPointInWall(world, sample, clearanceUnits)) {
            return dist;
        }
    }
    return maxDistance;
}

struct SpawnRayChoice {
    bool found = false;
    float heading = 0.0F;
    float clearDistance = 0.0F;
};

SpawnRayChoice PickSpawnDirection(const WorldState& world, const Vec2f& baseCenter, Random& random) {
    constexpr std::array<float, 8> kHeadings{
        0.0F, kPi * 0.25F, kPi * 0.5F, kPi * 0.75F, kPi, kPi * 1.25F, kPi * 1.5F, kPi * 1.75F};
    constexpr float kRequiredSpawnClearUnits = 6.0F;
    constexpr float kProbeMaxUnits = 8.0F;

    std::array<SpawnRayChoice, 8> choices{};
    std::array<int, 8> validIndices{};
    int validCount = 0;
    int bestIndex = 0;
    float bestClear = -1.0F;
    for (int i = 0; i < static_cast<int>(kHeadings.size()); ++i) {
        const float heading = kHeadings[static_cast<std::size_t>(i)];
        const float clearDistance = FreeDistanceAhead(
            world,
            baseCenter,
            heading,
            kProbeMaxUnits,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
        choices[static_cast<std::size_t>(i)] = SpawnRayChoice{
            .heading = heading,
            .clearDistance = clearDistance,
        };
        if (clearDistance > bestClear) {
            bestClear = clearDistance;
            bestIndex = i;
        }
        if (clearDistance >= kRequiredSpawnClearUnits) {
            validIndices[static_cast<std::size_t>(validCount)] = i;
            ++validCount;
        }
    }

    if (validCount > 0) {
        const int picked = validIndices[static_cast<std::size_t>(random.NextInt(0, validCount - 1))];
        SpawnRayChoice choice = choices[static_cast<std::size_t>(picked)];
        choice.found = true;
        return choice;
    }
    (void)bestIndex;
    (void)bestClear;
    return SpawnRayChoice{};
}

bool IsSpawnPositionFree(const GameState& state, const Vec2f& spawnPosition) {
    if (IsPointInWall(state.world, spawnPosition, GameplayConstants::kEnemyWallAvoidanceRadiusUnits)) {
        return false;
    }
    const float minDistanceSq =
        GameplayConstants::kEnemyPreferredSeparationUnits * GameplayConstants::kEnemyPreferredSeparationUnits;
    for (const EnemyTank& enemy : state.world.enemies) {
        if (!enemy.alive) {
            continue;
        }
        const float dx = spawnPosition.x - enemy.position.x;
        const float dy = spawnPosition.y - enemy.position.y;
        const float distanceSq = dx * dx + dy * dy;
        if (distanceSq < minDistanceSq) {
            return false;
        }
    }
    return true;
}
}  // namespace

void UpdateSpawnerSystem(GameState& state, float deltaSeconds) {
    static Random random(static_cast<std::uint32_t>(GetTime() * 1000.0));
    for (EnemyBase& base : state.world.enemyBases) {
        base.activeEnemies = 0;
    }
    for (const EnemyTank& enemy : state.world.enemies) {
        if (!enemy.alive) {
            continue;
        }
        if (enemy.originBaseIndex < 0 || enemy.originBaseIndex >= static_cast<int>(state.world.enemyBases.size())) {
            continue;
        }
        EnemyBase& origin = state.world.enemyBases[static_cast<std::size_t>(enemy.originBaseIndex)];
        if (!origin.destroyed) {
            origin.activeEnemies += 1;
        }
    }

    int aliveEnemies = AliveEnemyCount(state);
    for (int baseIndex = 0; baseIndex < static_cast<int>(state.world.enemyBases.size()); ++baseIndex) {
        EnemyBase& base = state.world.enemyBases[static_cast<std::size_t>(baseIndex)];
        if (base.destroyed) {
            base.activeEnemies = 0;
            continue;
        }
        base.spawnCooldownSeconds -= deltaSeconds;
        if (aliveEnemies >= GameplayConstants::kMaxAliveEnemies ||
            base.spawnCooldownSeconds > 0.0F ||
            base.activeEnemies >= GameplayConstants::kMaxAliveEnemiesPerBase) {
            continue;
        }

        const EnemySpawnEntry spawnedEnemy = PickSpawnEnemyForLevel(state.menuSettings.levelNumber, random);
        const SpawnRayChoice spawnDirection = PickSpawnDirection(state.world, base.position, random);
        if (!spawnDirection.found) {
            // Keep trying future ticks until the base has a clear 6-unit escape corridor.
            continue;
        }
        const Vec2f dir = DirectionFromHeading(spawnDirection.heading);
        const float spawnOffsetUnits =
            GameplayConstants::kEnemyBaseSizeUnits * 0.5F +
            GameplayConstants::kEntitySizeUnits * 0.5F +
            GameplayConstants::kEnemyWallClearanceUnits;
        Vec2f spawnPosition{
            .x = base.position.x + dir.x * spawnOffsetUnits,
            .y = base.position.y + dir.y * spawnOffsetUnits,
        };
        if (!IsSpawnPositionFree(state, spawnPosition)) {
            continue;
        }

        EnemyAiMode mode = EnemyAiMode::Wander;
        if (spawnedEnemy.type == EnemyType::Hunter) {
            mode = EnemyAiMode::Scout;
        } else if (spawnedEnemy.type == EnemyType::Assassin) {
            mode = EnemyAiMode::Path;
        }
        const float selfAwarenessInterval = random.NextFloat(4.0F, 8.0F);
        state.world.enemies.push_back(EnemyTank{
            .position = spawnPosition,
            .velocity = Vec2f{.x = 0.0F, .y = 0.0F},
            .headingRadians = spawnDirection.heading,
            .type = spawnedEnemy.type,
            .subtype = spawnedEnemy.subtype,
            .aiMode = mode,
            .fireCooldownSeconds = GameplayConstants::kEnemyInitialFireCooldownSeconds,
            .aiStateTimerSeconds = 0.0F,
            .aiModeElapsedSeconds = 0.0F,
            .selfAwarenessIntervalSeconds = selfAwarenessInterval,
            .selfAwarenessTimerSeconds = selfAwarenessInterval,
            .desiredHeadingRadians = 0.0F,
            .wanderDirection = Vec2f{.x = 0.0F, .y = -1.0F},
            .originBaseIndex = baseIndex,
            .pathWaypoints = {},
            .pathWaypointCount = 0,
            .pathWaypointIndex = 0,
            .alive = true,
        });
        base.activeEnemies += 1;
        base.spawnCooldownSeconds = GameplayConstants::kBaseSpawnCooldownSeconds;
        ++aliveEnemies;
    }
}
