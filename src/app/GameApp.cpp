#include "app/GameApp.h"

#include "game/systems/CollisionSystem.h"
#include "game/systems/EnemySystem.h"
#include "game/systems/MazeSystem.h"
#include "game/systems/PlayerSystem.h"
#include "game/systems/ProjectileSystem.h"
#include "game/systems/SpawnerSystem.h"
#include <algorithm>
#include "raylib.h"
#include "ui/RayguiContext.h"

int GameApp::Run() {
    InitWindow(config_.screenWidth, config_.screenHeight, config_.windowTitle.data());
    SetTargetFPS(config_.targetFps);
    ConfigureRayguiDefaultStyle();
    InitAudioDevice();
    audioReady_ = IsAudioDeviceReady();
    if (audioReady_) {
        menuClickSound_ = LoadSound("resources/audio/keyboard-click.wav");
        menuClickSoundLoaded_ = menuClickSound_.frameCount > 0;
    }

    while (!exitRequested_ && !WindowShouldClose()) {
        const FrameInput input = PollFrameInput();
        if (input.quitRequested) {
            exitRequested_ = true;
            continue;
        }
        fixedStepTimer_.Accumulate(GetFrameTime());

        if (modeController_.Mode() == GameMode::Menu) {
            UpdateMenu(input);
        }

        while (fixedStepTimer_.ShouldStep()) {
            if (modeController_.Mode() == GameMode::Playing) {
                UpdatePlaying(input, fixedStepTimer_.StepSeconds());
            }
            fixedStepTimer_.ConsumeStep();
        }

        Render();
    }

    if (menuClickSoundLoaded_) {
        UnloadSound(menuClickSound_);
    }
    if (audioReady_) {
        CloseAudioDevice();
    }
    CloseWindow();
    return 0;
}

void GameApp::UpdateMenu(const FrameInput& input) {
    if (input.startPressed || input.shootPressed) {
        modeController_.StartGame(state_, state_.menuSettings);
        return;
    }

    MenuSettings nextSettings = state_.menuSettings;
    if (input.menuDifficultyUpPressed && nextSettings.difficulty != DifficultyLevel::Easy) {
        nextSettings.difficulty = static_cast<DifficultyLevel>(
            static_cast<int>(nextSettings.difficulty) - 1);
    }
    if (input.menuDifficultyDownPressed && nextSettings.difficulty != DifficultyLevel::Hard) {
        nextSettings.difficulty = static_cast<DifficultyLevel>(
            static_cast<int>(nextSettings.difficulty) + 1);
    }
    if (input.menuDensityDecreasePressed) {
        nextSettings.mazeDensityPercent =
            std::max(20, nextSettings.mazeDensityPercent - 5);
    }
    if (input.menuDensityIncreasePressed) {
        nextSettings.mazeDensityPercent =
            std::min(80, nextSettings.mazeDensityPercent + 5);
    }

    if (nextSettings.difficulty != state_.menuSettings.difficulty ||
        nextSettings.mazeDensityPercent != state_.menuSettings.mazeDensityPercent) {
        state_.menuSettings = nextSettings;
        if (menuClickSoundLoaded_) {
            PlaySound(menuClickSound_);
        }
    }
}

void GameApp::UpdatePlaying(const FrameInput& input, float deltaSeconds) {
    UpdatePlayerSystem(state_, input, deltaSeconds);
    UpdateEnemySystem(state_, deltaSeconds);
    UpdateProjectileSystem(state_, deltaSeconds);
    UpdateSpawnerSystem(state_, deltaSeconds);
    UpdateMazeSystem(state_, deltaSeconds);
    UpdateCollisionSystem(state_, deltaSeconds);
}

void GameApp::Render() {
    BeginDrawing();
    ClearBackground(BLACK);

    if (modeController_.Mode() == GameMode::Menu) {
        const DifficultyLevel previousDifficulty = state_.menuSettings.difficulty;
        const MenuScreenResult result = menuScreen_.Render(state_.menuSettings, config_);
        state_.menuSettings = result.menuSettings;
        if (menuClickSoundLoaded_ &&
            (result.startGameRequested || state_.menuSettings.difficulty != previousDifficulty)) {
            PlaySound(menuClickSound_);
        }
        if (result.startGameRequested) {
            modeController_.StartGame(state_, state_.menuSettings);
        }
    } else {
        renderer_.DrawWorld(state_, config_);
        hudPanel_.Render(state_, config_);
    }

    EndDrawing();
}
