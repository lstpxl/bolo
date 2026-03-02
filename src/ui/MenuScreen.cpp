#include "ui/MenuScreen.h"

#include <algorithm>
#include <cmath>
#include "app/BuildInfo.h"
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

int RoundToNearestInt(float value) {
    return static_cast<int>(std::round(value));
}
}  // namespace

MenuScreenResult MenuScreen::Render(
    const MenuSettings& currentSettings,
    const AppConfig& config,
    const FrameInput& input) {
    levelNumber_ = std::clamp(currentSettings.levelNumber, kMinLevelNumber, kMaxLevelNumber);
    mazeDensity_ = std::clamp(currentSettings.mazeDensity, kMinMazeDensity, kMaxMazeDensity);
    invisibility_ = currentSettings.invisibility;
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

    const float panelWidth = std::min(440.0F, static_cast<float>(config.screenWidth) - 24.0F);
    const float panelHeight = static_cast<float>(config.screenHeight) - 24.0F;
    const Rectangle panel = {
        .x = (static_cast<float>(config.screenWidth) - panelWidth) * 0.5F,
        .y = (static_cast<float>(config.screenHeight) - panelHeight) * 0.5F,
        .width = panelWidth,
        .height = panelHeight,
    };
    const float panelCenterX = panel.x + panel.width * 0.5F;
    const float panelInnerPaddingX = 24.0F;
    const float controlsPaddingX = panelInnerPaddingX;
    const float controlsWidth = panel.width - controlsPaddingX * 2.0F;
    const float gaugeWidth = std::min(320.0F, controlsWidth);
    const float gaugeX = panelCenterX - gaugeWidth * 0.5F;
    const float buttonsX = panelCenterX - controlsWidth * 0.5F;

    const float titleY = panel.y + 20.0F;
    const float subtitleY = titleY + 46.0F;
    const float levelLabelY = subtitleY + 44.0F;
    const float levelGaugeY = levelLabelY + 28.0F;
    const float densityLabelY = levelGaugeY + 32.0F;
    const float densityGaugeY = densityLabelY + 28.0F;
    const float invisibilityY = densityGaugeY + 38.0F;
    const float buildTextY = panel.y + panel.height - 20.0F;
    const float quitButtonY = buildTextY - 38.0F - 60.0F;
    const float startButtonY = quitButtonY - 60.0F;

    DrawRectangleRounded(panel, 0.05F, 8, Color{38, 45, 58, 240});
    const int titleFontSize = 40;
    DrawText(
        "BOLT",
        static_cast<int>(panelCenterX) - MeasureText("BOLT", titleFontSize) / 2,
        static_cast<int>(titleY),
        titleFontSize,
        RAYWHITE);
    DrawText(
        TextFormat("Build #%d", CurrentBuildNumber()),
        static_cast<int>(panel.x + controlsPaddingX),
        static_cast<int>(buildTextY),
        10,
        GRAY);

    const int previousTextSize = GuiGetStyle(DEFAULT, TEXT_SIZE);
    GuiSetStyle(DEFAULT, TEXT_SIZE, 20);

    const char* subtitle = "Select level (1-9) and density (1-5)";
    DrawText(
        subtitle,
        static_cast<int>(panelCenterX) - MeasureText(subtitle, 20) / 2,
        static_cast<int>(subtitleY),
        20,
        LIGHTGRAY);

    const Rectangle levelGauge = Rectangle{gaugeX, levelGaugeY, gaugeWidth, 28.0F};
    const Rectangle densityGauge = Rectangle{gaugeX, densityGaugeY, gaugeWidth, 28.0F};
    const Rectangle invisibilityControl = Rectangle{gaugeX + 8.0F, invisibilityY, 28.0F, 28.0F};
    const Rectangle startButton = Rectangle{buttonsX, startButtonY, controlsWidth, 30.0F};
    const Rectangle quitButton = Rectangle{buttonsX, quitButtonY, controlsWidth, 30.0F};

    float levelValue = static_cast<float>(levelNumber_);
    DrawText(
        "Level",
        static_cast<int>(panelCenterX) - MeasureText("Level", 20) / 2,
        static_cast<int>(levelLabelY),
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
    levelNumber_ = RoundToNearestInt(levelValue);
    if (levelNumber_ != previousLevelNumber) {
        interactionOccurred = true;
    }

    float densityValue = static_cast<float>(mazeDensity_);
    DrawText(
        "Density",
        static_cast<int>(panelCenterX) - MeasureText("Density", 20) / 2,
        static_cast<int>(densityLabelY),
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
    mazeDensity_ = RoundToNearestInt(densityValue);
    if (mazeDensity_ != previousDensity) {
        interactionOccurred = true;
    }

    bool invisibilityValue = invisibility_;
    if (!quitConfirmationOpen_ && focusedControl_ == FocusedControl::Invisibility) {
        if (input.menuNavigateLeftPressed || input.menuNavigateRightPressed || input.menuSelectPressed) {
            invisibilityValue = !invisibilityValue;
            interactionOccurred = true;
        }
    }
    GuiCheckBox(invisibilityControl, "Invisibility", &invisibilityValue);
    if (invisibilityValue != invisibility_) {
        interactionOccurred = true;
    }
    invisibility_ = invisibilityValue;

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
    DrawFocus(invisibilityControl, !quitConfirmationOpen_ && focusedControl_ == FocusedControl::Invisibility);
    DrawFocus(startButton, !quitConfirmationOpen_ && focusedControl_ == FocusedControl::Start);
    DrawFocus(quitButton, !quitConfirmationOpen_ && focusedControl_ == FocusedControl::Quit);

    if (quitPressed) {
        quitConfirmationOpen_ = true;
        quitConfirmationDialog_.Open(ConfirmationDialog::Focus::Cancel);
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
        const ConfirmationDialogResult modalResult = quitConfirmationDialog_.Render(
            ConfirmationDialog::Spec{
                .bounds = dialog,
                .message = "Are you sure want to quit",
                .confirmButtonLabel = "Quit",
                .cancelButtonLabel = "Cancel",
            },
            input);
        if (modalResult.interactionOccurred) {
            interactionOccurred = true;
        }

        if (modalResult.confirmPressed) {
            confirmQuitPressed = true;
            quitConfirmationOpen_ = false;
        } else if (modalResult.cancelPressed) {
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
                .invisibility = invisibility_,
            },
    };
}
