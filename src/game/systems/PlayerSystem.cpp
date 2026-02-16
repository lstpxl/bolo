#include "game/systems/PlayerSystem.h"

#include <algorithm>
#include <cmath>

void UpdatePlayerSystem(GameState& state, const FrameInput& input, float deltaSeconds) {
    constexpr float fullVelocity = 20.0F;
    constexpr float secondsToFullVelocity = 3.0F;
    constexpr float throttleRatePerSecond = 1.0F / secondsToFullVelocity;
    constexpr float turnSpeedRadians = 2.5F;

    if (input.forwardButtonDown && !input.reverseButtonDown) {
        state.world.player.throttleNormalized += throttleRatePerSecond * deltaSeconds;
    } else if (input.reverseButtonDown && !input.forwardButtonDown) {
        state.world.player.throttleNormalized -= throttleRatePerSecond * deltaSeconds;
    }
    state.world.player.throttleNormalized =
        std::clamp(state.world.player.throttleNormalized, 0.0F, 1.0F);

    state.world.player.hullHeadingRadians += input.turnInput * turnSpeedRadians * deltaSeconds;
    state.world.player.turretHeadingRadians = state.world.player.hullHeadingRadians;
    const float forwardSpeed = state.world.player.throttleNormalized * fullVelocity;
    state.world.player.velocity.x = std::sin(state.world.player.hullHeadingRadians) * forwardSpeed;
    state.world.player.velocity.y = -std::cos(state.world.player.hullHeadingRadians) * forwardSpeed;

    state.world.player.position.x += state.world.player.velocity.x * deltaSeconds;
    state.world.player.position.y += state.world.player.velocity.y * deltaSeconds;
}
