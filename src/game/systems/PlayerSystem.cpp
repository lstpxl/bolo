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

float QuantizeToEightDirections(float angleRadians) {
    constexpr float kPi = 3.14159265358979323846F;
    constexpr float kStep = kPi / 4.0F;
    const float normalized = NormalizeAngle(angleRadians);
    const int stepIndex = static_cast<int>(std::round(normalized / kStep));
    return NormalizeAngle(static_cast<float>(stepIndex) * kStep);
}

Vec2f DirectionFromHeading(float headingRadians) {
    return Vec2f{
        .x = std::sin(headingRadians),
        .y = -std::cos(headingRadians),
    };
}

constexpr float kEightDirectionStepRadians = 3.14159265358979323846F / 4.0F;
constexpr float kTurnRepeatIntervalSeconds = 0.333F;

}  // namespace

void UpdatePlayerSystem(GameState& state, const FrameInput& input, float deltaSeconds) {
    // Throttle ramp 2× faster for UP/DOWN buttons.
    constexpr float throttleRatePerSecond =
        2.0F / GameplayConstants::kPlayerSecondsToFullVelocity;
    constexpr float joystickAxisRawMax = 32768.0F;
    constexpr float fireJoystickDeadzoneNormalized = 0.2F;
    constexpr float throttleAccelerationPerSecond =
        GameplayConstants::kPlayerFullVelocity / GameplayConstants::kPlayerSecondsToFullVelocity;

    if (!state.world.player.alive ||
        state.world.deathModeRemainingSeconds > 0.0F) {
        state.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        state.world.player.throttleNormalized = 0.0F;
        return;
    }

    const bool inStartMode = state.world.startModeRemainingSeconds > 0.0F;

    int requestedTurnDirection = 0;
    if (input.turnInput > 0.5F) {
        requestedTurnDirection = 1;
    } else if (input.turnInput < -0.5F) {
        requestedTurnDirection = -1;
    }
    if (requestedTurnDirection == 0) {
        state.world.player.turnHoldDirection = 0;
        state.world.player.turnHoldElapsedSeconds = 0.0F;
    } else if (requestedTurnDirection != state.world.player.turnHoldDirection) {
        state.world.player.hullHeadingRadians +=
            static_cast<float>(requestedTurnDirection) * kEightDirectionStepRadians;
        state.world.player.turnHoldDirection = requestedTurnDirection;
        state.world.player.turnHoldElapsedSeconds = 0.0F;
    } else {
        state.world.player.turnHoldElapsedSeconds += deltaSeconds;
        while (state.world.player.turnHoldElapsedSeconds >= kTurnRepeatIntervalSeconds) {
            state.world.player.hullHeadingRadians +=
                static_cast<float>(requestedTurnDirection) * kEightDirectionStepRadians;
            state.world.player.turnHoldElapsedSeconds -= kTurnRepeatIntervalSeconds;
        }
    }
    state.world.player.hullHeadingRadians = QuantizeToEightDirections(state.world.player.hullHeadingRadians);
    state.world.player.turretHeadingRadians +=
        input.turretTurnInput * GameplayConstants::kPlayerTurretTurnSpeedRadians * deltaSeconds;
    if (inStartMode) {
        // During refuel lock, allow heading/turret rotation but block movement/fire.
        state.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        state.world.player.throttleNormalized = 0.0F;
        return;
    }

    if (input.forwardButtonDown && !input.reverseButtonDown) {
        state.world.player.throttleNormalized += throttleRatePerSecond * deltaSeconds;
    } else if (input.reverseButtonDown && !input.forwardButtonDown) {
        state.world.player.throttleNormalized -= throttleRatePerSecond * deltaSeconds;
    }
    state.world.player.throttleNormalized =
        std::clamp(state.world.player.throttleNormalized, 0.0F, 1.0F);

    float speed = std::sqrt(
        state.world.player.velocity.x * state.world.player.velocity.x +
        state.world.player.velocity.y * state.world.player.velocity.y);

    speed += state.world.player.throttleNormalized * throttleAccelerationPerSecond * deltaSeconds;

    float targetNormalizedX = static_cast<float>(input.gamepadAxis0Raw) / joystickAxisRawMax;
    float targetNormalizedY = static_cast<float>(input.gamepadAxis1Raw) / joystickAxisRawMax;
    const bool joystickActive = (input.gamepadAxis0Raw != 0 || input.gamepadAxis1Raw != 0);
    if (!joystickActive && (input.moveX != 0.0F || input.moveY != 0.0F)) {
        targetNormalizedX = input.moveX;
        targetNormalizedY = input.moveY;
    }
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
    // 2× acceleration for UP/DOWN throttle responsiveness.
    const float transformStep =
        GameplayConstants::kJoystickAcceleration * 2.0F * deltaSeconds;
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
    state.world.player.hullHeadingRadians = QuantizeToEightDirections(state.world.player.hullHeadingRadians);

    speed = std::max(0.0F, speed);
    if (speed <= 0.001F) {
        state.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
    } else {
        const Vec2f snappedDirection = DirectionFromHeading(state.world.player.hullHeadingRadians);
        state.world.player.velocity.x = snappedDirection.x * speed;
        state.world.player.velocity.y = snappedDirection.y * speed;
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
