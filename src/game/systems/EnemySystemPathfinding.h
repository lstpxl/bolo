#pragma once

#include "core/Random.h"
#include "game/navigation/CellCoordCache.h"

struct GameState;
struct EnemyTank;

bool BuildAssassinPath(
    GameState& state,
    const game::navigation::CellCoordCache& cellCache,
    EnemyTank& enemy,
    int enemyIndex);

bool BuildAssassinPathToFarRandomTarget(
    GameState& state,
    const game::navigation::CellCoordCache& cellCache,
    EnemyTank& enemy,
    int enemyIndex,
    Random& random);
