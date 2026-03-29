#include "game/GameModeController.h"

#include "core/Log.h"

GameMode GameModeController::Mode() const {
    return mode_;
}

void GameModeController::RequestMenu() {
    mode_ = GameMode::Menu;
}

void GameModeController::StartGame(GameState& state, const MenuSettings& settings, const AppConfig& config) {
    (void)config;
    bolt::log::Debug("[FLOW] GameModeController::StartGame: settings.invisibility=%d level=%d",
        settings.invisibility ? 1 : 0, settings.levelNumber);
    state.menuSettings = settings;
    state.gameplayPhase = GameplayPhase::Starting;
    state.startingSequencePhase = 0;
    state.startingPhaseRemainingSeconds = 0.0F;
    state.gameOverPhaseRemainingSeconds = 0.0F;
    state.world.player.lives = GameplayConstants::kStartingLives;
    state.world.player.fuel = 0.0F;
    state.world.player.alive = true;
    state.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
    state.world.player.throttleNormalized = 0.0F;
    state.world.player.fireCooldownSeconds = 0.0F;
    state.world.playerTurnLostPending = false;
    state.world.enemyVisualContactMusicTimerSeconds = 0.0F;
    state.world.startModeRemainingSeconds = GameplayConstants::kStartModeDurationSeconds;
    state.world.startModeReason = StartModeReason::NewGame;
    state.world.deathModeRemainingSeconds = 0.0F;
    state.world.deathExplosionRemainingSeconds = 0.0F;
    state.world.deathExplosionBlastRemainingSeconds = 0.0F;
    state.world.score = 0;
    state.world.gameOver = false;
    mode_ = GameMode::Playing;
}
