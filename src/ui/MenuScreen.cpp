#include "ui/MenuScreen.h"

#include "raygui.h"
#include "raylib.h"

namespace {
DifficultyLevel ToDifficulty(int selectedDifficulty) {
    if (selectedDifficulty <= static_cast<int>(DifficultyLevel::Easy)) {
        return DifficultyLevel::Easy;
    }
    if (selectedDifficulty >= static_cast<int>(DifficultyLevel::Hard)) {
        return DifficultyLevel::Hard;
    }
    return DifficultyLevel::Normal;
}
}  // namespace

MenuScreenResult MenuScreen::Render(const MenuSettings& currentSettings, const AppConfig& config) {
    selectedDifficulty_ = static_cast<int>(currentSettings.difficulty);
    mazeDensityPercent_ = currentSettings.mazeDensityPercent;

    const Rectangle panel = {
        .x = static_cast<float>(config.screenWidth) * 0.5F - 220.0F,
        .y = 80.0F,
        .width = 440.0F,
        .height = 340.0F,
    };

    DrawRectangleRounded(panel, 0.15F, 8, Color{38, 45, 58, 240});
    DrawText("BOLO", static_cast<int>(panel.x) + 24, static_cast<int>(panel.y) + 20, 40, RAYWHITE);

    const int previousTextSize = GuiGetStyle(DEFAULT, TEXT_SIZE);
    GuiSetStyle(DEFAULT, TEXT_SIZE, 20);

    DrawText("Select difficulty and maze density", static_cast<int>(panel.x) + 24, static_cast<int>(panel.y) + 66, 20, LIGHTGRAY);

    bool easyActive = selectedDifficulty_ == static_cast<int>(DifficultyLevel::Easy);
    bool normalActive = selectedDifficulty_ == static_cast<int>(DifficultyLevel::Normal);
    bool hardActive = selectedDifficulty_ == static_cast<int>(DifficultyLevel::Hard);
    const bool wasEasyActive = easyActive;
    const bool wasNormalActive = normalActive;
    const bool wasHardActive = hardActive;

    GuiToggle(
        Rectangle{panel.x + 24.0F, panel.y + 108.0F, 180.0F, 30.0F},
        "Easy",
        &easyActive);
    GuiToggle(
        Rectangle{panel.x + 24.0F, panel.y + 146.0F, 180.0F, 30.0F},
        "Normal",
        &normalActive);
    GuiToggle(
        Rectangle{panel.x + 24.0F, panel.y + 184.0F, 180.0F, 30.0F},
        "Hard",
        &hardActive);

    if (!wasEasyActive && easyActive) {
        selectedDifficulty_ = static_cast<int>(DifficultyLevel::Easy);
    } else if (!wasNormalActive && normalActive) {
        selectedDifficulty_ = static_cast<int>(DifficultyLevel::Normal);
    } else if (!wasHardActive && hardActive) {
        selectedDifficulty_ = static_cast<int>(DifficultyLevel::Hard);
    }

    float densityValue = static_cast<float>(mazeDensityPercent_);
    DrawText("Maze Density", static_cast<int>(panel.x) + 24, static_cast<int>(panel.y) + 228, 20, LIGHTGRAY);
    GuiSliderBar(
        Rectangle{panel.x + 24.0F, panel.y + 252.0F, 320.0F, 28.0F},
        "",
        TextFormat("%d%%", mazeDensityPercent_),
        &densityValue,
        20.0F,
        80.0F);
    mazeDensityPercent_ = static_cast<int>(densityValue);

    const bool startPressed = GuiButton(
        Rectangle{panel.x + 24.0F, panel.y + 288.0F, 390.0F, 28.0F},
        "Start");

    GuiSetStyle(DEFAULT, TEXT_SIZE, previousTextSize);

    return MenuScreenResult{
        .startGameRequested = startPressed,
        .menuSettings =
            MenuSettings{
                .difficulty = ToDifficulty(selectedDifficulty_),
                .mazeDensityPercent = mazeDensityPercent_,
            },
    };
}
