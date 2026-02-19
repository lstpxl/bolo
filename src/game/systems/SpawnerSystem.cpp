#include "game/systems/SpawnerSystem.h"

namespace {
int AliveEnemyCount(const GameState& state) {
    int count = 0;
    for (const EnemyTank& enemy : state.world.enemies) {
        if (enemy.alive) {
            ++count;
        }
    }
    return count;
}
}  // namespace

void UpdateSpawnerSystem(GameState& state, float deltaSeconds) {
    constexpr int kMaxAliveEnemies = 18;
    int aliveEnemies = AliveEnemyCount(state);
    for (EnemyBase& base : state.world.enemyBases) {
        if (base.destroyed) {
            base.activeEnemies = 0;
            continue;
        }
        base.spawnCooldownSeconds -= deltaSeconds;
        if (aliveEnemies >= kMaxAliveEnemies || base.spawnCooldownSeconds > 0.0F) {
            continue;
        }

        state.world.enemies.push_back(EnemyTank{
            .position = base.position,
            .velocity = Vec2f{.x = 0.0F, .y = 0.0F},
            .headingRadians = 0.0F,
            .type = EnemyType::Drone,
            .fireCooldownSeconds = 0.2F,
            .aiStateTimerSeconds = 0.0F,
            .wanderDirection = Vec2f{.x = 0.0F, .y = -1.0F},
            .alive = true,
        });
        base.activeEnemies += 1;
        base.spawnCooldownSeconds = 1.2F;
        ++aliveEnemies;
    }
}
