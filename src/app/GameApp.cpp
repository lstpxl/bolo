#include "app/GameApp.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
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
                    game_.Update(input, fixedStepTimer_.StepSeconds(), config_);
                    const GameState& afterUpdate = game_.State();
                    const int playerProjectilesAfter = CountPlayerProjectiles(afterUpdate);
                    if (audioReady_ && playerShotSoundLoaded_ && playerProjectilesAfter > playerProjectilesBefore) {
                        PlaySound(playerShotSound_);
                    }
                    if (audioReady_ &&
                        powerUpSoundLoaded_ &&
                        beforeUpdate.world.startModeRemainingSeconds <= 0.0F &&
                        afterUpdate.world.startModeRemainingSeconds > 0.0F) {
                        PlaySound(powerUpSound_);
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
            previousPlayerProjectileCount_ = 0;
            previousStartModeRemainingSeconds_ = game_.State().world.startModeRemainingSeconds;
            if (audioReady_ && powerUpSoundLoaded_ && previousStartModeRemainingSeconds_ > 0.0F) {
                PlaySound(powerUpSound_);
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
