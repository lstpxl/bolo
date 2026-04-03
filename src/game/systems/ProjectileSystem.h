#pragma once

#include <cstdint>
#include "core/Types.h"
#include "game/GameplayView.h"
#include "game/model/EntityTypes.h"

struct GameState;

void SpawnProjectile(
    GameState& state,
    ProjectileOwner owner,
    const Vec2f& position,
    float headingRadians,
    float speedUnitsPerSecond,
    std::uint32_t shooterEnemySessionId = 0);
void UpdateProjectileSystem(GameState& state, float deltaSeconds, const GameplayView& view);
