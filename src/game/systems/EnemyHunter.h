#pragma once

#include "core/Random.h"
#include "game/model/EntityTypes.h"
#include "game/model/WorldState.h"
#include "game/navigation/CellCoordCache.h"

void InvalidateHunterScoutPath(EnemyTank& enemy);

float SelectScoutHeadingWithFallback(
    const WorldState& world, const EnemyTank& enemy, bool allowNinetyTurns, bool& shouldRotate);

bool SelectHunterScoutMotion(
    const WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    EnemyTank& enemy,
    Random& random,
    float& outHeadingRadians,
    Vec2f& outTargetPoint);
