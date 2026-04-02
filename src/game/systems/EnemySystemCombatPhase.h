#pragma once

#include "core/Random.h"
#include "game/model/EntityTypes.h"

struct GameState;

struct EnemyPerception {
    Vec2f toPlayer{};
    Vec2f toPlayerNormalized{};
    float distanceToPlayerSq = 0.0F;
    float distanceToPlayer = 0.0F;
    bool playerObscured = false;
    /// Assassin-only: fast-movement band when player is seen and within `kEnemyAggroRangeUnits`.
    bool assassinInAggroMode = false;
};

float EnemyFireInterval(EnemyType type);

EnemyPerception RunPerceptionPhase(
    GameState& state,
    EnemyTank& enemy,
    float deltaSeconds,
    bool playerInvisible,
    Random& random);

void RunFiringPhase(
    GameState& state,
    EnemyTank& enemy,
    const EnemyPerception& perception,
    float deltaSeconds);
