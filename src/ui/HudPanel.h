#pragma once

#include "app/AppConfig.h"
#include "game/GameState.h"

class HudPanel {
public:
    void Render(const GameState& state, const AppConfig& config) const;
};
