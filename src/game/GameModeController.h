#pragma once

#include "app/AppConfig.h"
#include "core/Types.h"
#include "game/GameState.h"

class GameModeController {
public:
    GameMode Mode() const;

    void RequestMenu();
    void StartGame(GameState& state, const MenuSettings& settings, const AppConfig& config);

private:
    GameMode mode_ = GameMode::Menu;
};
