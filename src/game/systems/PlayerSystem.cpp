#include "game/systems/PlayerSystem.h"

#include <algorithm>
#include <cmath>
#include "game/systems/ProjectileSystem.h"

void UpdatePlayerSystem(GameState& state, const FrameInput& input, float deltaSeconds) {
    constexpr float throttleRatePerSecond = 1.0F / GameplayConstants::kPlayerSecondsToFullVelocity;

    if (input.forwardButtonDown && !input.reverseButtonDown) {
        state.world.player.throttleNormalized += throttleRatePerSecond * deltaSeconds;
    } else if (input.reverseButtonDown && !input.forwardButtonDown) {
        state.world.player.throttleNormalized -= throttleRatePerSecond * deltaSeconds;
    }
    state.world.player.throttleNormalized =
        std::clamp(state.world.player.throttleNormalized, 0.0F, 1.0F);

    state.world.player.hullHeadingRadians += input.turnInput * GameplayConstants::kPlayerTurnSpeedRadians * deltaSeconds;
    state.world.player.turretHeadingRadians +=
        input.turretTurnInput * GameplayConstants::kPlayerTurretTurnSpeedRadians * deltaSeconds;
    const float forwardSpeed = state.world.player.throttleNormalized * GameplayConstants::kPlayerFullVelocity;
    state.world.player.velocity.x = std::sin(state.world.player.hullHeadingRadians) * forwardSpeed;
    state.world.player.velocity.y = -std::cos(state.world.player.hullHeadingRadians) * forwardSpeed;

    state.world.player.fireCooldownSeconds = std::max(0.0F, state.world.player.fireCooldownSeconds - deltaSeconds);
    if (input.shootPressed && state.world.player.fireCooldownSeconds <= 0.0F) {
        const float projectileHeading = state.world.player.hullHeadingRadians;
        SpawnProjectile(
            state,
            ProjectileOwner::Player,
            state.world.player.position,
            projectileHeading,
            GameplayConstants::kPlayerProjectileSpeed);
        state.world.player.fireCooldownSeconds = GameplayConstants::kPlayerFireCooldownSeconds;
    }

    state.world.player.position.x += state.world.player.velocity.x * deltaSeconds;
    state.world.player.position.y += state.world.player.velocity.y * deltaSeconds;
}
