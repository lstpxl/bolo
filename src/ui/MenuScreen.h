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
        Start = 2,
        Quit = 3,
    };

    MenuScreenResult Render(
        const MenuSettings& currentSettings,
        const AppConfig& config,
        const FrameInput& input);

private:
    int levelNumber_ = 1;
    int mazeDensity_ = 3;
    FocusedControl focusedControl_ = FocusedControl::Start;
    bool quitConfirmationOpen_ = false;
    ConfirmationDialog quitConfirmationDialog_{};
};
