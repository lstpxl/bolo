#include "game/Game.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <vector>
#include "core/Log.h"
#include "game/EnemyAppearance.h"
#include "game/model/GameplayEvents.h"
#include "game/model/EntityTypes.h"
#include "game/systems/CollisionSystem.h"
#include "game/systems/EnemySystem.h"
#include "game/systems/EnemySystemHelpers.h"
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

float DistanceSqExplosion(const Vec2f& a, const Vec2f& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

bool CanTraverseMazeCardinal(const MazeState& maze, int fromX, int fromY, int toX, int toY) {
    if (toX < 0 || toY < 0 || toX >= maze.widthCells || toY >= maze.heightCells) {
        return false;
    }
    const MazeCell& from = maze.cells[static_cast<std::size_t>(fromY * maze.widthCells + fromX)];
    if (toX == fromX + 1) {
        return !from.eastWall;
    }
    if (toX == fromX - 1) {
        return !from.westWall;
    }
    if (toY == fromY + 1) {
        return !from.southWall;
    }
    if (toY == fromY - 1) {
        return !from.northWall;
    }
    return false;
}

int MazeCellHash(int cellX, int cellY, int widthCells) {
    return cellY * widthCells + cellX;
}

bool IsPlayerInsideEvacZone(const WorldState& world) {
    if (!world.evacObjectiveActive || !world.player.alive) {
        return false;
    }
    const float halfZone = GameplayConstants::kEvacZoneSizeUnits * 0.5F;
    const float innerHalf = halfZone - GameplayConstants::kEntityRadiusUnits;
    if (innerHalf <= 0.0F) {
        return false;
    }
    const float dx = std::fabs(world.player.position.x - world.evacZoneCenter.x);
    const float dy = std::fabs(world.player.position.y - world.evacZoneCenter.y);
    return dx <= innerHalf && dy <= innerHalf;
}


void ApplyExplosionBlast(GameState& state, const Vec2f& center, float radius) {
    WorldState& world = state.world;
    const float rSq = radius * radius;
    if (world.player.alive &&
        DistanceSqExplosion(world.player.position, center) <= rSq) {
        world.player.alive = false;
        world.playerTurnLostPending = true;
    }
    for (EnemyTank& enemy : world.enemies) {
        if (!enemy.alive) {
            continue;
        }
        if (enemy.simTier != EnemySimTier::Full) {
            continue;
        }
        if (DistanceSqExplosion(enemy.position, center) > rSq) {
            continue;
        }
        world.gameplayEvents.Push(GameplayEvent{
            .type = GameplayEventType::EnemyDestroyed,
            .position = enemy.position,
            .enemyType = enemy.type,
            .enemySubtype = enemy.subtype,
        });
        enemy.alive = false;
        DecrementOriginBaseAliveCount(world, enemy);
        world.score +=
            state.menuSettings.levelNumber * GameplayConstants::kEnemyScorePerLevelMultiplier;
    }
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
    // Stop any pending async flow-field work before dropping gameplay state.
    flowWorker_.Drain();
    state_.world = WorldState{};
    state_.gameplayPhase = GameplayPhase::Starting;
    state_.startingPhaseRemainingSeconds = 0.0F;
    state_.gameOverPhaseRemainingSeconds = 0.0F;
    state_.victoryPhaseRemainingSeconds = 0.0F;
    state_.gameOverAwaitInputClear = false;
    state_.victoryAwaitInputClear = false;
    state_.startingSequencePhase = 0;
    modeController_.RequestMenu();
}

void Game::StartGame(const AppConfig& config, const GameplayView& view) {
    (void)view;
    bolt::log::Debug("[FLOW] StartGame: draining worker, invisibility=%d", state_.menuSettings.invisibility ? 1 : 0);
    flowWorker_.Drain();
    modeController_.StartGame(state_, state_.menuSettings, config);
    bolt::log::Debug("[FLOW] StartGame done: invisibility=%d level=%d",
        state_.menuSettings.invisibility ? 1 : 0, state_.menuSettings.levelNumber);
}

void Game::Update(const FrameInput& input, float deltaSeconds, const GameplayView& view) {
    if (modeController_.Mode() != GameMode::Playing) {
        return;
    }
    profiling::ScopedProfile gameScope(profiling::Scope::GameUpdate, true);

    switch (state_.gameplayPhase) {
    case GameplayPhase::Starting:
        UpdateStartingPhase(deltaSeconds, view);
        return;
    case GameplayPhase::Active:
        UpdateActivePhase(input, deltaSeconds, view);
        return;
    case GameplayPhase::EvacObjective:
        UpdateEvacObjectivePhase(input, deltaSeconds, view);
        return;
    case GameplayPhase::GameOver:
        UpdateGameOverPhase(input, deltaSeconds, view);
        return;
    case GameplayPhase::Victory:
        UpdateVictoryPhase(input, deltaSeconds, view);
        return;
    }
}

void Game::UpdateStartingPhase(float deltaSeconds, const GameplayView& view) {
    if (state_.startingSequencePhase == 0) {
        state_.startingPhaseRemainingSeconds = GameplayConstants::kGameplayStartingPhaseMinSeconds;
        state_.startingSequencePhase = 1;
        return;
    }
    if (state_.startingSequencePhase == 1) {
        flowWorker_.Drain();
        InitializeMazeWorld(state_, view, random_);
        flowWorker_.stableMaze = state_.world.maze;
        flowWorker_.stableCellCoords = state_.world.navigationCache.cellCoords;
        state_.startingSequencePhase = 2;
        state_.startingPhaseRemainingSeconds =
            std::max(0.0F, state_.startingPhaseRemainingSeconds - deltaSeconds);
        return;
    }
    state_.startingPhaseRemainingSeconds =
        std::max(0.0F, state_.startingPhaseRemainingSeconds - deltaSeconds);
    if (state_.startingPhaseRemainingSeconds <= 0.0F) {
        state_.gameplayPhase = GameplayPhase::Active;
        state_.startingSequencePhase = 0;
        state_.world.gameplayEvents.Push(GameplayEvent{
            .type = GameplayEventType::StartModeStarted,
            .position = state_.world.player.position,
            .startModeReason = StartModeReason::NewGame,
        });
    }
}

void Game::UpdateActivePhase(const FrameInput& input, float deltaSeconds, const GameplayView& view) {
    RunPlayingWorldTick(
        input,
        deltaSeconds,
        view,
        true,
        true,
        true,
        true,
        true);
}

void Game::UpdateEvacObjectivePhase(
    const FrameInput& input,
    float deltaSeconds,
    const GameplayView& view) {
    RunPlayingWorldTick(
        input,
        deltaSeconds,
        view,
        true,
        true,
        true,
        true,
        false);
    CheckEvacZoneCompletion();
}

bool Game::TrySelectEvacZoneCell(int& outCellX, int& outCellY) {
    const MazeState& maze = state_.world.maze;
    const game::navigation::CellCoordCache& cellCache = state_.world.navigationCache.cellCoords;
    if (maze.widthCells <= 0 || maze.heightCells <= 0 || maze.cells.empty()) {
        return false;
    }
    const game::navigation::MazeCellCoord playerCell = cellCache.WorldToCell(state_.world.player.position);
    if (!cellCache.IsValidCell(playerCell.x, playerCell.y)) {
        return false;
    }

    const int totalCells = maze.widthCells * maze.heightCells;
    std::vector<int> distance(static_cast<std::size_t>(totalCells), std::numeric_limits<int>::max());
    std::vector<std::uint8_t> ruined(static_cast<std::size_t>(totalCells), 0U);
    for (const EnemyBase& base : state_.world.enemyBases) {
        if (!base.destroyed) {
            continue;
        }
        const game::navigation::MazeCellCoord baseCell = cellCache.WorldToCell(base.position);
        if (!cellCache.IsValidCell(baseCell.x, baseCell.y)) {
            continue;
        }
        const int baseHash = MazeCellHash(baseCell.x, baseCell.y, maze.widthCells);
        ruined[static_cast<std::size_t>(baseHash)] = 1U;
    }

    const int startHash = MazeCellHash(playerCell.x, playerCell.y, maze.widthCells);
    distance[static_cast<std::size_t>(startHash)] = 0;
    std::deque<int> queue{};
    queue.push_back(startHash);
    constexpr std::array<int, 4> kDx{1, -1, 0, 0};
    constexpr std::array<int, 4> kDy{0, 0, 1, -1};
    while (!queue.empty()) {
        const int currentHash = queue.front();
        queue.pop_front();
        const int cx = currentHash % maze.widthCells;
        const int cy = currentHash / maze.widthCells;
        const int currentDistance = distance[static_cast<std::size_t>(currentHash)];
        for (int i = 0; i < 4; ++i) {
            const int nx = cx + kDx[static_cast<std::size_t>(i)];
            const int ny = cy + kDy[static_cast<std::size_t>(i)];
            if (!cellCache.IsValidCell(nx, ny)) {
                continue;
            }
            if (!CanTraverseMazeCardinal(maze, cx, cy, nx, ny)) {
                continue;
            }
            const int nextHash = MazeCellHash(nx, ny, maze.widthCells);
            if (distance[static_cast<std::size_t>(nextHash)] <= currentDistance + 1) {
                continue;
            }
            distance[static_cast<std::size_t>(nextHash)] = currentDistance + 1;
            queue.push_back(nextHash);
        }
    }

    std::vector<int> candidates{};
    candidates.reserve(static_cast<std::size_t>(totalCells));
    for (int hash = 0; hash < totalCells; ++hash) {
        const int dist = distance[static_cast<std::size_t>(hash)];
        if (dist == std::numeric_limits<int>::max() || ruined[static_cast<std::size_t>(hash)] != 0U) {
            continue;
        }
        if (dist >= GameplayConstants::kEvacZoneMinDistanceFromPlayerCells &&
            dist <= GameplayConstants::kEvacZoneMaxDistanceFromPlayerCells) {
            candidates.push_back(hash);
        }
    }

    if (candidates.empty()) {
        return false;
    }
    const int index = random_.NextInt(0, static_cast<int>(candidates.size()) - 1);
    const int selectedHash = candidates[static_cast<std::size_t>(index)];
    outCellX = selectedHash % maze.widthCells;
    outCellY = selectedHash / maze.widthCells;
    return true;
}

void Game::StartEvacObjectivePhase() {
    flowWorker_.Drain();
    state_.world.enemyVisualContactMusicTimerSeconds = 0.0F;
    state_.world.levelCleared = false;
    state_.world.levelClearMessageSeconds = 0.0F;
    state_.world.playerTurnLostPending = false;

    int evacCellX = -1;
    int evacCellY = -1;
    if (!TrySelectEvacZoneCell(evacCellX, evacCellY)) {
        bolt::log::Warning(
            "[FLOW] Evac objective fallback: no valid zone cell in %d..%d cells; entering Victory",
            GameplayConstants::kEvacZoneMinDistanceFromPlayerCells,
            GameplayConstants::kEvacZoneMaxDistanceFromPlayerCells);
        state_.gameplayPhase = GameplayPhase::Victory;
        state_.victoryPhaseRemainingSeconds = GameplayConstants::kVictoryPhaseDurationSeconds;
        state_.victoryAwaitInputClear = true;
        return;
    }
    state_.world.evacObjectiveActive = true;
    state_.world.evacZoneCellX = evacCellX;
    state_.world.evacZoneCellY = evacCellY;
    state_.world.evacZoneCenter = state_.world.navigationCache.cellCoords.CellCenter(evacCellX, evacCellY);
    state_.gameplayPhase = GameplayPhase::EvacObjective;
}

void Game::CheckEvacZoneCompletion() {
    if (!IsPlayerInsideEvacZone(state_.world)) {
        return;
    }
    const Vec2f completionPos = state_.world.player.position;
    state_.world.gameplayEvents.Push(GameplayEvent{
        .type = GameplayEventType::EvacZoneCompleted,
        .position = completionPos,
    });
    state_.world.evacObjectiveActive = false;
    state_.world.player.alive = false;
    state_.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
    state_.world.player.throttleNormalized = 0.0F;
    state_.world.playerTurnLostPending = false;
    state_.gameplayPhase = GameplayPhase::Victory;
    state_.victoryPhaseRemainingSeconds = GameplayConstants::kVictoryPhaseDurationSeconds;
    state_.victoryAwaitInputClear = true;
}

void Game::UpdateGameOverPhase(const FrameInput& input, float deltaSeconds, const GameplayView& view) {
    RunPlayingWorldTick(
        input,
        deltaSeconds,
        view,
        false,
        false,
        false,
        false,
        false);
    if (state_.gameOverAwaitInputClear) {
        if (!input.anyInteractionDown) {
            state_.gameOverAwaitInputClear = false;
        }
        return;
    }
    if (input.anyInteractionPressed) {
        RequestMenu();
    }
}

void Game::UpdateVictoryPhase(const FrameInput& input, float deltaSeconds, const GameplayView& view) {
    RunPlayingWorldTick(
        input,
        deltaSeconds,
        view,
        false,
        false,
        false,
        false,
        false);
    state_.victoryPhaseRemainingSeconds =
        std::max(0.0F, state_.victoryPhaseRemainingSeconds - deltaSeconds);
    if (state_.victoryPhaseRemainingSeconds > 0.0F) {
        return;
    }
    if (state_.victoryAwaitInputClear) {
        if (!input.anyInteractionDown) {
            state_.victoryAwaitInputClear = false;
        }
        return;
    }
    if (input.anyInteractionPressed) {
        RequestMenu();
    }
}

void Game::RunPlayingWorldTick(
    const FrameInput& input,
    float deltaSeconds,
    const GameplayView& view,
    bool allowPlayerDriving,
    bool allowPendingDeathTrigger,
    bool allowFuelDeath,
    bool allowTurnLossAndRespawn,
    bool allowLevelComplete) {

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
            (game::LevelHasFlowConsumers(state_.menuSettings.levelNumber) || state_.menuSettings.debugInfo) &&
            !state_.menuSettings.invisibility;
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
    if (state_.world.panModeActive || !allowPlayerDriving) {
        playerInput.moveX = 0.0F;
        playerInput.moveY = 0.0F;
    }

    auto beginDeathMode = [&]() {
        if (state_.world.deathModeRemainingSeconds > 0.0F) {
            return;
        }
        state_.world.startModeRemainingSeconds = 0.0F;
        state_.world.startModeDurationSeconds = 0.0F;
        state_.world.startModeReason = StartModeReason::Unknown;
        state_.world.startModeFuelRampStart = 0.0F;
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
        state_.world.deathExplosionBlastRemainingSeconds =
            GameplayConstants::kExplosionBlastDamageDurationSeconds;
    };

    if (state_.world.startModeRemainingSeconds > 0.0F) {
        state_.world.startModeRemainingSeconds =
            std::max(0.0F, state_.world.startModeRemainingSeconds - deltaSeconds);
        const float startModeDuration =
            std::max(0.0001F, state_.world.startModeDurationSeconds);
        const float startProgress =
            1.0F - (state_.world.startModeRemainingSeconds / startModeDuration);
        const float p = std::clamp(startProgress, 0.0F, 1.0F);
        if (state_.world.startModeReason == StartModeReason::BaseRefuel) {
            const float a = state_.world.startModeFuelRampStart;
            state_.world.player.fuel = a + (GameplayConstants::kFuelMax - a) * p;
        } else {
            state_.world.player.fuel = GameplayConstants::kFuelMax * p;
        }
        if (state_.world.startModeRemainingSeconds <= 0.0F) {
            state_.world.startModeDurationSeconds = 0.0F;
            state_.world.startModeReason = StartModeReason::Unknown;
            state_.world.startModeFuelRampStart = 0.0F;
        }
    }

    if (state_.world.deathModeRemainingSeconds > 0.0F) {
        state_.world.deathModeRemainingSeconds =
            std::max(0.0F, state_.world.deathModeRemainingSeconds - deltaSeconds);
    }
    if (state_.world.deathExplosionRemainingSeconds > 0.0F) {
        state_.world.deathExplosionRemainingSeconds =
            std::max(0.0F, state_.world.deathExplosionRemainingSeconds - deltaSeconds);
    }

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

    if (allowPlayerDriving && state_.world.player.alive && state_.world.deathModeRemainingSeconds <= 0.0F) {
        profiling::ScopedProfile scope(profiling::Scope::PlayerUpdate);
        UpdatePlayerSystem(state_, playerInput, deltaSeconds);
    } else {
        state_.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        state_.world.player.throttleNormalized = 0.0F;
    }
    {
        profiling::ScopedProfile scope(profiling::Scope::ProjectileUpdate);
        UpdateProjectileSystem(state_, deltaSeconds, view);
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

    if (allowPendingDeathTrigger &&
        !state_.world.player.alive &&
        state_.world.playerTurnLostPending &&
        state_.world.deathModeRemainingSeconds <= 0.0F) {
        beginDeathMode();
    }

    // Fuel/rules (evaluate after collision so start-mode refuel from base destruction suppresses drain
    // on the same tick refuel begins).
    const bool fuelDrainLocked =
        state_.world.startModeRemainingSeconds > 0.0F ||
        state_.world.deathModeRemainingSeconds > 0.0F ||
        !state_.world.player.alive;
    const float speedSq = state_.world.player.velocity.x * state_.world.player.velocity.x +
        state_.world.player.velocity.y * state_.world.player.velocity.y;
    if (allowFuelDeath && !fuelDrainLocked) {
        const float speed = std::sqrt(speedSq);
        const float speedRatio = speed / GameplayConstants::kPlayerFullVelocity;
        const float drainScale = 1.0F + speedRatio;
        const int mazeDensityClamped =
            std::max(1, std::min(5, state_.menuSettings.mazeDensity));
        const float mazeDensityDrainScale = std::max(
            0.0F,
            1.0F - GameplayConstants::kFuelDrainReductionPerAdditionalMazeDensity *
                static_cast<float>(mazeDensityClamped - 1));
        const float drainPerSecond =
            (GameplayConstants::kFuelDrainPercentOfMaxPerSecond / 100.0F) *
            GameplayConstants::kFuelMax * drainScale * mazeDensityDrainScale;
        state_.world.player.fuel -= deltaSeconds * drainPerSecond;
        state_.world.player.fuel = std::max(0.0F, state_.world.player.fuel);
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

    // Blast damage for kExplosionBlastDamageDurationSeconds (VFX continues to kExplosionTotalDurationSeconds).
    for (EnemyExplosion& ex : state_.world.enemyExplosions) {
        if (ex.active &&
            ex.elapsedSeconds < GameplayConstants::kExplosionBlastDamageDurationSeconds) {
            ApplyExplosionBlast(
                state_,
                ex.position,
                GameplayConstants::kExplosionBlastRadiusUnits);
        }
    }
    for (EnemyExplosion& ex : state_.world.baseExplosions) {
        if (ex.active &&
            ex.elapsedSeconds < GameplayConstants::kExplosionBlastDamageDurationSeconds) {
            ApplyExplosionBlast(
                state_,
                ex.position,
                GameplayConstants::kBaseExplosionBlastRadiusUnits);
        }
    }
    if (state_.world.deathExplosionBlastRemainingSeconds > 0.0F) {
        ApplyExplosionBlast(
            state_,
            state_.world.deathExplosionPosition,
            GameplayConstants::kExplosionBlastRadiusUnits);
        state_.world.deathExplosionBlastRemainingSeconds =
            std::max(0.0F, state_.world.deathExplosionBlastRemainingSeconds - deltaSeconds);
    }

    // Blast (and other late-tick kills) can set `playerTurnLostPending` after the earlier death trigger;
    // arm death mode here so turn loss waits for `kDeathModeDurationSeconds`.
    if (allowPendingDeathTrigger &&
        !state_.world.player.alive &&
        state_.world.playerTurnLostPending &&
        state_.world.deathModeRemainingSeconds <= 0.0F) {
        beginDeathMode();
    }

    // Spawn explosions for all enemies that died this frame (including blast chain-kills above).
    for (EnemyTank& enemy : state_.world.enemies) {
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
    if (allowTurnLossAndRespawn &&
        !state_.world.player.alive &&
        state_.world.deathModeRemainingSeconds <= 0.0F) {
        state_.world.player.lives -= 1;
        if (state_.world.player.lives <= 0) {
            state_.world.gameOver = true;
            state_.gameplayPhase = GameplayPhase::GameOver;
            state_.gameOverPhaseRemainingSeconds = 0.0F;
            state_.gameOverAwaitInputClear = true;
            state_.world.playerTurnLostPending = false;
            return;
        }
        state_.world.player.alive = true;
        state_.world.player.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        state_.world.player.throttleNormalized = 0.0F;
        state_.world.player.fireCooldownSeconds = 0.0F;
        state_.world.player.fuel = 0.0F;
        state_.world.deathExplosionBlastRemainingSeconds = 0.0F;
        state_.world.enemyVisualContactMusicTimerSeconds = 0.0F;
        state_.world.startModeRemainingSeconds = GameplayConstants::kStartModeDurationSeconds;
        state_.world.startModeDurationSeconds = GameplayConstants::kStartModeDurationSeconds;
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
            (hasFlowConsumers || state_.menuSettings.debugInfo) &&
            !state_.menuSettings.invisibility;
        bolt::log::Debug("[FLOW] Respawn: invisibility=%d hasFlowConsumers=%d shouldHaveFlowActive=%d",
            state_.menuSettings.invisibility ? 1 : 0, hasFlowConsumers ? 1 : 0, shouldHaveFlowActive ? 1 : 0);
        state_.world.navigationCache.playerFlowField.SetCacheActive(shouldHaveFlowActive);
        state_.world.gameplayEvents.Push(GameplayEvent{
            .type = GameplayEventType::StartModeStarted,
            .position = state_.world.player.position,
            .startModeReason = StartModeReason::Respawn,
        });
    }

    // Evac objective handling (all bases destroyed).
    int aliveBases = 0;
    for (const EnemyBase& base : state_.world.enemyBases) {
        if (!base.destroyed) {
            ++aliveBases;
        }
    }
    if (allowLevelComplete && aliveBases == 0) {
        bolt::log::Debug("[FLOW] All bases destroyed, entering evac objective phase");
        StartEvacObjectivePhase();
        return;
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
