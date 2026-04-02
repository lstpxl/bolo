#pragma once

#include "app/AppConfig.h"
#include "app/AudioEventRouter.h"
#include "app/DebugOverlayRenderer.h"
#include "app/FuncbeatMelody.h"
#include "app/MenuMusicPlayer.h"
#include "app/ResonantBeatMelody.h"
#include "app/SoundPool.h"
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
    bool Render(const FrameInput& input);

    static constexpr int kMaxFixedStepsPerFrame = 4;

    AppConfig config_ = MakeDefaultAppConfig();
    InputPollState inputPollState_{};
    FixedStepTimer fixedStepTimer_{config_.fixedDeltaSeconds, kMaxFixedStepsPerFrame};
    Game game_{};
    MenuScreen menuScreen_{};
    RaylibRenderer renderer_{};
    Sound menuClickSound_{};
    Sound menuSelectOnButtonSound_{};
    SoundPool<4> powerUpSound_{};
    SoundPool<4> playerShotSound_{};
    SoundPool<4> enemyShotSound_{};
    SoundPool<4> enemySpawningSound_{};
    SoundPool<4> enemyExplodingSound_{};
    SoundPool<4> baseExplodingSound_{};
    SoundPool<4> projectileWallHitSound_{};
    SoundPool<4> playerExplosionSound_{};
    bool audioReady_ = false;
    bool menuClickSoundLoaded_ = false;
    bool menuSelectOnButtonSoundLoaded_ = false;
    bool powerUpSoundLoaded_ = false;
    bool playerShotSoundLoaded_ = false;
    bool enemyShotSoundLoaded_ = false;
    bool enemySpawningSoundLoaded_ = false;
    bool enemyExplodingSoundLoaded_ = false;
    bool baseExplodingSoundLoaded_ = false;
    bool projectileWallHitSoundLoaded_ = false;
    bool playerExplosionSoundLoaded_ = false;
    bool menuMusicPlayerReady_ = false;
    bool gameplayMusicPlayerReady_ = false;
    bool exitRequested_ = false;
    bool suppressMenuInteractionUntilRelease_ = false;
    bool gameplayPauseDialogOpen_ = false;
    ConfirmationDialog gameplayPauseDialog_{};
    AudioEventRouter audioEventRouter_{};
    FuncbeatMelody menuMelody_{};
    MenuMusicPlayer menuMusicPlayer_{};
    ResonantBeatMelody gameplayMelody_{};
    MenuMusicPlayer gameplayMusicPlayer_{};
    DebugOverlayRenderer debugOverlayRenderer_{};
    RenderTexture2D presentationTarget_{};
    bool presentationTargetLoaded_ = false;
};
