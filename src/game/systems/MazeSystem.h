#pragma once

#include "core/Random.h"
#include "game/GameplayView.h"
#include "game/GameState.h"

void InitializeMazeWorld(GameState& state, const GameplayView& view, Random& random);
bool PlacePlayerAtSafeSpawn(GameState& state, const GameplayView& view, Random& random);
void UpdateMazeSystem(GameState& state, float deltaSeconds);
