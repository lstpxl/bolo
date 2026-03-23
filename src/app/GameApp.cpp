#include "app/GameApp.h"

#include <algorithm>
#include <string>
#include "core/Log.h"
#include "core/Math.h"
#include "core/Profiling.h"
#include "core/ResourceLocator.h"
#include "raylib.h"
#include "ui/UiPrimitives.h"
#include "ui/RayguiContext.h"

namespace {
constexpr int kPresentationScale =
#if defined(__APPLE__) && !defined(NDEBUG)
    1;
#elif defined(__APPLE__)
    2;
#else
    1;
#endif

bool TryLoadSoundAtPath(Sound& sound, const std::string& path) {
    if (!FileExists(path.c_str())) {
        return false;
    }

    sound = LoadSound(path.c_str());
    if (sound.frameCount <= 0) {
        return false;
    }

    return true;
}

bool TryLoadSoundFromKnownPaths(Sound& sound, const char* fileName, const char* logName) {
    const std::string path = core::resources::ResolveResourcePath("audio", fileName);
    if (!path.empty() && TryLoadSoundAtPath(sound, path)) {
        bolt::log::Info("AUDIO: %s loaded from: %s", logName, path.c_str());
        return true;
    }

    bolt::log::Warning("AUDIO: %s failed to load from known paths", logName);
    return false;
}

GameplayView BuildGameplayView(const AppConfig& config) {
    return GameplayView{
        .viewportWidthUnits =
            static_cast<float>(config.screenWidth - ComputeHudWidth(config)) /
            static_cast<float>(GameplayConstants::kPixelsPerUnit),
        .viewportHeightUnits =
            static_cast<float>(config.screenHeight) /
            static_cast<float>(GameplayConstants::kPixelsPerUnit),
    };
}

}  // namespace

int GameApp::Run() {
    core::math::InitializeLookupTables();

    bolt::log::Init();
    InitWindow(
        config_.screenWidth * kPresentationScale,
        config_.screenHeight * kPresentationScale,
        config_.windowTitle.data());
    SetExitKey(KEY_NULL);
    SetTargetFPS(config_.targetFps);
    ConfigureRayguiDefaultStyle();
    if (!renderer_.LoadResources()) {
        bolt::log::Warning("RENDER: Failed to load one or more renderer resources");
    }
    if (!menuScreen_.LoadResources()) {
        bolt::log::Warning("MENU: Failed to load one or more menu resources");
    }
    int activePresentationScale = 1;
    if (kPresentationScale > 1) {
        presentationTarget_ = LoadRenderTexture(config_.screenWidth, config_.screenHeight);
        presentationTargetLoaded_ = presentationTarget_.id != 0;
        if (presentationTargetLoaded_) {
            SetTextureFilter(presentationTarget_.texture, TEXTURE_FILTER_POINT);
            activePresentationScale = kPresentationScale;
        } else {
            bolt::log::Warning(
                "RENDER: Failed to create presentation render target, falling back to 1x presentation");
            SetWindowSize(config_.screenWidth, config_.screenHeight);
        }
    }
    SetMouseOffset(0, 0);
    SetMouseScale(
        1.0F / static_cast<float>(activePresentationScale),
        1.0F / static_cast<float>(activePresentationScale));
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
        menuMusicGeneratorReady_ = menuMusicGenerator_.Initialize();
        if (!menuMusicGeneratorReady_) {
            bolt::log::Warning("AUDIO: menu music generator failed to initialize");
        }
    }

    while (!exitRequested_ && !WindowShouldClose()) {
        profiling::Profiler::Instance().BeginFrame();
        {
            profiling::ScopedProfile frameScope(profiling::Scope::FrameTotal, true);
            bool frameHasBackbufferWork = false;
            FrameInput input{};
            {
                profiling::ScopedProfile cpuWorkScope(profiling::Scope::FrameCpuWork, true);
                {
                    profiling::ScopedProfile inputScope(profiling::Scope::FrameInputPoll);
                    input = PollFrameInput();
                }
                if (input.quitRequested) {
                    exitRequested_ = true;
                } else {
                    fixedStepTimer_.Accumulate(GetFrameTime());

                    int fixedStepsThisFrame = 0;
                    constexpr int kMaxFixedStepsPerFrame = 4;
                    while (fixedStepTimer_.ShouldStep() && fixedStepsThisFrame < kMaxFixedStepsPerFrame) {
                        profiling::ScopedProfile fixedStepScope(profiling::Scope::FixedStepUpdate, true);
                        if (game_.Mode() == GameMode::Playing) {
                            if (input.gameplayPausePressed && !gameplayPauseDialogOpen_) {
                                gameplayPauseDialogOpen_ = true;
                                gameplayPauseDialog_.Open(ConfirmationDialog::Focus::Cancel);
                            }
                            if (!gameplayPauseDialogOpen_) {
                                const float stepSeconds = fixedStepTimer_.StepSeconds();
                                game_.Update(input, stepSeconds, BuildGameplayView(config_));
                                GameState& afterUpdate = game_.MutableState();
                                {
                                    profiling::ScopedProfile audioRouteScope(profiling::Scope::AudioRouteStep);
                                    audioEventRouter_.RouteStep(
                                        afterUpdate.world.player.position,
                                        afterUpdate.world.gameplayEvents,
                                        AudioEventRouterConfig{
                                            .audioReady = audioReady_,
                                            .powerUpLoaded = powerUpSoundLoaded_,
                                            .playerShotLoaded = playerShotSoundLoaded_,
                                            .enemyShotLoaded = enemyShotSoundLoaded_,
                                            .enemySpawningLoaded = enemySpawningSoundLoaded_,
                                            .enemyExplodingLoaded = enemyExplodingSoundLoaded_,
                                            .baseExplodingLoaded = baseExplodingSoundLoaded_,
                                            .powerUpSound = &powerUpSound_,
                                            .playerShotSound = &playerShotSound_,
                                            .enemyShotSound = &enemyShotSound_,
                                            .enemySpawningSound = &enemySpawningSound_,
                                            .enemyExplodingSound = &enemyExplodingSound_,
                                            .baseExplodingSound = &baseExplodingSound_,
                                        });
                                }
                            }
                        }
                        fixedStepTimer_.ConsumeStep();
                        ++fixedStepsThisFrame;
                    }

                    {
                        profiling::ScopedProfile renderScope(profiling::Scope::FrameRender);
                        frameHasBackbufferWork = Render(input);
                    }
                    if (audioReady_ && menuMusicGeneratorReady_) {
                        menuMusicGenerator_.SetEnabled(game_.Mode() == GameMode::Menu);
                        menuMusicGenerator_.Update();
                    }
                }
            }
            if (frameHasBackbufferWork) {
                profiling::ScopedProfile presentScope(profiling::Scope::FramePresent);
                EndDrawing();
            }
        }
        profiling::Profiler::Instance().EndFrame();
        if (profiling::Profiler::Instance().ShouldEmitPeriodicReport()) {
            profiling::Profiler::Instance().EmitPeriodicReport(fixedStepTimer_.StepSeconds());
        }
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
    if (presentationTargetLoaded_) {
        UnloadRenderTexture(presentationTarget_);
        presentationTargetLoaded_ = false;
    }
    debugOverlayRenderer_.ReleaseResources();
    menuScreen_.UnloadResources();
    renderer_.UnloadResources();
    if (audioReady_) {
        if (menuMusicGeneratorReady_) {
            menuMusicGenerator_.Shutdown();
            menuMusicGeneratorReady_ = false;
        }
        CloseAudioDevice();
    }
    bolt::log::PrepareRaylibShutdown();
    CloseWindow();
    bolt::log::Shutdown();
    return 0;
}

