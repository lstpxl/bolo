#pragma once

#include "app/AppConfig.h"
#include "core/Types.h"
#include "platform/Input.h"
#include "raylib.h"
#include "ui/ConfirmationDialog.h"

struct MenuScreenResult {
    bool startGameRequested;
    bool quitRequested;
    bool interactionOccurred;
    MenuSettings menuSettings;
};

class MenuScreen {
public:
    enum class FocusedControl {
        Start = 0,
        Level = 1,
        Density = 2,
        DebugInfo = 3,
        Quit = 4,
    };

    MenuScreenResult Render(
        const MenuSettings& currentSettings,
        const AppConfig& config,
        const FrameInput& input);
    bool LoadResources();
    void UnloadResources();

private:
    Font titleFont_{};
    bool titleFontLoaded_ = false;
    Font betaFont_{};
    bool betaFontLoaded_ = false;
    Texture2D densityHatchTexture_{};
    bool densityHatchLoaded_ = false;
    int levelNumber_ = kDefaultGameLevelNumber;
    int mazeDensity_ = 1;
    bool debugInfo_ = false;
    FocusedControl focusedControl_ = FocusedControl::Start;
    bool quitConfirmationOpen_ = false;
    ConfirmationDialog quitConfirmationDialog_{};
    /// Wall-clock time (`GetTime()`) until which density hatch sprites stay visible; `0` = hidden.
    double densitySpritesRevealUntilTime_ = 0.0;
    /// Wall-clock time until the level slider stays visible instead of the text row; `0` = text row.
    double levelSliderRevealUntilTime_ = 0.0;
};
