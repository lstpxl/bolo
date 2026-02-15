#pragma once

#include "app/AppConfig.h"
#include "game/GameState.h"

class Renderer2D {
public:
    void DrawWorld(const GameState& state, const AppConfig& config) const;
};
