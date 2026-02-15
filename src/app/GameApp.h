#pragma once

#include "app/AppConfig.h"
#include "core/Time.h"
#include "game/GameModeController.h"
#include "game/GameState.h"
#include "platform/Input.h"
#include "platform/Renderer2D.h"
#include "ui/HudPanel.h"
#include "ui/MenuScreen.h"

class GameApp {
public:
    int Run();

private:
    void UpdateMenu(const FrameInput& input);
    void UpdatePlaying(const FrameInput& input, float deltaSeconds);
    void Render();

    AppConfig config_ = MakeDefaultAppConfig();
    FixedStepTimer fixedStepTimer_{config_.fixedDeltaSeconds};
    GameModeController modeController_{};
    GameState state_{};
    MenuScreen menuScreen_{};
    HudPanel hudPanel_{};
    Renderer2D renderer_{};
};
