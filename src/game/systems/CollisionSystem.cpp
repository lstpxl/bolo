#include "game/systems/CollisionSystem.h"

void UpdateCollisionSystem(GameState& state, float deltaSeconds) {
    (void)deltaSeconds;
    if (!state.world.player.alive) {
        return;
    }

    // Placeholder fail-fast bounds until maze collision system is implemented.
    const bool outOfBounds = state.world.player.position.x < -500.0F ||
        state.world.player.position.x > 500.0F ||
        state.world.player.position.y < -500.0F ||
        state.world.player.position.y > 500.0F;

    if (outOfBounds) {
        state.world.player.alive = false;
    }
}
