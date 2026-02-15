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
        Easy = 0,
        Normal = 1,
        Hard = 2,
        Density = 3,
        Start = 4,
        Quit = 5,
    };

    MenuScreenResult Render(
        const MenuSettings& currentSettings,
        const AppConfig& config,
        const FrameInput& input);

private:
    int selectedDifficulty_ = static_cast<int>(DifficultyLevel::Normal);
    int mazeDensityPercent_ = 45;
    FocusedControl focusedControl_ = FocusedControl::Easy;
};
