#include "game/GameModeController.h"

#include "game/systems/MazeSystem.h"

GameMode GameModeController::Mode() const {
    return mode_;
}

void GameModeController::RequestMenu() {
    mode_ = GameMode::Menu;
}

void GameModeController::StartGame(
    GameState& state,
    const MenuSettings& settings,
    const AppConfig& config) {
    state.menuSettings = settings;
    state.world.player.lives = GameplayConstants::kStartingLives;
    state.world.player.fuel = GameplayConstants::kFuelMax;
    state.world.score = 0;
    state.world.gameOver = false;
    InitializeMazeWorld(state, config);
    mode_ = GameMode::Playing;
}
