#include "game/Game.h"

#include "game/systems/CollisionSystem.h"
#include "game/systems/EnemySystem.h"
#include "game/systems/MazeSystem.h"
#include "game/systems/PlayerSystem.h"
#include "game/systems/ProjectileSystem.h"
#include "game/systems/SpawnerSystem.h"
#include <algorithm>

GameMode Game::Mode() const {
    return modeController_.Mode();
}

const GameState& Game::State() const {
    return state_;
}

MenuSettings Game::CurrentMenuSettings() const {
    return state_.menuSettings;
}

void Game::SetMenuSettings(const MenuSettings& settings) {
    state_.menuSettings = settings;
}

void Game::RequestMenu() {
    modeController_.RequestMenu();
}

void Game::StartGame(const AppConfig& config) {
    modeController_.StartGame(state_, state_.menuSettings, config);
}

void Game::Update(const FrameInput& input, float deltaSeconds, const AppConfig& config) {
    if (modeController_.Mode() != GameMode::Playing) {
        return;
    }

    // Target order: Input -> AI -> Movement -> Collision -> Combat -> Spawning -> Fuel/Rules -> Cleanup.
    UpdateEnemySystem(state_, config, deltaSeconds);
    UpdatePlayerSystem(state_, input, deltaSeconds);
    UpdateProjectileSystem(state_, deltaSeconds);
    UpdateCollisionSystem(state_, deltaSeconds);
    UpdateSpawnerSystem(state_, deltaSeconds);
    UpdateMazeSystem(state_, deltaSeconds);

    // Fuel/rules.
    const float speedSq = state_.world.player.velocity.x * state_.world.player.velocity.x +
        state_.world.player.velocity.y * state_.world.player.velocity.y;
    if (speedSq > GameplayConstants::kFuelDrainMovementThresholdSq) {
        state_.world.player.fuel -= deltaSeconds * (
            GameplayConstants::kFuelDrainBasePerSecond +
            state_.world.player.throttleNormalized * GameplayConstants::kFuelDrainThrottlePerSecond);
    }
    if (state_.world.player.fuel <= 0.0F) {
        state_.world.player.fuel = 0.0F;
        state_.world.player.alive = false;
        state_.world.playerTurnLostPending = true;
    }

    // Cleanup dead entities.
    state_.world.projectiles.erase(
        std::remove_if(
            state_.world.projectiles.begin(),
            state_.world.projectiles.end(),
            [](const Projectile& projectile) { return !projectile.alive; }),
        state_.world.projectiles.end());
    state_.world.enemies.erase(
        std::remove_if(
            state_.world.enemies.begin(),
            state_.world.enemies.end(),
            [](const EnemyTank& enemy) { return !enemy.alive; }),
        state_.world.enemies.end());

    // Turn loss handling.
    if (state_.world.playerTurnLostPending) {
        state_.world.playerTurnLostPending = false;
        state_.world.player.lives -= 1;
        if (state_.world.player.lives <= 0) {
            state_.world.gameOver = true;
            RequestMenu();
            return;
        }
        state_.world.player.fuel = GameplayConstants::kFuelMax;
        PlacePlayerAtSafeSpawn(state_, config);
    }

    // Level complete handling.
    int aliveBases = 0;
    for (const EnemyBase& base : state_.world.enemyBases) {
        if (!base.destroyed) {
            ++aliveBases;
        }
    }
    if (aliveBases == 0) {
        const int score = state_.world.score;
        const int lives = state_.world.player.lives;
        InitializeMazeWorld(state_, config);
        state_.world.score = score;
        state_.world.player.lives = lives;
        state_.world.player.fuel = GameplayConstants::kFuelMax;
        state_.world.levelCleared = true;
        state_.world.levelClearMessageSeconds = GameplayConstants::kLevelClearMessageSeconds;
    }

    if (state_.world.levelClearMessageSeconds > 0.0F) {
        state_.world.levelClearMessageSeconds = std::max(0.0F, state_.world.levelClearMessageSeconds - deltaSeconds);
    } else {
        state_.world.levelCleared = false;
    }
}

void Game::Render(IRenderer& renderer, const AppConfig& config) const {
    if (modeController_.Mode() != GameMode::Playing) {
        return;
    }
    renderer.RenderGameplay(state_, config);
}
