#pragma once

#include "core/Random.h"
#include "game/GameplayView.h"
#include "game/navigation/CellCoordCache.h"
#include "game/navigation/PlayerFlowField.h"

struct GameState;

void AdvanceCheapTierTimers(
    GameState& state,
    EnemyTank& enemy,
    float deltaSeconds,
    bool playerInvisible,
    const GameplayView& view,
    Random& random);

void ApplyCheapTierMovement(
    GameState& state,
    const game::navigation::CellCoordCache& cellCache,
    const game::navigation::PlayerFlowField& flowField,
    EnemyTank& enemy,
    int enemyIndex,
    float deltaSeconds,
    float speed,
    Random& random);
