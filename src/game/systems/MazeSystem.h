#pragma once

#include "app/AppConfig.h"
#include "game/GameState.h"

void InitializeMazeWorld(GameState& state, const AppConfig& config);
bool PlacePlayerAtSafeSpawn(GameState& state, const AppConfig& config);
void UpdateMazeSystem(GameState& state, float deltaSeconds);
