#pragma once

#include "app/AppConfig.h"
#include "game/GameState.h"
#include "platform/Input.h"

class DebugOverlayRenderer {
public:
    void Draw(const GameState& state, const AppConfig& config, const FrameInput& input) const;
};
