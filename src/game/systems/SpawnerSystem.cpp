#include "game/systems/SpawnerSystem.h"

void UpdateSpawnerSystem(GameState& state, float deltaSeconds) {
    (void)deltaSeconds;
    if (state.world.enemyBase.activeEnemies < 1) {
        state.world.enemyBase.activeEnemies = 1;
    }
}
