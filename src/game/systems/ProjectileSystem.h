#pragma once

#include "game/GameState.h"

void SpawnProjectile(
    GameState& state,
    ProjectileOwner owner,
    const Vec2f& position,
    float headingRadians,
    float speedUnitsPerSecond,
    std::uint32_t shooterEnemySessionId = 0);
void UpdateProjectileSystem(GameState& state, float deltaSeconds);
