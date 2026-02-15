#include "game/systems/PlayerSystem.h"

void UpdatePlayerSystem(GameState& state, const FrameInput& input, float deltaSeconds) {
    constexpr float moveSpeed = 120.0F;
    state.world.player.velocity.x = input.moveX * moveSpeed;
    state.world.player.velocity.y = input.moveY * moveSpeed;

    state.world.player.position.x += state.world.player.velocity.x * deltaSeconds;
    state.world.player.position.y += state.world.player.velocity.y * deltaSeconds;
}
