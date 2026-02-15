#include "game/systems/PlayerSystem.h"

#include <cmath>

void UpdatePlayerSystem(GameState& state, const FrameInput& input, float deltaSeconds) {
    constexpr float moveSpeed = 120.0F;
    constexpr float turnSpeedRadians = 2.5F;
    state.world.player.headingRadians += input.turnInput * turnSpeedRadians * deltaSeconds;
    const float forwardSpeed = -input.moveY * moveSpeed;
    state.world.player.velocity.x = std::sin(state.world.player.headingRadians) * forwardSpeed;
    state.world.player.velocity.y = -std::cos(state.world.player.headingRadians) * forwardSpeed;

    state.world.player.position.x += state.world.player.velocity.x * deltaSeconds;
    state.world.player.position.y += state.world.player.velocity.y * deltaSeconds;
}
