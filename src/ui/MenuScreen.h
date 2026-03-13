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
        Level = 0,
        Density = 1,
        DebugInfo = 2,
        Start = 3,
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
    int levelNumber_ = kDefaultGameLevelNumber;
    int mazeDensity_ = 1;
    bool debugInfo_ = false;
    FocusedControl focusedControl_ = FocusedControl::Start;
    bool quitConfirmationOpen_ = false;
    ConfirmationDialog quitConfirmationDialog_{};
};
