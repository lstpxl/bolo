#include "game/Game.h"

#include <algorithm>
#include <cstdint>
#include "core/Log.h"
#include "game/EnemyAppearance.h"
#include "game/model/EntityTypes.h"
#include "game/systems/CollisionSystem.h"
#include "game/systems/EnemySystem.h"
#include "game/systems/MazeSystem.h"
#include "game/systems/PlayerSystem.h"
#include "game/systems/ProjectileSystem.h"
#include "game/systems/SpawnerSystem.h"
#include "core/Profiling.h"
#include <random>

namespace {
std::uint32_t MakeSeed() {
    std::random_device device;
    return device();
}
}  // namespace

Game::Game()
    : runtimeContext_{.randomSeed = MakeSeed()}, random_(runtimeContext_.randomSeed) {}

GameMode Game::Mode() const {
    return modeController_.Mode();
}

const GameState& Game::State() const {
    return state_;
}

GameState& Game::MutableState() {
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

void Game::StartGame(const AppConfig& config, const GameplayView& view) {
    bolt::log::Debug("[FLOW] StartGame: draining worker, invisibility=%d", state_.menuSettings.invisibility ? 1 : 0);
    flowWorker_.Drain();
    modeController_.StartGame(state_, state_.menuSettings, config, view, random_);
    bolt::log::Debug("[FLOW] StartGame done: invisibility=%d level=%d",
        state_.menuSettings.invisibility ? 1 : 0, state_.menuSettings.levelNumber);
}

void Game::Update(const FrameInput& input, float deltaSeconds, const GameplayView& view) {
    if (modeController_.Mode() != GameMode::Playing) {
        return;
    }
    profiling::ScopedProfile gameScope(profiling::Scope::GameUpdate, true);

    // Pan mode: P toggles; W/A/S/D move viewport 1 cell when active.
    if (input.panTogglePressed) {
        state_.world.panModeActive = !state_.world.panModeActive;
        if (state_.world.panModeActive) {
            state_.world.panTarget = state_.world.player.position;
        }
    }
    if (input.invisibilityTogglePressed) {
        state_.menuSettings.invisibility = !state_.menuSettings.invisibility;
        const bool shouldHaveFlowActive =
            game::LevelHasFlowConsumers(state_.menuSettings.levelNumber) && !state_.menuSettings.invisibility;
        bolt::log::Debug("[INVIS] I pressed: invisibility=%d shouldHaveFlowActive=%d level=%d",
            state_.menuSettings.invisibility ? 1 : 0, shouldHaveFlowActive ? 1 : 0, state_.menuSettings.levelNumber);
        state_.world.navigationCache.playerFlowField.SetCacheActive(shouldHaveFlowActive);
        // Always invalidate on visibility mode transition:
        // - invisibility ON: clear any stale flow immediately (cheap-tier assassins should idle)
        // - invisibility OFF: force rebuild for current player cell
        state_.world.navigationCache.playerFlowField.Invalidate();
        state_.world.navigationCache.flowFieldInvalidationGeneration += 1;
    }
    if (state_.world.panModeActive) {
        const float cellSize = static_cast<float>(state_.world.maze.cellSizeUnits);
        const float mazeW = static_cast<float>(state_.world.maze.widthCells * state_.world.maze.cellSizeUnits);
        const float mazeH = static_cast<float>(state_.world.maze.heightCells * state_.world.maze.cellSizeUnits);
        if (input.panNorthPressed) {
            state_.world.panTarget.y = std::max(0.0F, state_.world.panTarget.y - cellSize);
        }
        if (input.panSouthPressed) {
            state_.world.panTarget.y = std::min(mazeH, state_.world.panTarget.y + cellSize);
        }
        if (input.panWestPressed) {
            state_.world.panTarget.x = std::max(0.0F, state_.world.panTarget.x - cellSize);
        }
        if (input.panEastPressed) {
            state_.world.panTarget.x = std::min(mazeW, state_.world.panTarget.x + cellSize);
        }
    }

    FrameInput playerInput = input;
    if (state_.world.panModeActive) {
        playerInput.moveX = 0.0F;
        playerInput.moveY = 0.0F;
    }

    auto beginDeathMode = [&]() {
        if (state_.world.deathModeRemainingSeconds > 0.0F) {
            return;
        }
        state_.world.startModeRemainingSeconds = 0.0F;
        state_.world.startModeReason = StartModeReason::Unknown;
        state_.world.player.alive = false;
        state_.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        state_.world.player.throttleNormalized = 0.0F;
        state_.world.player.fireCooldownSeconds = 0.0F;
        // Consume the "new death" trigger when death mode starts so it cannot re-trigger endlessly.
        state_.world.playerTurnLostPending = false;
        state_.world.deathModeRemainingSeconds = GameplayConstants::kDeathModeDurationSeconds;
        state_.world.deathExplosionRemainingSeconds = GameplayConstants::kDeathExplosionDurationSeconds;
        state_.world.deathExplosionPosition = state_.world.player.position;
        state_.world.gameplayEvents.Push(GameplayEvent{
            .type = GameplayEventType::PlayerExplosion,
            .position = state_.world.deathExplosionPosition,
        });
    };

    if (state_.world.startModeRemainingSeconds > 0.0F) {
        state_.world.startModeRemainingSeconds =
            std::max(0.0F, state_.world.startModeRemainingSeconds - deltaSeconds);
        const float startProgress =
            1.0F - (state_.world.startModeRemainingSeconds / GameplayConstants::kStartModeDurationSeconds);
        state_.world.player.fuel = GameplayConstants::kFuelMax * std::clamp(startProgress, 0.0F, 1.0F);
        if (state_.world.startModeRemainingSeconds <= 0.0F) {
            state_.world.startModeReason = StartModeReason::Unknown;
        }
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
        UpdateEnemySystem(state_, view, deltaSeconds, random_, flowWorker_);
    }
    bool anyEnemySeesPlayer = false;
    for (const EnemyTank& enemy : state_.world.enemies) {
        if (enemy.alive && enemy.seesPlayer) {
            anyEnemySeesPlayer = true;
            break;
        }
    }
    if (anyEnemySeesPlayer) {
        state_.world.enemyVisualContactMusicTimerSeconds =
            GameplayConstants::kEnemyVisualContactMusicHoldSeconds;
    } else {
        state_.world.enemyVisualContactMusicTimerSeconds =
            std::max(0.0F, state_.world.enemyVisualContactMusicTimerSeconds - deltaSeconds);
    }

    if (state_.world.player.alive && state_.world.deathModeRemainingSeconds <= 0.0F) {
        profiling::ScopedProfile scope(profiling::Scope::PlayerUpdate);
        UpdatePlayerSystem(state_, playerInput, deltaSeconds);
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
        UpdateSpawnerSystem(state_, deltaSeconds, random_);
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

    // Spawn explosions for enemies that died this frame.
    for (const EnemyTank& enemy : state_.world.enemies) {
        if (enemy.alive) {
            continue;
        }
        for (EnemyExplosion& slot : state_.world.enemyExplosions) {
            if (!slot.active) {
                slot.position = enemy.position;
                slot.elapsedSeconds = 0.0F;
                slot.active = true;
                break;
            }
        }
    }

    // Spawn explosions for projectiles that hit walls (same sprite/radius as enemy death).
    for (std::size_t ei = 0; ei < state_.world.gameplayEvents.count; ++ei) {
        if (state_.world.gameplayEvents.events[ei].type != GameplayEventType::ProjectileHitWall) {
            continue;
        }
        const Vec2f pos = state_.world.gameplayEvents.events[ei].position;
        for (EnemyExplosion& slot : state_.world.enemyExplosions) {
            if (!slot.active) {
                slot.position = pos;
                slot.elapsedSeconds = 0.0F;
                slot.active = true;
                break;
            }
        }
    }

    // Spawn explosions for bases destroyed this frame.
    for (EnemyBase& base : state_.world.enemyBases) {
        if (!base.destroyed || base.explosionPlayed) {
            continue;
        }
        base.explosionPlayed = true;
        for (EnemyExplosion& slot : state_.world.baseExplosions) {
            if (!slot.active) {
                slot.position = base.position;
                slot.elapsedSeconds = 0.0F;
                slot.active = true;
                break;
            }
        }
    }

    // Tick active explosions.
    for (EnemyExplosion& explosion : state_.world.enemyExplosions) {
        if (!explosion.active) {
            continue;
        }
        explosion.elapsedSeconds += deltaSeconds;
        if (explosion.elapsedSeconds >= GameplayConstants::kExplosionTotalDurationSeconds) {
            explosion.active = false;
        }
    }
    for (EnemyExplosion& explosion : state_.world.baseExplosions) {
        if (!explosion.active) {
            continue;
        }
        explosion.elapsedSeconds += deltaSeconds;
        if (explosion.elapsedSeconds >= GameplayConstants::kExplosionTotalDurationSeconds) {
            explosion.active = false;
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
        state_.world.enemyVisualContactMusicTimerSeconds = 0.0F;
        state_.world.startModeRemainingSeconds = GameplayConstants::kStartModeDurationSeconds;
        state_.world.startModeReason = StartModeReason::Respawn;
        PlacePlayerAtSafeSpawn(state_, view, random_);

        state_.world.navigationCache.playerFlowField.Invalidate();
        state_.world.navigationCache.flowFieldInvalidationGeneration += 1;
        const bool hasFlowConsumers =
            std::any_of(state_.world.enemies.begin(),
                        state_.world.enemies.end(),
                        [](const EnemyTank& e) {
                            return e.alive && (e.type == EnemyType::Assassin || e.type == EnemyType::Hunter);
                        });
        const bool shouldHaveFlowActive =
            hasFlowConsumers && !state_.menuSettings.invisibility;
        bolt::log::Debug("[FLOW] Respawn: invisibility=%d hasFlowConsumers=%d shouldHaveFlowActive=%d",
            state_.menuSettings.invisibility ? 1 : 0, hasFlowConsumers ? 1 : 0, shouldHaveFlowActive ? 1 : 0);
        state_.world.navigationCache.playerFlowField.SetCacheActive(shouldHaveFlowActive);
        state_.world.gameplayEvents.Push(GameplayEvent{
            .type = GameplayEventType::StartModeStarted,
            .position = state_.world.player.position,
            .startModeReason = StartModeReason::Respawn,
        });
    }

    // Level complete handling.
    int aliveBases = 0;
    for (const EnemyBase& base : state_.world.enemyBases) {
        if (!base.destroyed) {
            ++aliveBases;
        }
    }
    if (aliveBases == 0) {
        bolt::log::Debug("[FLOW] Level complete: draining, invisibility=%d", state_.menuSettings.invisibility ? 1 : 0);
        flowWorker_.Drain();
        const int score = state_.world.score;
        const int lives = state_.world.player.lives;
        InitializeMazeWorld(state_, view, random_);
        state_.world.score = score;
        state_.world.player.lives = lives;
        state_.world.player.fuel = 0.0F;
        state_.world.enemyVisualContactMusicTimerSeconds = 0.0F;
        state_.world.startModeRemainingSeconds = GameplayConstants::kStartModeDurationSeconds;
        state_.world.startModeReason = StartModeReason::LevelComplete;
        state_.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        state_.world.player.throttleNormalized = 0.0F;
        state_.world.levelCleared = true;
        state_.world.levelClearMessageSeconds = GameplayConstants::kLevelClearMessageSeconds;
        state_.world.gameplayEvents.Push(GameplayEvent{
            .type = GameplayEventType::StartModeStarted,
            .position = state_.world.player.position,
            .startModeReason = StartModeReason::LevelComplete,
        });
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

bool Game::IsGameplayMusicTense() const {
    return modeController_.Mode() == GameMode::Playing &&
        state_.world.enemyVisualContactMusicTimerSeconds > 0.0F;
}
