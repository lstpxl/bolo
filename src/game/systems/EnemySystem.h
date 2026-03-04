#pragma once

#include "core/Random.h"
#include "game/GameplayView.h"
#include "game/GameState.h"

void UpdateEnemySystem(GameState& state, const GameplayView& view, float deltaSeconds, Random& random);
