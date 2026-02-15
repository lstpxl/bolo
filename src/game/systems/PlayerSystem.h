#pragma once

#include "game/GameState.h"
#include "platform/Input.h"

void UpdatePlayerSystem(GameState& state, const FrameInput& input, float deltaSeconds);
