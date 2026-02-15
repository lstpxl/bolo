#include "app/GameApp.h"

#include "game/systems/CollisionSystem.h"
#include "game/systems/EnemySystem.h"
#include "game/systems/MazeSystem.h"
#include "game/systems/PlayerSystem.h"
#include "game/systems/ProjectileSystem.h"
#include "game/systems/SpawnerSystem.h"
#include <array>
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

bool TryLoadMenuClickSound(Sound& sound) {
    constexpr std::array<const char*, 5> relativePaths = {
        "resources/audio/keyboard-click.wav",
        "../resources/audio/keyboard-click.wav",
        "../../resources/audio/keyboard-click.wav",
        "../../../resources/audio/keyboard-click.wav",
        "../../../../resources/audio/keyboard-click.wav",
    };

    for (const char* path : relativePaths) {
        if (TryLoadSoundAtPath(sound, path)) {
            return true;
        }
    }

    const char* applicationDirectory = GetApplicationDirectory();
    if (applicationDirectory != nullptr && applicationDirectory[0] != '\0') {
        std::filesystem::path base(applicationDirectory);
        for (int level = 0; level <= 4; ++level) {
            const std::filesystem::path candidate = base / "resources" / "audio" / "keyboard-click.wav";
            if (TryLoadSoundAtPath(sound, candidate.string())) {
                return true;
            }
            if (!base.has_parent_path()) {
                break;
            }
            base = base.parent_path();
        }
    }

    TraceLog(LOG_WARNING, "AUDIO: Menu click sound failed to load from known paths");
    return false;
}
}  // namespace

int GameApp::Run() {
    InitWindow(config_.screenWidth, config_.screenHeight, config_.windowTitle.data());
    SetTargetFPS(config_.targetFps);
    ConfigureRayguiDefaultStyle();
    InitAudioDevice();
    audioReady_ = IsAudioDeviceReady();
    if (audioReady_) {
        menuClickSoundLoaded_ = TryLoadMenuClickSound(menuClickSound_);
    }

    while (!exitRequested_ && !WindowShouldClose()) {
        const FrameInput input = PollFrameInput();
        if (input.quitRequested) {
            exitRequested_ = true;
            continue;
        }
        fixedStepTimer_.Accumulate(GetFrameTime());

        while (fixedStepTimer_.ShouldStep()) {
            if (modeController_.Mode() == GameMode::Playing) {
                UpdatePlaying(input, fixedStepTimer_.StepSeconds());
            }
            fixedStepTimer_.ConsumeStep();
        }

        Render(input);
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

void GameApp::UpdatePlaying(const FrameInput& input, float deltaSeconds) {
    UpdatePlayerSystem(state_, input, deltaSeconds);
    UpdateEnemySystem(state_, deltaSeconds);
    UpdateProjectileSystem(state_, deltaSeconds);
    UpdateSpawnerSystem(state_, deltaSeconds);
    UpdateMazeSystem(state_, deltaSeconds);
    UpdateCollisionSystem(state_, deltaSeconds);
}

void GameApp::Render(const FrameInput& input) {
    BeginDrawing();
    ClearBackground(BLACK);

    if (modeController_.Mode() == GameMode::Menu) {
        const MenuSettings previousSettings = state_.menuSettings;
        const MenuScreenResult result = menuScreen_.Render(state_.menuSettings, config_, input);
        state_.menuSettings = result.menuSettings;
        if (menuClickSoundLoaded_ &&
            (result.interactionOccurred ||
             result.startGameRequested ||
             result.quitRequested ||
             state_.menuSettings.difficulty != previousSettings.difficulty ||
             state_.menuSettings.mazeDensityPercent != previousSettings.mazeDensityPercent)) {
            PlaySound(menuClickSound_);
        }
        if (result.startGameRequested) {
            modeController_.StartGame(state_, state_.menuSettings);
        }
        if (result.quitRequested) {
            exitRequested_ = true;
        }
    } else {
        renderer_.DrawWorld(state_, config_);
        hudPanel_.Render(state_, config_);
    }

    EndDrawing();
}
