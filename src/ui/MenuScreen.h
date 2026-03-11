#pragma once

#include "app/AppConfig.h"
#include "core/Types.h"
#include "platform/Input.h"
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

private:
    int levelNumber_ = 4;
    int mazeDensity_ = 1;
    bool debugInfo_ = false;
    FocusedControl focusedControl_ = FocusedControl::Start;
    bool quitConfirmationOpen_ = false;
    ConfirmationDialog quitConfirmationDialog_{};
};
