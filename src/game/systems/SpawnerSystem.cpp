#include "game/systems/SpawnerSystem.h"

#include <array>
#include <cmath>
#include <vector>
#include "core/AngleMath.h"
#include "core/Random.h"
#include "game/EnemyAppearance.h"
#include "game/GameQueries.h"
#include "game/geometry/WorldGeometry.h"

namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kDiagonalSpawnCoreShiftUnits = 0.5F;
constexpr float kRequiredSpawnClearUnits = 6.0F;
constexpr float kSpawnProbeMaxUnits = 8.0F;

game::EnemySpawnChoice PickSpawnEnemyForLevel(int level, Random& random) {
    std::vector<game::EnemySpawnChoice> candidates = game::EnemyTypesForLevel(level);
    if (candidates.empty()) {
        return game::EnemySpawnChoice{.type = EnemyType::Drone, .subtype = EnemySubtype::Advanced};
    }
    const int pickedIndex = random.NextInt(0, static_cast<int>(candidates.size()) - 1);
    return candidates[static_cast<std::size_t>(pickedIndex)];
}

struct SpawnRayChoice {
    bool found = false;
    float heading = 0.0F;
    float clearDistance = 0.0F;
};

SpawnRayChoice PickSpawnDirection(const WorldState& world, const Vec2f& baseCenter, Random& random) {
    constexpr std::array<float, 8> kHeadings{
        0.0F, kPi * 0.25F, kPi * 0.5F, kPi * 0.75F, kPi, kPi * 1.25F, kPi * 1.5F, kPi * 1.75F};

    std::array<SpawnRayChoice, 8> choices{};
    std::array<int, 8> validIndices{};
    int validCount = 0;
    int bestIndex = 0;
    float bestClear = -1.0F;
    for (int i = 0; i < static_cast<int>(kHeadings.size()); ++i) {
        const float heading = kHeadings[static_cast<std::size_t>(i)];
        const float clearDistance = game::geometry::FreeDistanceAhead(
            world,
            baseCenter,
            heading,
            kSpawnProbeMaxUnits,
            GameplayConstants::kWallClearanceForAvoidance);
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
    if (game::geometry::IsPointInWall(state.world, spawnPosition, GameplayConstants::kWallClearanceForAvoidance)) {
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

bool IsDiagonalDirection(const Vec2f& dir) {
    return std::fabs(dir.x) > 0.5F && std::fabs(dir.y) > 0.5F;
}

void ResetSpawnTimerAfterFailedAttempt(EnemyBase& base) {
    base.enemyGenerationTimerSeconds = base.enemyGenerationIntervalSeconds;
}
}  // namespace

void UpdateSpawnerSystem(GameState& state, float deltaSeconds, Random& random) {
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

    int aliveEnemies = game::queries::CountAliveEnemies(state);
    for (int baseIndex = 0; baseIndex < static_cast<int>(state.world.enemyBases.size()); ++baseIndex) {
        EnemyBase& base = state.world.enemyBases[static_cast<std::size_t>(baseIndex)];
        if (base.destroyed) {
            base.activeEnemies = 0;
            continue;
        }
        if (base.enemyGenerationIntervalSeconds <= 0.0F) {
            base.enemyGenerationIntervalSeconds = GameplayConstants::kBaseSpawnCooldownSeconds;
        }
        base.enemyGenerationTimerSeconds -= deltaSeconds;
        const int maxPerBase = (state.menuSettings.levelNumber == 9) ? 6 : GameplayConstants::kMaxAliveEnemiesPerBase;
        if (aliveEnemies >= GameplayConstants::kMaxAliveEnemies ||
            base.enemyGenerationTimerSeconds > 0.0F ||
            base.activeEnemies >= maxPerBase) {
            continue;
        }

        const game::EnemySpawnChoice spawnedEnemy = PickSpawnEnemyForLevel(state.menuSettings.levelNumber, random);
        const SpawnRayChoice spawnDirection = PickSpawnDirection(state.world, base.position, random);
        if (!spawnDirection.found) {
            // Failed attempt: wait a full interval before retrying.
            ResetSpawnTimerAfterFailedAttempt(base);
            continue;
        }
        const Vec2f dir = core::angle::DirectionFromHeading(spawnDirection.heading);
        const float baseHalfSizeUnits = GameplayConstants::kEnemyBaseSizeUnits * 0.5F;
        const bool diagonalSpawn = IsDiagonalDirection(dir);
        Vec2f noseAnchor = Vec2f{
            .x = base.position.x + dir.x * baseHalfSizeUnits,
            .y = base.position.y + dir.y * baseHalfSizeUnits,
        };
        if (diagonalSpawn) {
            noseAnchor = Vec2f{
                .x = base.position.x + (dir.x >= 0.0F ? baseHalfSizeUnits : -baseHalfSizeUnits),
                .y = base.position.y + (dir.y >= 0.0F ? baseHalfSizeUnits : -baseHalfSizeUnits),
            };
        }
        const float spawnBackoffUnits =
            GameplayConstants::kEntitySizeUnits * 0.5F +
            (diagonalSpawn ? kDiagonalSpawnCoreShiftUnits : 0.0F);
        Vec2f spawnPosition{
            .x = noseAnchor.x - dir.x * spawnBackoffUnits,
            .y = noseAnchor.y - dir.y * spawnBackoffUnits,
        };
        if (!IsSpawnPositionFree(state, spawnPosition)) {
            ResetSpawnTimerAfterFailedAttempt(base);
            continue;
        }
        const float forwardClearWithEnemies = game::geometry::FreeDistanceAheadWithEnemies(
            state.world,
            state.world.enemies,
            -1,
            spawnPosition,
            spawnDirection.heading,
            kRequiredSpawnClearUnits,
            GameplayConstants::kWallClearanceForAvoidance);
        if (forwardClearWithEnemies < kRequiredSpawnClearUnits) {
            // Failed attempt: wait a full interval before retrying.
            ResetSpawnTimerAfterFailedAttempt(base);
            continue;
        }

        EnemyAiMode mode = EnemyAiMode::Wander;
        if (spawnedEnemy.type == EnemyType::Hunter) {
            mode = EnemyAiMode::Scout;
        } else if (spawnedEnemy.type == EnemyType::Assassin) {
            mode = EnemyAiMode::Pursuit;
        }
        const float selfAwarenessInterval = (spawnedEnemy.type == EnemyType::Drone)
            ? random.NextFloat(6.0F, 12.0F)
            : random.NextFloat(4.0F, 8.0F);
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
        base.enemyGenerationTimerSeconds = base.enemyGenerationIntervalSeconds;
        ++aliveEnemies;
    }
}
