#include "game/systems/ProjectileSystem.h"

#include <cmath>

void SpawnProjectile(
    GameState& state,
    ProjectileOwner owner,
    const Vec2f& position,
    float headingRadians,
    float speedUnitsPerSecond) {
    state.world.projectiles.push_back(Projectile{
        .previousPosition = position,
        .position = position,
        .velocity =
            Vec2f{
                .x = std::sin(headingRadians) * speedUnitsPerSecond,
                .y = -std::cos(headingRadians) * speedUnitsPerSecond,
            },
        .owner = owner,
        .remainingLifeSeconds = GameplayConstants::kProjectileLifetimeSeconds,
        .alive = true,
    });
    state.world.gameplayEvents.Push(GameplayEvent{
        .type = GameplayEventType::ProjectileFired,
        .position = position,
        .projectileOwner = owner,
    });
}

void UpdateProjectileSystem(GameState& state, float deltaSeconds) {
    for (Projectile& projectile : state.world.projectiles) {
        if (!projectile.alive) {
            continue;
        }
        projectile.previousPosition = projectile.position;
        projectile.position.x += projectile.velocity.x * deltaSeconds;
        projectile.position.y += projectile.velocity.y * deltaSeconds;
        projectile.remainingLifeSeconds -= deltaSeconds;
        if (projectile.remainingLifeSeconds <= 0.0F) {
            projectile.alive = false;
        }
    }
}
