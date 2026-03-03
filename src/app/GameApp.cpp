#include "app/GameApp.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include "raylib.h"
#include "ui/RayguiContext.h"

namespace {
bool TryLoadSoundAtPath(Sound& sound, const std::string& path) {
    if (!FileExists(path.c_str())) {
        return false;
    }

    sound = LoadSound(path.c_str());
    if (sound.frameCount <= 0) {
        return false;
    }

    TraceLog(LOG_INFO, "AUDIO: Menu click sound loaded from: %s", path.c_str());
    return true;
}

bool TryLoadSoundFromKnownPaths(Sound& sound, const char* fileName, const char* logName) {
    const std::array<std::string, 5> relativePaths = {
        std::string("resources/audio/") + fileName,
        std::string("../resources/audio/") + fileName,
        std::string("../../resources/audio/") + fileName,
        std::string("../../../resources/audio/") + fileName,
        std::string("../../../../resources/audio/") + fileName,
    };

    for (const std::string& path : relativePaths) {
        if (TryLoadSoundAtPath(sound, path)) {
            TraceLog(LOG_INFO, "AUDIO: %s loaded from: %s", logName, path.c_str());
            return true;
        }
    }

    const char* applicationDirectory = GetApplicationDirectory();
    if (applicationDirectory != nullptr && applicationDirectory[0] != '\0') {
        std::filesystem::path base(applicationDirectory);
        for (int level = 0; level <= 4; ++level) {
            const std::filesystem::path candidate = base / "resources" / "audio" / fileName;
            if (TryLoadSoundAtPath(sound, candidate.string())) {
                TraceLog(LOG_INFO, "AUDIO: %s loaded from: %s", logName, candidate.string().c_str());
                return true;
            }
            if (!base.has_parent_path()) {
                break;
            }
            base = base.parent_path();
        }
    }

    TraceLog(LOG_WARNING, "AUDIO: %s failed to load from known paths", logName);
    return false;
}

int CountPlayerProjectiles(const GameState& state) {
    int count = 0;
    for (const Projectile& projectile : state.world.projectiles) {
        if (projectile.alive && projectile.owner == ProjectileOwner::Player) {
            ++count;
        }
    }
    return count;
}

int CountEnemyProjectiles(const GameState& state) {
    int count = 0;
    for (const Projectile& projectile : state.world.projectiles) {
        if (projectile.alive && projectile.owner == ProjectileOwner::Enemy) {
            ++count;
        }
    }
    return count;
}

int CountAliveEnemies(const GameState& state) {
    int count = 0;
    for (const EnemyTank& enemy : state.world.enemies) {
        if (enemy.alive) {
            ++count;
        }
    }
    return count;
}

int CountAliveBases(const GameState& state) {
    int count = 0;
    for (const EnemyBase& base : state.world.enemyBases) {
        if (!base.destroyed) {
            ++count;
        }
    }
    return count;
}

float Distance(const Vec2f& a, const Vec2f& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

float ComputeSpatialVolume(const Vec2f& listener, const Vec2f& source) {
    const float r1 = 3.0F * static_cast<float>(GameplayConstants::kMazeCellSizeUnits);
    const float r2 = 10.0F * static_cast<float>(GameplayConstants::kMazeCellSizeUnits);
    const float d = Distance(listener, source);
    if (d > r2) {
        return 0.0F;
    }
    if (d <= r1) {
        return 1.0F;
    }
    return 1.0F - (d - r1) / (r2 - r1);
}

void PlaySpatialSound(Sound& sound, const Vec2f& listener, const Vec2f& source) {
    const float volume = ComputeSpatialVolume(listener, source);
    if (volume <= 0.0F) {
        return;
    }
    SetSoundVolume(sound, volume);
    PlaySound(sound);
}

bool FindClosestFreshProjectilePosition(
    const GameState& state,
    ProjectileOwner owner,
    float stepSeconds,
    const Vec2f& listener,
    Vec2f& outPosition) {
    const float freshThreshold = GameplayConstants::kProjectileLifetimeSeconds - stepSeconds - 0.001F;
    bool found = false;
    float bestDistance = std::numeric_limits<float>::infinity();
    for (const Projectile& projectile : state.world.projectiles) {
        if (!projectile.alive || projectile.owner != owner) {
            continue;
        }
        if (projectile.remainingLifeSeconds < freshThreshold) {
            continue;
        }
        const float distance = Distance(listener, projectile.position);
        if (!found || distance < bestDistance) {
            found = true;
            bestDistance = distance;
            outPosition = projectile.position;
        }
    }
    return found;
}

bool FindClosestRemovedEnemyPosition(
    const GameState& before,
    const GameState& after,
    const Vec2f& listener,
    Vec2f& outPosition) {
    std::vector<Vec2f> afterAlivePositions{};
    afterAlivePositions.reserve(after.world.enemies.size());
    for (const EnemyTank& enemy : after.world.enemies) {
        if (enemy.alive) {
            afterAlivePositions.push_back(enemy.position);
        }
    }
    std::vector<bool> matched(afterAlivePositions.size(), false);
    std::vector<Vec2f> removedPositions{};
    constexpr float kMatchDistanceUnits = 1.5F;
    for (const EnemyTank& beforeEnemy : before.world.enemies) {
        if (!beforeEnemy.alive) {
            continue;
        }
        int bestIndex = -1;
        float bestDist = kMatchDistanceUnits;
        for (int i = 0; i < static_cast<int>(afterAlivePositions.size()); ++i) {
            if (matched[static_cast<std::size_t>(i)]) {
                continue;
            }
            const float dist = Distance(beforeEnemy.position, afterAlivePositions[static_cast<std::size_t>(i)]);
            if (dist <= bestDist) {
                bestDist = dist;
                bestIndex = i;
            }
        }
        if (bestIndex >= 0) {
            matched[static_cast<std::size_t>(bestIndex)] = true;
        } else {
            removedPositions.push_back(beforeEnemy.position);
        }
    }
    if (removedPositions.empty()) {
        return false;
    }
    float bestListenerDistance = std::numeric_limits<float>::infinity();
    for (const Vec2f& pos : removedPositions) {
        const float dist = Distance(listener, pos);
        if (dist < bestListenerDistance) {
            bestListenerDistance = dist;
            outPosition = pos;
        }
    }
    return true;
}

bool FindClosestSpawnedEnemyPosition(
    const GameState& before,
    const GameState& after,
    const Vec2f& listener,
    Vec2f& outPosition) {
    std::vector<Vec2f> beforeAlivePositions{};
    beforeAlivePositions.reserve(before.world.enemies.size());
    for (const EnemyTank& enemy : before.world.enemies) {
        if (enemy.alive) {
            beforeAlivePositions.push_back(enemy.position);
        }
    }
    std::vector<bool> matched(beforeAlivePositions.size(), false);
    std::vector<Vec2f> spawnedPositions{};
    constexpr float kMatchDistanceUnits = 1.5F;
    for (const EnemyTank& afterEnemy : after.world.enemies) {
        if (!afterEnemy.alive) {
            continue;
        }
        int bestIndex = -1;
        float bestDist = kMatchDistanceUnits;
        for (int i = 0; i < static_cast<int>(beforeAlivePositions.size()); ++i) {
            if (matched[static_cast<std::size_t>(i)]) {
                continue;
            }
            const float dist = Distance(afterEnemy.position, beforeAlivePositions[static_cast<std::size_t>(i)]);
            if (dist <= bestDist) {
                bestDist = dist;
                bestIndex = i;
            }
        }
        if (bestIndex >= 0) {
            matched[static_cast<std::size_t>(bestIndex)] = true;
        } else {
            spawnedPositions.push_back(afterEnemy.position);
        }
    }
    if (spawnedPositions.empty()) {
        return false;
    }
    float bestListenerDistance = std::numeric_limits<float>::infinity();
    for (const Vec2f& pos : spawnedPositions) {
        const float dist = Distance(listener, pos);
        if (dist < bestListenerDistance) {
            bestListenerDistance = dist;
            outPosition = pos;
        }
    }
    return true;
}

bool FindClosestDestroyedBasePosition(
    const GameState& before,
    const GameState& after,
    const Vec2f& listener,
    Vec2f& outPosition) {
    bool found = false;
    float bestListenerDistance = std::numeric_limits<float>::infinity();
    const int count = std::min(
        static_cast<int>(before.world.enemyBases.size()),
        static_cast<int>(after.world.enemyBases.size()));
    for (int i = 0; i < count; ++i) {
        const EnemyBase& beforeBase = before.world.enemyBases[static_cast<std::size_t>(i)];
        const EnemyBase& afterBase = after.world.enemyBases[static_cast<std::size_t>(i)];
        if (beforeBase.destroyed || !afterBase.destroyed) {
            continue;
        }
        const float dist = Distance(listener, afterBase.position);
        if (!found || dist < bestListenerDistance) {
            found = true;
            bestListenerDistance = dist;
            outPosition = afterBase.position;
        }
    }
    return found;
}
}  // namespace

int GameApp::Run() {
    InitWindow(config_.screenWidth, config_.screenHeight, config_.windowTitle.data());
    SetExitKey(KEY_NULL);
    SetTargetFPS(config_.targetFps);
    ConfigureRayguiDefaultStyle();
    if (!renderer_.LoadResources()) {
        TraceLog(LOG_WARNING, "RENDER: Failed to load one or more renderer resources");
    }
    InitAudioDevice();
    audioReady_ = IsAudioDeviceReady();
    if (audioReady_) {
        menuClickSoundLoaded_ = TryLoadSoundFromKnownPaths(menuClickSound_, "keyboard-click.wav", "menu click sound");
        powerUpSoundLoaded_ = TryLoadSoundFromKnownPaths(powerUpSound_, "power-up.wav", "power-up sound");
        playerShotSoundLoaded_ = TryLoadSoundFromKnownPaths(playerShotSound_, "player-shot.wav", "player-shot sound");
        enemyShotSoundLoaded_ = TryLoadSoundFromKnownPaths(enemyShotSound_, "enemy-shot.wav", "enemy-shot sound");
        enemySpawningSoundLoaded_ =
            TryLoadSoundFromKnownPaths(enemySpawningSound_, "enemy-spawning.wav", "enemy-spawning sound");
        enemyExplodingSoundLoaded_ =
            TryLoadSoundFromKnownPaths(enemyExplodingSound_, "enemy-exploding.wav", "enemy-exploding sound");
        baseExplodingSoundLoaded_ =
            TryLoadSoundFromKnownPaths(baseExplodingSound_, "base-exploding.wav", "base-exploding sound");
    }

    while (!exitRequested_ && !WindowShouldClose()) {
        const FrameInput input = PollFrameInput();
        if (input.quitRequested) {
            exitRequested_ = true;
            continue;
        }
        fixedStepTimer_.Accumulate(GetFrameTime());

        int fixedStepsThisFrame = 0;
        constexpr int kMaxFixedStepsPerFrame = 4;
        while (fixedStepTimer_.ShouldStep() && fixedStepsThisFrame < kMaxFixedStepsPerFrame) {
            if (game_.Mode() == GameMode::Playing) {
                if (input.gameplayPausePressed && !gameplayPauseDialogOpen_) {
                    gameplayPauseDialogOpen_ = true;
                    gameplayPauseDialog_.Open(ConfirmationDialog::Focus::Cancel);
                }
                if (!gameplayPauseDialogOpen_) {
                    const GameState beforeUpdate = game_.State();
                    const int playerProjectilesBefore = CountPlayerProjectiles(beforeUpdate);
                    const int enemyProjectilesBefore = CountEnemyProjectiles(beforeUpdate);
                    const int aliveEnemiesBefore = CountAliveEnemies(beforeUpdate);
                    const int aliveBasesBefore = CountAliveBases(beforeUpdate);
                    const float startModeBefore = beforeUpdate.world.startModeRemainingSeconds;
                    const float stepSeconds = fixedStepTimer_.StepSeconds();
                    game_.Update(input, stepSeconds, config_);
                    const GameState& afterUpdate = game_.State();
                    const int playerProjectilesAfter = CountPlayerProjectiles(afterUpdate);
                    const int enemyProjectilesAfter = CountEnemyProjectiles(afterUpdate);
                    const int aliveEnemiesAfter = CountAliveEnemies(afterUpdate);
                    const int aliveBasesAfter = CountAliveBases(afterUpdate);
                    const Vec2f listener = afterUpdate.world.player.position;
                    if (audioReady_ && playerShotSoundLoaded_ && playerProjectilesAfter > playerProjectilesBefore) {
                        Vec2f source = listener;
                        if (FindClosestFreshProjectilePosition(
                                afterUpdate,
                                ProjectileOwner::Player,
                                stepSeconds,
                                listener,
                                source)) {
                            PlaySpatialSound(playerShotSound_, listener, source);
                        } else {
                            PlaySpatialSound(playerShotSound_, listener, listener);
                        }
                    }
                    if (audioReady_ && enemyShotSoundLoaded_ && enemyProjectilesAfter > enemyProjectilesBefore) {
                        Vec2f source = listener;
                        if (FindClosestFreshProjectilePosition(
                                afterUpdate,
                                ProjectileOwner::Enemy,
                                stepSeconds,
                                listener,
                                source)) {
                            PlaySpatialSound(enemyShotSound_, listener, source);
                        }
                    }
                    if (audioReady_ && enemySpawningSoundLoaded_ && aliveEnemiesAfter > aliveEnemiesBefore) {
                        Vec2f source{};
                        if (FindClosestSpawnedEnemyPosition(beforeUpdate, afterUpdate, listener, source)) {
                            PlaySpatialSound(enemySpawningSound_, listener, source);
                        }
                    }
                    if (audioReady_ && enemyExplodingSoundLoaded_ && aliveEnemiesAfter < aliveEnemiesBefore) {
                        Vec2f source{};
                        if (FindClosestRemovedEnemyPosition(beforeUpdate, afterUpdate, listener, source)) {
                            PlaySpatialSound(enemyExplodingSound_, listener, source);
                        }
                    }
                    if (audioReady_ && baseExplodingSoundLoaded_ && aliveBasesAfter < aliveBasesBefore) {
                        Vec2f source{};
                        if (FindClosestDestroyedBasePosition(beforeUpdate, afterUpdate, listener, source)) {
                            PlaySpatialSound(baseExplodingSound_, listener, source);
                        }
                    }
                    if (audioReady_ &&
                        powerUpSoundLoaded_ &&
                        startModeBefore <= 0.0F &&
                        afterUpdate.world.startModeRemainingSeconds > 0.0F) {
                        PlaySpatialSound(powerUpSound_, listener, listener);
                    }
                }
            }
            fixedStepTimer_.ConsumeStep();
            ++fixedStepsThisFrame;
        }

        Render(input);
    }

    if (menuClickSoundLoaded_) {
        UnloadSound(menuClickSound_);
    }
    if (powerUpSoundLoaded_) {
        UnloadSound(powerUpSound_);
    }
    if (playerShotSoundLoaded_) {
        UnloadSound(playerShotSound_);
    }
    if (enemyShotSoundLoaded_) {
        UnloadSound(enemyShotSound_);
    }
    if (enemySpawningSoundLoaded_) {
        UnloadSound(enemySpawningSound_);
    }
    if (enemyExplodingSoundLoaded_) {
        UnloadSound(enemyExplodingSound_);
    }
    if (baseExplodingSoundLoaded_) {
        UnloadSound(baseExplodingSound_);
    }
    renderer_.UnloadResources();
    if (audioReady_) {
        CloseAudioDevice();
    }
    CloseWindow();
    return 0;
}

void GameApp::RenderGameplayPauseDialog(const FrameInput& input) {
    DrawRectangle(
        0,
        0,
        config_.screenWidth,
        config_.screenHeight,
        Fade(BLACK, 0.6F));

    const float dialogWidth = std::min(360.0F, static_cast<float>(config_.screenWidth) - 48.0F);
    const Rectangle dialog = {
        .x = (static_cast<float>(config_.screenWidth) - dialogWidth) * 0.5F,
        .y = static_cast<float>(config_.screenHeight) * 0.5F - 75.0F,
        .width = dialogWidth,
        .height = 150.0F,
    };

    const ConfirmationDialogResult dialogResult = gameplayPauseDialog_.Render(
        ConfirmationDialog::Spec{
            .bounds = dialog,
            .message = "Paused",
            .confirmButtonLabel = "Quit to menu",
            .cancelButtonLabel = "Resume",
        },
        input);

    if (menuClickSoundLoaded_ && dialogResult.interactionOccurred) {
        PlaySound(menuClickSound_);
    }

    if (dialogResult.cancelPressed) {
        gameplayPauseDialogOpen_ = false;
    } else if (dialogResult.confirmPressed) {
        gameplayPauseDialogOpen_ = false;
        game_.RequestMenu();
    }
}

void GameApp::Render(const FrameInput& input) {
    BeginDrawing();
    ClearBackground(BLACK);

    if (game_.Mode() == GameMode::Menu) {
        const MenuSettings previousSettings = game_.CurrentMenuSettings();
        const MenuScreenResult result = menuScreen_.Render(game_.CurrentMenuSettings(), config_, input);
        game_.SetMenuSettings(result.menuSettings);
        if (menuClickSoundLoaded_ &&
            (result.interactionOccurred ||
             result.startGameRequested ||
             result.quitRequested ||
             result.menuSettings.levelNumber != previousSettings.levelNumber ||
             result.menuSettings.mazeDensity != previousSettings.mazeDensity ||
             result.menuSettings.invisibility != previousSettings.invisibility)) {
            PlaySound(menuClickSound_);
        }
        if (result.startGameRequested) {
            gameplayPauseDialogOpen_ = false;
            game_.StartGame(config_);
            if (audioReady_ && powerUpSoundLoaded_ && game_.State().world.startModeRemainingSeconds > 0.0F) {
                const Vec2f listener = game_.State().world.player.position;
                PlaySpatialSound(powerUpSound_, listener, listener);
            }
        }
        if (result.quitRequested) {
            exitRequested_ = true;
        }
    } else {
        game_.Render(renderer_, config_, input);
        const GameState& state = game_.State();
        int aliveBases = 0;
        for (const EnemyBase& base : state.world.enemyBases) {
            if (!base.destroyed) {
                ++aliveBases;
            }
        }

        int dronesAlive = 0;
        int torpedoesAlive = 0;
        int huntersAlive = 0;
        int assassinsAlive = 0;
        for (const EnemyTank& enemy : state.world.enemies) {
            if (!enemy.alive) {
                continue;
            }
            if (enemy.type == EnemyType::Drone) {
                ++dronesAlive;
            } else if (enemy.type == EnemyType::Torpedo) {
                ++torpedoesAlive;
            } else if (enemy.type == EnemyType::Hunter) {
                ++huntersAlive;
            } else if (enemy.type == EnemyType::Assassin) {
                ++assassinsAlive;
            }
        }

        char axesText[96] = {};
        std::snprintf(
            axesText,
            sizeof(axesText),
            "Axes:  0:%6d  1:%6d  2:%6d  3:%6d",
            input.gamepadAxis0Raw,
            input.gamepadAxis1Raw,
            input.gamepadAxis2Raw,
            input.gamepadAxis3Raw);
        DrawText(axesText, 8, 8, 10, RAYWHITE);

        char countsText[96] = {};
        std::snprintf(
            countsText,
            sizeof(countsText),
            "B:%d D:%d T:%d H:%d A:%d",
            aliveBases,
            dronesAlive,
            torpedoesAlive,
            huntersAlive,
            assassinsAlive);
        DrawText(countsText, 8, config_.screenHeight - 18, 10, RAYWHITE);
        if (gameplayPauseDialogOpen_) {
            RenderGameplayPauseDialog(input);
        }
    }

    EndDrawing();
}
