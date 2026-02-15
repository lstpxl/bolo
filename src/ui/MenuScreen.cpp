#include "ui/MenuScreen.h"

#include <algorithm>
#include "raygui.h"
#include "raylib.h"

namespace {
constexpr int kMinLevelNumber = 1;
constexpr int kMaxLevelNumber = 9;
constexpr int kMinMazeDensity = 1;
constexpr int kMaxMazeDensity = 5;

MenuScreen::FocusedControl NextFocusedControl(MenuScreen::FocusedControl current) {
    if (current == MenuScreen::FocusedControl::Quit) {
        return MenuScreen::FocusedControl::Level;
    }
    return static_cast<MenuScreen::FocusedControl>(static_cast<int>(current) + 1);
}

MenuScreen::FocusedControl PreviousFocusedControl(MenuScreen::FocusedControl current) {
    if (current == MenuScreen::FocusedControl::Level) {
        return MenuScreen::FocusedControl::Quit;
    }
    return static_cast<MenuScreen::FocusedControl>(static_cast<int>(current) - 1);
}

void DrawFocus(const Rectangle& bounds, bool isFocused) {
    if (!isFocused) {
        return;
    }
    DrawRectangleLinesEx(
        Rectangle{
            .x = bounds.x - 3.0F,
            .y = bounds.y - 3.0F,
            .width = bounds.width + 6.0F,
            .height = bounds.height + 6.0F,
        },
        3.0F,
        Color{255, 209, 102, 255});
}
}  // namespace