void GameApp::RenderGameplayPauseDialog(const FrameInput& input) {
    ui::primitives::DrawModalBackdrop(config_.screenWidth, config_.screenHeight);

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

bool GameApp::Render(const FrameInput& input) {
    if (game_.Mode() == GameMode::Playing) {
        renderer_.PrepareGameplayRender(game_.State(), config_, input);
    }

    const auto drawLogicalFrame = [&]() {
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
                 result.menuSettings.invisibility != previousSettings.invisibility ||
                 result.menuSettings.debugInfo != previousSettings.debugInfo)) {
                PlaySound(menuClickSound_);
            }
            if (result.startGameRequested) {
                gameplayPauseDialogOpen_ = false;
                game_.StartGame(config_, BuildGameplayView(config_));
            }
            if (result.quitRequested) {
                exitRequested_ = true;
            }
        } else {
            game_.Render(renderer_, config_, input);
            if (game_.State().menuSettings.debugInfo) {
                profiling::ScopedProfile overlayScope(profiling::Scope::RenderOverlay);
                debugOverlayRenderer_.Draw(game_.State(), config_, input);
            }
            if (gameplayPauseDialogOpen_) {
                profiling::ScopedProfile overlayScope(profiling::Scope::RenderOverlay);
                RenderGameplayPauseDialog(input);
            }
        }
    };

    if (kPresentationScale > 1 && presentationTargetLoaded_) {
        BeginTextureMode(presentationTarget_);
        drawLogicalFrame();
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);
        const Rectangle source{
            .x = 0.0F,
            .y = 0.0F,
            .width = static_cast<float>(config_.screenWidth),
            .height = -static_cast<float>(config_.screenHeight),
        };
        const Rectangle dest{
            .x = 0.0F,
            .y = 0.0F,
            .width = static_cast<float>(config_.screenWidth * kPresentationScale),
            .height = static_cast<float>(config_.screenHeight * kPresentationScale),
        };
        DrawTexturePro(presentationTarget_.texture, source, dest, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
        return true;
    }

    BeginDrawing();
    drawLogicalFrame();
    return true;
}
