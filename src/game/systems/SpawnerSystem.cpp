#include "game/systems/SpawnerSystem.h"

void UpdateSpawnerSystem(GameState& state, float deltaSeconds) {
    (void)deltaSeconds;
    if (!state.world.enemies.empty()) {
        return;
    }

    for (EnemyBase& base : state.world.enemyBases) {
        if (base.destroyed) {
            continue;
        }
        state.world.enemies.push_back(EnemyTank{
            .position = base.position,
            .headingRadians = 0.0F,
            .alive = true,
        });
        base.activeEnemies = 1;
    }
}
