#include "game/Game.h"

#include "game/systems/CollisionSystem.h"
#include "game/systems/EnemySystem.h"
#include "game/systems/MazeSystem.h"
#include "game/systems/PlayerSystem.h"
#include "game/systems/ProjectileSystem.h"
#include "game/systems/SpawnerSystem.h"
#include "core/Profiling.h"
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
    profiling::ScopedProfile gameScope(profiling::Scope::GameUpdate, true);

    auto beginDeathMode = [&]() {
        if (state_.world.deathModeRemainingSeconds > 0.0F) {
            return;
        }
        state_.world.startModeRemainingSeconds = 0.0F;
        state_.world.player.alive = false;
        state_.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        state_.world.player.throttleNormalized = 0.0F;
        state_.world.player.fireCooldownSeconds = 0.0F;
        // Consume the "new death" trigger when death mode starts so it cannot re-trigger endlessly.
        state_.world.playerTurnLostPending = false;
        state_.world.deathModeRemainingSeconds = GameplayConstants::kDeathModeDurationSeconds;
        state_.world.deathExplosionRemainingSeconds = GameplayConstants::kDeathExplosionDurationSeconds;
        state_.world.deathExplosionPosition = state_.world.player.position;
    };

    if (state_.world.startModeRemainingSeconds > 0.0F) {
        state_.world.startModeRemainingSeconds =
            std::max(0.0F, state_.world.startModeRemainingSeconds - deltaSeconds);
        const float startProgress =
            1.0F - (state_.world.startModeRemainingSeconds / GameplayConstants::kStartModeDurationSeconds);
        state_.world.player.fuel = GameplayConstants::kFuelMax * std::clamp(startProgress, 0.0F, 1.0F);
    } else if (state_.world.player.fuel < GameplayConstants::kFuelMax && !state_.world.playerTurnLostPending) {
        state_.world.player.fuel = GameplayConstants::kFuelMax;
    }

    if (state_.world.deathModeRemainingSeconds > 0.0F) {
        state_.world.deathModeRemainingSeconds =
            std::max(0.0F, state_.world.deathModeRemainingSeconds - deltaSeconds);
    }
    if (state_.world.deathExplosionRemainingSeconds > 0.0F) {
        state_.world.deathExplosionRemainingSeconds =
            std::max(0.0F, state_.world.deathExplosionRemainingSeconds - deltaSeconds);
    }

    const bool playerLocked =
        state_.world.startModeRemainingSeconds > 0.0F ||
        state_.world.deathModeRemainingSeconds > 0.0F ||
        !state_.world.player.alive;

    // Target order: Input -> AI -> Movement -> Collision -> Combat -> Spawning -> Fuel/Rules -> Cleanup.
    {
        profiling::ScopedProfile scope(profiling::Scope::AiUpdate, true);
        UpdateEnemySystem(state_, config, deltaSeconds);
    }
    if (!playerLocked) {
        profiling::ScopedProfile scope(profiling::Scope::PlayerUpdate);
        UpdatePlayerSystem(state_, input, deltaSeconds);
    } else {
        state_.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        state_.world.player.throttleNormalized = 0.0F;
    }
    {
        profiling::ScopedProfile scope(profiling::Scope::ProjectileUpdate);
        UpdateProjectileSystem(state_, deltaSeconds);
    }
    {
        profiling::ScopedProfile scope(profiling::Scope::PhysicsCollisionUpdate, true);
        UpdateCollisionSystem(state_, deltaSeconds);
    }
    {
        profiling::ScopedProfile scope(profiling::Scope::SpawnerUpdate);
        UpdateSpawnerSystem(state_, deltaSeconds);
    }
    {
        profiling::ScopedProfile scope(profiling::Scope::MazeUpdate);
        UpdateMazeSystem(state_, deltaSeconds);
    }

    if (!state_.world.player.alive &&
        state_.world.playerTurnLostPending &&
        state_.world.deathModeRemainingSeconds <= 0.0F) {
        beginDeathMode();
    }

    // Fuel/rules.
    const float speedSq = state_.world.player.velocity.x * state_.world.player.velocity.x +
        state_.world.player.velocity.y * state_.world.player.velocity.y;
    if (!playerLocked && speedSq > GameplayConstants::kFuelDrainMovementThresholdSq) {
        state_.world.player.fuel -= deltaSeconds * (
            GameplayConstants::kFuelDrainBasePerSecond +
            state_.world.player.throttleNormalized * GameplayConstants::kFuelDrainThrottlePerSecond);
        if (state_.world.player.fuel <= 0.0F) {
            state_.world.player.fuel = 0.0F;
            beginDeathMode();
        }
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
    if (!state_.world.player.alive && state_.world.deathModeRemainingSeconds <= 0.0F) {
        state_.world.player.lives -= 1;
        if (state_.world.player.lives <= 0) {
            state_.world.gameOver = true;
            RequestMenu();
            return;
        }
        state_.world.player.alive = true;
        state_.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        state_.world.player.throttleNormalized = 0.0F;
        state_.world.player.fireCooldownSeconds = 0.0F;
        state_.world.player.fuel = 0.0F;
        state_.world.startModeRemainingSeconds = GameplayConstants::kStartModeDurationSeconds;
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
        state_.world.player.fuel = 0.0F;
        state_.world.startModeRemainingSeconds = GameplayConstants::kStartModeDurationSeconds;
        state_.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        state_.world.player.throttleNormalized = 0.0F;
        state_.world.levelCleared = true;
        state_.world.levelClearMessageSeconds = GameplayConstants::kLevelClearMessageSeconds;
    }

    if (state_.world.levelClearMessageSeconds > 0.0F) {
        state_.world.levelClearMessageSeconds = std::max(0.0F, state_.world.levelClearMessageSeconds - deltaSeconds);
    } else {
        state_.world.levelCleared = false;
    }
}

void Game::Render(IRenderer& renderer, const AppConfig& config, const FrameInput& input) const {
    if (modeController_.Mode() != GameMode::Playing) {
        return;
    }
    renderer.RenderGameplay(state_, config, input);
}
