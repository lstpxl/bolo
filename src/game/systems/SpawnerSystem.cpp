#include "game/systems/SpawnerSystem.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include "core/Random.h"
#include "raylib.h"

namespace {
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
}  // namespace

void UpdateSpawnerSystem(GameState& state, float deltaSeconds) {
    static Random random(static_cast<std::uint32_t>(GetTime() * 1000.0));
    int aliveEnemies = AliveEnemyCount(state);
    for (EnemyBase& base : state.world.enemyBases) {
        if (base.destroyed) {
            base.activeEnemies = 0;
            continue;
        }
        base.spawnCooldownSeconds -= deltaSeconds;
        if (aliveEnemies >= GameplayConstants::kMaxAliveEnemies || base.spawnCooldownSeconds > 0.0F) {
            continue;
        }

        const EnemySpawnEntry spawnedEnemy = PickSpawnEnemyForLevel(state.menuSettings.levelNumber, random);
        state.world.enemies.push_back(EnemyTank{
            .position = base.position,
            .velocity = Vec2f{.x = 0.0F, .y = 0.0F},
            .headingRadians = 0.0F,
            .type = spawnedEnemy.type,
            .subtype = spawnedEnemy.subtype,
            .fireCooldownSeconds = GameplayConstants::kEnemyInitialFireCooldownSeconds,
            .aiStateTimerSeconds = 0.0F,
            .wanderDirection = Vec2f{.x = 0.0F, .y = -1.0F},
            .alive = true,
        });
        base.activeEnemies += 1;
        base.spawnCooldownSeconds = GameplayConstants::kBaseSpawnCooldownSeconds;
        ++aliveEnemies;
    }
}
