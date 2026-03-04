#pragma once

#include "app/AppConfig.h"
#include "core/Random.h"
#include "core/Types.h"
#include "game/GameplayView.h"
#include "game/GameState.h"

class GameModeController {
public:
    GameMode Mode() const;

    void RequestMenu();
    void StartGame(
        GameState& state,
        const MenuSettings& settings,
        const AppConfig& config,
        const GameplayView& view,
        Random& random);

private:
    GameMode mode_ = GameMode::Menu;
};
