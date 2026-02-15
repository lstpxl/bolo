#pragma once

#include "app/AppConfig.h"
#include "core/Types.h"

struct MenuScreenResult {
    bool startGameRequested;
    MenuSettings menuSettings;
};

class MenuScreen {
public:
    MenuScreenResult Render(const MenuSettings& currentSettings, const AppConfig& config);

private:
    int selectedDifficulty_ = static_cast<int>(DifficultyLevel::Normal);
    int mazeDensityPercent_ = 45;
};
