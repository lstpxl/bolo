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
    const AppConfig& config,
    const GameplayView& view,
    Random& random) {
    (void)config;
    state.menuSettings = settings;
    state.world.player.lives = GameplayConstants::kStartingLives;
    state.world.player.fuel = 0.0F;
    state.world.player.alive = true;
    state.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
    state.world.player.throttleNormalized = 0.0F;
    state.world.player.fireCooldownSeconds = 0.0F;
    state.world.playerTurnLostPending = false;
    state.world.startModeRemainingSeconds = GameplayConstants::kStartModeDurationSeconds;
    state.world.deathModeRemainingSeconds = 0.0F;
    state.world.deathExplosionRemainingSeconds = 0.0F;
    state.world.score = 0;
    state.world.gameOver = false;
    InitializeMazeWorld(state, view, random);
    mode_ = GameMode::Playing;
}