MenuScreenResult MenuScreen::Render(
    const MenuSettings& currentSettings,
    const AppConfig& config,
    const FrameInput& input) {
    levelNumber_ = std::clamp(currentSettings.levelNumber, kMinLevelNumber, kMaxLevelNumber);
    mazeDensity_ = std::clamp(currentSettings.mazeDensity, kMinMazeDensity, kMaxMazeDensity);
    bool interactionOccurred = false;

    if (!quitConfirmationOpen_) {
        if (input.menuNavigateDownPressed) {
            focusedControl_ = NextFocusedControl(focusedControl_);
            interactionOccurred = true;
        }
        if (input.menuNavigateUpPressed) {
            focusedControl_ = PreviousFocusedControl(focusedControl_);
            interactionOccurred = true;
        }
    }

    const Rectangle panel = {
        .x = static_cast<float>(config.screenWidth) * 0.5F - 220.0F,
        .y = static_cast<float>(config.screenHeight) * 0.5F - 190.0F,
        .width = 440.0F,
        .height = 380.0F,
    };
    const float panelCenterX = panel.x + panel.width * 0.5F;

    DrawRectangleRounded(panel, 0.15F, 8, Color{38, 45, 58, 240});
    const int titleFontSize = 40;
    DrawText(
        "BOLO",
        static_cast<int>(panelCenterX) - MeasureText("BOLO", titleFontSize) / 2,
        static_cast<int>(panel.y) + 20,
        titleFontSize,
        RAYWHITE);

    const int previousTextSize = GuiGetStyle(DEFAULT, TEXT_SIZE);
    GuiSetStyle(DEFAULT, TEXT_SIZE, 20);

    const char* subtitle = "Select level (1-9) and density (1-5)";
    DrawText(
        subtitle,
        static_cast<int>(panelCenterX) - MeasureText(subtitle, 20) / 2,
        static_cast<int>(panel.y) + 66,
        20,
        LIGHTGRAY);

    const Rectangle levelGauge = Rectangle{panelCenterX - 160.0F, panel.y + 138.0F, 320.0F, 28.0F};
    const Rectangle densityGauge = Rectangle{panelCenterX - 160.0F, panel.y + 228.0F, 320.0F, 28.0F};
    const Rectangle startButton = Rectangle{panelCenterX - 195.0F, panel.y + 292.0F, 390.0F, 30.0F};
    const Rectangle quitButton = Rectangle{panelCenterX - 195.0F, panel.y + 338.0F, 390.0F, 30.0F};

    float levelValue = static_cast<float>(levelNumber_);
    DrawText(
        "Level",
        static_cast<int>(panelCenterX) - MeasureText("Level", 20) / 2,
        static_cast<int>(panel.y) + 110,
        20,
        LIGHTGRAY);
    if (!quitConfirmationOpen_ && focusedControl_ == FocusedControl::Level) {
        if (input.menuNavigateLeftPressed) {
            levelValue = std::max(static_cast<float>(kMinLevelNumber), levelValue - 1.0F);
            interactionOccurred = true;
        }
        if (input.menuNavigateRightPressed) {
            levelValue = std::min(static_cast<float>(kMaxLevelNumber), levelValue + 1.0F);
            interactionOccurred = true;
        }
    }
    const int previousLevelNumber = levelNumber_;
    GuiSliderBar(
        levelGauge,
        "",
        TextFormat("%d", levelNumber_),
        &levelValue,
        static_cast<float>(kMinLevelNumber),
        static_cast<float>(kMaxLevelNumber));
    levelNumber_ = static_cast<int>(levelValue);
    if (levelNumber_ != previousLevelNumber) {
        interactionOccurred = true;
    }

    float densityValue = static_cast<float>(mazeDensity_);
    DrawText(
        "Density",
        static_cast<int>(panelCenterX) - MeasureText("Density", 20) / 2,
        static_cast<int>(panel.y) + 200,
        20,
        LIGHTGRAY);
    if (!quitConfirmationOpen_ && focusedControl_ == FocusedControl::Density) {
        if (input.menuNavigateLeftPressed) {
            densityValue = std::max(static_cast<float>(kMinMazeDensity), densityValue - 1.0F);
            interactionOccurred = true;
        }
        if (input.menuNavigateRightPressed) {
            densityValue = std::min(static_cast<float>(kMaxMazeDensity), densityValue + 1.0F);
            interactionOccurred = true;
        }
    }
    const int previousDensity = mazeDensity_;
    GuiSliderBar(
        densityGauge,
        "",
        TextFormat("%d", mazeDensity_),
        &densityValue,
        static_cast<float>(kMinMazeDensity),
        static_cast<float>(kMaxMazeDensity));
    mazeDensity_ = static_cast<int>(densityValue);
    if (mazeDensity_ != previousDensity) {
        interactionOccurred = true;
    }

    bool startPressed = GuiButton(startButton, "Start");
    bool quitPressed = GuiButton(quitButton, "Quit");
    if (quitConfirmationOpen_) {
        startPressed = false;
        quitPressed = false;
    } else if (startPressed || quitPressed) {
        interactionOccurred = true;
    }

    if (!quitConfirmationOpen_ && input.menuSelectPressed) {
        interactionOccurred = true;
        if (focusedControl_ == FocusedControl::Start) {
            startPressed = true;
        } else if (focusedControl_ == FocusedControl::Quit) {
            quitPressed = true;
        }
    }

    DrawFocus(levelGauge, !quitConfirmationOpen_ && focusedControl_ == FocusedControl::Level);
    DrawFocus(densityGauge, !quitConfirmationOpen_ && focusedControl_ == FocusedControl::Density);
    DrawFocus(startButton, !quitConfirmationOpen_ && focusedControl_ == FocusedControl::Start);
    DrawFocus(quitButton, !quitConfirmationOpen_ && focusedControl_ == FocusedControl::Quit);

    if (quitPressed) {
        quitConfirmationOpen_ = true;
        quitDialogFocus_ = QuitDialogFocus::Cancel;
        interactionOccurred = true;
    }

    bool confirmQuitPressed = false;
    if (quitConfirmationOpen_) {
        DrawRectangle(
            0,
            0,
            config.screenWidth,
            config.screenHeight,
            Fade(BLACK, 0.6F));

        const Rectangle dialog = {
            .x = panel.x + 40.0F,
            .y = panel.y + 116.0F,
            .width = panel.width - 80.0F,
            .height = 150.0F,
        };
        const Rectangle confirmQuitButton = {
            .x = dialog.x + 24.0F,
            .y = dialog.y + 98.0F,
            .width = 132.0F,
            .height = 30.0F,
        };
        const Rectangle confirmCancelButton = {
            .x = dialog.x + dialog.width - 24.0F - 132.0F,
            .y = dialog.y + 98.0F,
            .width = 132.0F,
            .height = 30.0F,
        };

        DrawRectangleRounded(dialog, 0.12F, 8, Color{26, 31, 40, 248});
        DrawText(
            "Are you sure want to quit",
            static_cast<int>(dialog.x) + 20,
            static_cast<int>(dialog.y) + 24,
            20,
            RAYWHITE);

        bool modalQuitPressed = GuiButton(confirmQuitButton, "Quit");
        bool modalCancelPressed = GuiButton(confirmCancelButton, "Cancel");
        if (modalQuitPressed || modalCancelPressed) {
            interactionOccurred = true;
        }

        if (input.menuNavigateLeftPressed || input.menuNavigateRightPressed) {
            quitDialogFocus_ = quitDialogFocus_ == QuitDialogFocus::Quit
                ? QuitDialogFocus::Cancel
                : QuitDialogFocus::Quit;
            interactionOccurred = true;
        }

        if (input.menuSelectPressed) {
            interactionOccurred = true;
            if (quitDialogFocus_ == QuitDialogFocus::Quit) {
                modalQuitPressed = true;
            } else {
                modalCancelPressed = true;
            }
        }

        DrawFocus(confirmQuitButton, quitDialogFocus_ == QuitDialogFocus::Quit);
        DrawFocus(confirmCancelButton, quitDialogFocus_ == QuitDialogFocus::Cancel);

        if (modalQuitPressed) {
            confirmQuitPressed = true;
            quitConfirmationOpen_ = false;
        } else if (modalCancelPressed) {
            quitConfirmationOpen_ = false;
        }
    }

    GuiSetStyle(DEFAULT, TEXT_SIZE, previousTextSize);

    return MenuScreenResult{
        .startGameRequested = startPressed,
        .quitRequested = confirmQuitPressed,
        .interactionOccurred = interactionOccurred,
        .menuSettings =
            MenuSettings{
                .levelNumber = levelNumber_,
                .mazeDensity = mazeDensity_,
            },
    };
}
