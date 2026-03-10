#pragma once

#include "game/model/EntityTypes.h"
#include "game/model/WorldState.h"

float SelectScoutHeadingWithFallback(const WorldState &world,
                                     const EnemyTank &enemy,
                                     bool allowNinetyTurns, bool &shouldRotate);
