#pragma once

#include "app/AppConfig.h"
#include "core/Types.h"
#include "platform/Input.h"

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
    enum class QuitDialogFocus {
        Quit = 0,
        Cancel = 1,
    };

    int levelNumber_ = 1;
    int mazeDensity_ = 3;
    FocusedControl focusedControl_ = FocusedControl::Level;
    bool quitConfirmationOpen_ = false;
    QuitDialogFocus quitDialogFocus_ = QuitDialogFocus::Cancel;
};
