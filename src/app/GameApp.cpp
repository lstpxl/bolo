#include "app/GameApp.h"

#include "game/systems/CollisionSystem.h"
#include "game/systems/EnemySystem.h"
#include "game/systems/MazeSystem.h"
#include "game/systems/PlayerSystem.h"
#include "game/systems/ProjectileSystem.h"
#include "game/systems/SpawnerSystem.h"
#include "raylib.h"
#include "ui/RayguiContext.h"

int GameApp::Run() {
    InitWindow(config_.screenWidth, config_.screenHeight, config_.windowTitle.data());
    SetTargetFPS(config_.targetFps);
    ConfigureRayguiDefaultStyle();

    while (!WindowShouldClose()) {
        const FrameInput input = PollFrameInput();
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

    CloseWindow();
    return 0;
}

void GameApp::UpdateMenu(const FrameInput& input) {
    if (input.quitRequested) {
        modeController_.RequestMenu();
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
        const MenuScreenResult result = menuScreen_.Render(state_.menuSettings, config_);
        state_.menuSettings = result.menuSettings;
        if (result.startGameRequested) {
            modeController_.StartGame(state_, state_.menuSettings);
        }
    } else {
        renderer_.DrawWorld(state_, config_);
        hudPanel_.Render(state_, config_);
    }

    EndDrawing();
}
