#include "game/GameModeController.h"

GameMode GameModeController::Mode() const {
    return mode_;
}

void GameModeController::RequestMenu() {
    mode_ = GameMode::Menu;
}

void GameModeController::StartGame(GameState& state, const MenuSettings& settings) {
    state.menuSettings = settings;
    state.world.player.position = {.x = 0.0F, .y = 0.0F};
    state.world.player.velocity = {.x = 0.0F, .y = 0.0F};
    state.world.player.headingRadians = 0.0F;
    state.world.player.alive = true;
    state.world.enemyBase.activeEnemies = 0;
    state.world.score = 0;
    mode_ = GameMode::Playing;
}
