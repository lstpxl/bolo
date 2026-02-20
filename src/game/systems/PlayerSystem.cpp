#include "game/systems/PlayerSystem.h"

#include <algorithm>
#include <cmath>
#include "game/systems/ProjectileSystem.h"

namespace {
float NormalizeAngle(float angleRadians) {
    constexpr float kPi = 3.14159265358979323846F;
    const float twoPi = kPi * 2.0F;
    float normalized = std::fmod(angleRadians, twoPi);
    if (normalized < 0.0F) {
        normalized += twoPi;
    }
    return normalized;
}

}  // namespace

void UpdatePlayerSystem(GameState& state, const FrameInput& input, float deltaSeconds) {
    constexpr float throttleRatePerSecond = 1.0F / GameplayConstants::kPlayerSecondsToFullVelocity;
    constexpr float joystickAxisRawMax = 32768.0F;
    constexpr float fireJoystickDeadzoneNormalized = 0.2F;
    constexpr float throttleAccelerationPerSecond =
        GameplayConstants::kPlayerFullVelocity / GameplayConstants::kPlayerSecondsToFullVelocity;

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

    float speed = std::sqrt(
        state.world.player.velocity.x * state.world.player.velocity.x +
        state.world.player.velocity.y * state.world.player.velocity.y);
    float heading = state.world.player.hullHeadingRadians;
    if (speed > 0.001F) {
        heading = std::atan2(state.world.player.velocity.x, -state.world.player.velocity.y);
    }

    speed += state.world.player.throttleNormalized * throttleAccelerationPerSecond * deltaSeconds;

    const float joystickRawX = static_cast<float>(input.gamepadAxis0Raw);
    const float joystickRawY = static_cast<float>(input.gamepadAxis1Raw);
    float targetNormalizedX = joystickRawX / joystickAxisRawMax;
    float targetNormalizedY = joystickRawY / joystickAxisRawMax;
    const float joystickMagnitude =
        std::sqrt(targetNormalizedX * targetNormalizedX + targetNormalizedY * targetNormalizedY);
    if (joystickMagnitude > 1.0F) {
        targetNormalizedX /= joystickMagnitude;
        targetNormalizedY /= joystickMagnitude;
    }

    // Preserve legacy UP/DOWN throttle effect by adding forward target component.
    targetNormalizedX += std::sin(state.world.player.hullHeadingRadians) * state.world.player.throttleNormalized;
    targetNormalizedY += -std::cos(state.world.player.hullHeadingRadians) * state.world.player.throttleNormalized;
    const float targetMagnitude =
        std::sqrt(targetNormalizedX * targetNormalizedX + targetNormalizedY * targetNormalizedY);
    if (targetMagnitude > 1.0F) {
        targetNormalizedX /= targetMagnitude;
        targetNormalizedY /= targetMagnitude;
    }

    float velocityNormalizedX = state.world.player.velocity.x / GameplayConstants::kPlayerFullVelocity;
    float velocityNormalizedY = state.world.player.velocity.y / GameplayConstants::kPlayerFullVelocity;
    const float transformX = targetNormalizedX - velocityNormalizedX;
    const float transformY = targetNormalizedY - velocityNormalizedY;
    const float transformMagnitude = std::sqrt(transformX * transformX + transformY * transformY);
    const float transformStep = GameplayConstants::kJoystickAcceleration * deltaSeconds;
    if (transformMagnitude > 0.0F) {
        const float appliedStep = std::min(transformStep, transformMagnitude);
        velocityNormalizedX += (transformX / transformMagnitude) * appliedStep;
        velocityNormalizedY += (transformY / transformMagnitude) * appliedStep;
    }
    state.world.player.velocity.x = velocityNormalizedX * GameplayConstants::kPlayerFullVelocity;
    state.world.player.velocity.y = velocityNormalizedY * GameplayConstants::kPlayerFullVelocity;

    speed = std::sqrt(
        state.world.player.velocity.x * state.world.player.velocity.x +
        state.world.player.velocity.y * state.world.player.velocity.y);
    if (speed > 0.001F) {
        heading = std::atan2(state.world.player.velocity.x, -state.world.player.velocity.y);
    }
    state.world.player.hullHeadingRadians = heading;

    speed = std::max(0.0F, speed);
    heading = NormalizeAngle(heading);
    if (speed <= 0.001F) {
        state.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
    } else {
        state.world.player.velocity.x = std::sin(heading) * speed;
        state.world.player.velocity.y = -std::cos(heading) * speed;
    }
    state.world.player.hullHeadingRadians = NormalizeAngle(state.world.player.hullHeadingRadians);

    state.world.player.fireCooldownSeconds = std::max(0.0F, state.world.player.fireCooldownSeconds - deltaSeconds);
    const float fireJoystickRawX = static_cast<float>(input.gamepadAxis2Raw);
    const float fireJoystickRawY = static_cast<float>(input.gamepadAxis3Raw);
    const float fireJoystickAmplitude = std::sqrt(
        fireJoystickRawX * fireJoystickRawX + fireJoystickRawY * fireJoystickRawY) / joystickAxisRawMax;
    const bool fireJoystickInclined = fireJoystickAmplitude > fireJoystickDeadzoneNormalized;
    const bool fireRequested = input.shootPressed || fireJoystickInclined;
    if (fireRequested && state.world.player.fireCooldownSeconds <= 0.0F) {
        float projectileHeading = state.world.player.hullHeadingRadians;
        if (fireJoystickInclined) {
            projectileHeading = std::atan2(fireJoystickRawX, -fireJoystickRawY);
        }
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
