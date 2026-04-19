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
#if defined(__APPLE__)
    2;
#else
    1;
#endif

constexpr bool kMenuMusicEnabled = true;

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

bool TryLoadSoundPoolAtPath(SoundPool<4>& soundPool, const std::string& path) {
    return soundPool.Load(path);
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

bool TryLoadSoundPoolFromKnownPaths(SoundPool<4>& soundPool, const char* fileName, const char* logName) {
    const std::string path = core::resources::ResolveResourcePath("audio", fileName);
    if (!path.empty() && TryLoadSoundPoolAtPath(soundPool, path)) {
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
    menuBackgroundSimulation_.Initialize();
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
        menuClickSoundLoaded_ = TryLoadSoundFromKnownPaths(menuClickSound_, "ui-pass.wav", "menu click sound");
        menuSelectOnButtonSoundLoaded_ = TryLoadSoundFromKnownPaths(
            menuSelectOnButtonSound_, "positive-select.wav", "menu select-on-button sound");
        powerUpSoundLoaded_ =
            TryLoadSoundPoolFromKnownPaths(powerUpSound_, "power-up-digital.wav", "power-up (start mode) sound");
        playerShotSoundLoaded_ = TryLoadSoundPoolFromKnownPaths(playerShotSound_, "player-shot.wav", "player-shot sound");
        enemyShotSoundLoaded_ = TryLoadSoundPoolFromKnownPaths(enemyShotSound_, "enemy-shot.wav", "enemy-shot sound");
        enemySpawningSoundLoaded_ =
            TryLoadSoundPoolFromKnownPaths(enemySpawningSound_, "enemy-spawning.wav", "enemy-spawning sound");
        enemyExplodingSoundLoaded_ =
            TryLoadSoundPoolFromKnownPaths(enemyExplodingSound_, "match-fire.wav", "enemy death sound");
        baseExplodingSoundLoaded_ =
            TryLoadSoundPoolFromKnownPaths(baseExplodingSound_, "base-exploding.wav", "base-exploding sound");
        projectileWallHitSoundLoaded_ =
            TryLoadSoundPoolFromKnownPaths(projectileWallHitSound_, "kick-hit-1.wav", "projectile wall-hit sound");
        playerExplosionSoundLoaded_ =
            TryLoadSoundPoolFromKnownPaths(playerExplosionSound_, "player-explosion.wav", "player explosion sound");
        levelEvacCompleteSoundLoaded_ =
            TryLoadSoundFromKnownPaths(levelEvacCompleteSound_, "braams-2.wav", "level evac complete sound");
        if (kMenuMusicEnabled) {
            menuMusicPlayer_.SetLabel("menu");
            menuMusicPlayerReady_ = menuMusicPlayer_.Initialize(menuMelody_);
            if (!menuMusicPlayerReady_) {
                bolt::log::Warning("AUDIO: menu music player failed to initialize");
            }
            gameplayMusicPlayer_.SetLabel("gameplay");
            gameplayMusicPlayerReady_ = gameplayMusicPlayer_.Initialize(gameplayMelody_);
            if (!gameplayMusicPlayerReady_) {
                bolt::log::Warning("AUDIO: gameplay music player failed to initialize");
            }
        }
    }

    GameMode previousMode = game_.Mode();
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
                    input = PollFrameInput(inputPollState_);
                }
                if (input.quitRequested) {
                    exitRequested_ = true;
                } else {
                    fixedStepTimer_.Accumulate(GetFrameTime());

                    int fixedStepsThisFrame = 0;
                    while (fixedStepTimer_.ShouldStep() && fixedStepsThisFrame < kMaxFixedStepsPerFrame) {
                        profiling::ScopedProfile fixedStepScope(profiling::Scope::FixedStepUpdate, true);
                        if (game_.Mode() == GameMode::Playing) {
                            const GameplayPhase gameplayPhase = game_.State().gameplayPhase;
                            const bool canOpenGameplayPauseDialog =
                                gameplayPhase == GameplayPhase::Active ||
                                gameplayPhase == GameplayPhase::EvacObjective ||
                                gameplayPhase == GameplayPhase::GameOver ||
                                gameplayPhase == GameplayPhase::Victory;
                            if (canOpenGameplayPauseDialog && input.gameplayPausePressed && !gameplayPauseDialogOpen_) {
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
                                            .projectileWallHitLoaded = projectileWallHitSoundLoaded_,
                                            .playerExplosionLoaded = playerExplosionSoundLoaded_,
                                            .levelEvacCompleteLoaded = levelEvacCompleteSoundLoaded_,
                                            .powerUpSound = &powerUpSound_,
                                            .playerShotSound = &playerShotSound_,
                                            .enemyShotSound = &enemyShotSound_,
                                            .enemySpawningSound = &enemySpawningSound_,
                                            .enemyExplodingSound = &enemyExplodingSound_,
                                            .baseExplodingSound = &baseExplodingSound_,
                                            .projectileWallHitSound = &projectileWallHitSound_,
                                            .playerExplosionSound = &playerExplosionSound_,
                                            .levelEvacCompleteSound = &levelEvacCompleteSound_,
                                        });
                                }
                            }
                        } else if (game_.Mode() == GameMode::Menu) {
                            if (!menuBackgroundSimulation_.IsInitialized()) {
                                menuBackgroundSimulation_.Initialize();
                            }
                            menuBackgroundSimulation_.Update(fixedStepTimer_.StepSeconds());
                        }
                        fixedStepTimer_.ConsumeStep();
                        ++fixedStepsThisFrame;
                    }

                    FrameInput renderInput = input;
                    const GameMode modeBeforeRender = game_.Mode();
                    if (previousMode == GameMode::Playing && modeBeforeRender == GameMode::Menu) {
                        suppressMenuInteractionUntilRelease_ = true;
                        gameplayPauseDialogOpen_ = false;
                        renderer_.ResetTransientState();
                        menuBackgroundSimulation_.Initialize();
                    }
                    if (modeBeforeRender == GameMode::Menu && suppressMenuInteractionUntilRelease_) {
                        if (!input.anyInteractionDown) {
                            suppressMenuInteractionUntilRelease_ = false;
                        } else {
                            renderInput.shootPressed = false;
                            renderInput.startPressed = false;
                            renderInput.gameplayPausePressed = false;
                            renderInput.menuNavigateUpPressed = false;
                            renderInput.menuNavigateDownPressed = false;
                            renderInput.menuNavigateLeftPressed = false;
                            renderInput.menuNavigateRightPressed = false;
                            renderInput.menuSelectPressed = false;
                        }
                    }
                    {
                        profiling::ScopedProfile renderScope(profiling::Scope::FrameRender);
                        frameHasBackbufferWork = Render(renderInput);
                    }
                    if (audioReady_) {
                        if (menuMusicPlayerReady_) {
                            menuMusicPlayer_.SetEnabled(game_.Mode() == GameMode::Menu);
                            menuMusicPlayer_.Update();
                        }
                        if (gameplayMusicPlayerReady_) {
                            gameplayMusicPlayer_.SetEnabled(game_.Mode() == GameMode::Playing);
                            gameplayMusicPlayer_.SetTense(game_.IsGameplayMusicTense());
                            profiling::ScopedProfile gameplayMusicScope(profiling::Scope::GameplayMusicUpdate);
                            gameplayMusicPlayer_.Update();
                        }
                    }
                }
            }
            previousMode = game_.Mode();

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
    if (menuSelectOnButtonSoundLoaded_) {
        UnloadSound(menuSelectOnButtonSound_);
    }
    if (powerUpSoundLoaded_) {
        powerUpSound_.Unload();
    }
    if (playerShotSoundLoaded_) {
        playerShotSound_.Unload();
    }
    if (enemyShotSoundLoaded_) {
        enemyShotSound_.Unload();
    }
    if (enemySpawningSoundLoaded_) {
        enemySpawningSound_.Unload();
    }
    if (enemyExplodingSoundLoaded_) {
        enemyExplodingSound_.Unload();
    }
    if (baseExplodingSoundLoaded_) {
        baseExplodingSound_.Unload();
    }
    if (projectileWallHitSoundLoaded_) {
        projectileWallHitSound_.Unload();
    }
    if (playerExplosionSoundLoaded_) {
        playerExplosionSound_.Unload();
    }
    if (levelEvacCompleteSoundLoaded_) {
        UnloadSound(levelEvacCompleteSound_);
    }
    if (presentationTargetLoaded_) {
        UnloadRenderTexture(presentationTarget_);
        presentationTargetLoaded_ = false;
    }
    debugOverlayRenderer_.ReleaseResources();
    menuScreen_.UnloadResources();
    renderer_.UnloadResources();
    if (audioReady_) {
        if (menuMusicPlayerReady_) {
            menuMusicPlayer_.Shutdown();
            menuMusicPlayerReady_ = false;
        }
        if (gameplayMusicPlayerReady_) {
            gameplayMusicPlayer_.Shutdown();
            gameplayMusicPlayerReady_ = false;
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

    const float dialogWidth = std::min(400.0F, static_cast<float>(config_.screenWidth) - 48.0F);
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
            .confirmButtonLabel = "Quit",
            .cancelButtonLabel = "Resume",
            .buttonTextSize = 20,
        },
        input);

    if (menuSelectOnButtonSoundLoaded_ && dialogResult.buttonActivatedViaMenuSelect) {
        PlaySound(menuSelectOnButtonSound_);
    }
    if (menuClickSoundLoaded_ && dialogResult.interactionOccurred && !dialogResult.buttonActivatedViaMenuSelect) {
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
    if (game_.Mode() == GameMode::Playing &&
        game_.State().gameplayPhase != GameplayPhase::Starting) {
        renderer_.PrepareGameplayRender(game_.State(), config_, input);
    }

    const auto drawLogicalFrame = [&]() {
        ClearBackground(BLACK);

        if (game_.Mode() == GameMode::Menu) {
            if (menuBackgroundSimulation_.IsInitialized()) {
                renderer_.RenderMenuBackground(menuBackgroundSimulation_, config_);
                DrawRectangle(0, 0, config_.screenWidth, config_.screenHeight, Color{0, 0, 0, 160});
            }
            const MenuSettings previousSettings = game_.CurrentMenuSettings();
            const MenuScreenResult result = menuScreen_.Render(game_.CurrentMenuSettings(), config_, input);
            game_.SetMenuSettings(result.menuSettings);
            if (menuSelectOnButtonSoundLoaded_ && result.menuButtonActivatedViaMenuSelect) {
                PlaySound(menuSelectOnButtonSound_);
            }
            if (menuClickSoundLoaded_ &&
                ((result.interactionOccurred && !result.menuButtonActivatedViaMenuSelect) ||
                 (result.startGameRequested && !result.menuButtonActivatedViaMenuSelect) ||
                 (result.quitRequested && !result.menuButtonActivatedViaMenuSelect) ||
                 result.menuSettings.levelNumber != previousSettings.levelNumber ||
                 result.menuSettings.mazeDensity != previousSettings.mazeDensity ||
                 result.menuSettings.invisibility != previousSettings.invisibility ||
                 result.menuSettings.debugInfo != previousSettings.debugInfo)) {
                PlaySound(menuClickSound_);
            }
            if (result.startGameRequested) {
                gameplayPauseDialogOpen_ = false;
                inputPollState_ = {};
                menuBackgroundSimulation_.Reset();
                game_.StartGame(config_, BuildGameplayView(config_));
            }
            if (result.quitRequested) {
                exitRequested_ = true;
            }
        } else {
            const GameState& state = game_.State();
            const int worldWidth = config_.screenWidth - ComputeHudWidth(config_);

            if (state.gameplayPhase == GameplayPhase::Starting) {
                // Starting phase intentionally renders no world/HUD.
                const int overlayFontSize = 40;
                const char* overlayText = "STARTING...";
                const int textWidth = MeasureText(overlayText, overlayFontSize);
                const int textX = (config_.screenWidth - textWidth) / 2;
                const int textY = (config_.screenHeight / 2) - (overlayFontSize / 2);
                DrawText(overlayText, std::max(0, textX), textY, overlayFontSize, YELLOW);
            } else {
                game_.Render(renderer_, config_, input);
                if (state.gameplayPhase == GameplayPhase::EvacObjective ||
                    state.gameplayPhase == GameplayPhase::GameOver ||
                    state.gameplayPhase == GameplayPhase::Victory) {
                    const int overlayFontSize = 40;
                    if (state.gameplayPhase == GameplayPhase::EvacObjective) {
                        const char* line1 = "Proceed";
                        const char* line2 = "to the evac zone";
                        const int lineGap = 4;
                        const int blockHeight = overlayFontSize * 2 + lineGap;
                        const int startY = (config_.screenHeight - blockHeight) / 2;
                        const int w1 = MeasureText(line1, overlayFontSize);
                        const int w2 = MeasureText(line2, overlayFontSize);
                        const int x1 = (worldWidth - w1) / 2;
                        const int x2 = (worldWidth - w2) / 2;
                        DrawText(line1, std::max(0, x1), startY, overlayFontSize, YELLOW);
                        DrawText(line2, std::max(0, x2), startY + overlayFontSize + lineGap,
                                 overlayFontSize, YELLOW);
                    } else {
                        const char* overlayText =
                            state.gameplayPhase == GameplayPhase::Victory ? "Congratulations" : "GAME OVER";
                        const int textWidth = MeasureText(overlayText, overlayFontSize);
                        const int textX = (worldWidth - textWidth) / 2;
                        const int textY = (config_.screenHeight / 2) - (overlayFontSize / 2);
                        DrawText(overlayText, std::max(0, textX), textY, overlayFontSize, YELLOW);
                    }
                }
            }
            if (state.gameplayPhase != GameplayPhase::Starting && game_.State().menuSettings.debugInfo) {
                profiling::ScopedProfile overlayScope(profiling::Scope::RenderOverlay);
                debugOverlayRenderer_.Draw(game_.State(), config_, input);
            }
            if (state.gameplayPhase != GameplayPhase::Starting && gameplayPauseDialogOpen_) {
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
