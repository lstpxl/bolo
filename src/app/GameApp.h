#pragma once

#include "app/AppConfig.h"
#include "app/AudioEventRouter.h"
#include "app/DebugOverlayRenderer.h"
#include "core/Time.h"
#include "game/Game.h"
#include "platform/Input.h"
#include "platform/RaylibRenderer.h"
#include "raylib.h"
#include "ui/ConfirmationDialog.h"
#include "ui/MenuScreen.h"

class GameApp {
public:
    int Run();

private:
    void RenderGameplayPauseDialog(const FrameInput& input);
    void Render(const FrameInput& input);

    AppConfig config_ = MakeDefaultAppConfig();
    FixedStepTimer fixedStepTimer_{config_.fixedDeltaSeconds};
    Game game_{};
    MenuScreen menuScreen_{};
    RaylibRenderer renderer_{};
    Sound menuClickSound_{};
    Sound powerUpSound_{};
    Sound playerShotSound_{};
    Sound enemyShotSound_{};
    Sound enemySpawningSound_{};
    Sound enemyExplodingSound_{};
    Sound baseExplodingSound_{};
    bool audioReady_ = false;
    bool menuClickSoundLoaded_ = false;
    bool powerUpSoundLoaded_ = false;
    bool playerShotSoundLoaded_ = false;
    bool enemyShotSoundLoaded_ = false;
    bool enemySpawningSoundLoaded_ = false;
    bool enemyExplodingSoundLoaded_ = false;
    bool baseExplodingSoundLoaded_ = false;
    bool exitRequested_ = false;
    bool gameplayPauseDialogOpen_ = false;
    ConfirmationDialog gameplayPauseDialog_{};
    AudioEventRouter audioEventRouter_{};
    DebugOverlayRenderer debugOverlayRenderer_{};
    RenderTexture2D presentationTarget_{};
    bool presentationTargetLoaded_ = false;
};
