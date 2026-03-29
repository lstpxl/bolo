#include "ui/ConfirmationDialog.h"

#include "ui/UiPrimitives.h"
#include "raygui.h"
#include "raylib.h"

void ConfirmationDialog::ApplyRayGuiStyle(int buttonTextSize) {
    GuiLoadStyleDefault();
    const int transparent = static_cast<int>(ColorToInt(BLANK));
    const int borderGold = static_cast<int>(ColorToInt(Color{180, 160, 70, 255}));
    const int borderGoldHi = static_cast<int>(ColorToInt(Color{255, 209, 102, 255}));
    const int textLight = static_cast<int>(ColorToInt(Color{210, 215, 225, 255}));
    const int pressedTint = static_cast<int>(ColorToInt(Fade(WHITE, 0.15F)));

    GuiSetStyle(DEFAULT, TEXT_SIZE, buttonTextSize);
    GuiSetStyle(BUTTON, BORDER_WIDTH, 1);
    GuiSetStyle(BUTTON, BORDER_COLOR_NORMAL, borderGold);
    GuiSetStyle(BUTTON, BASE_COLOR_NORMAL, transparent);
    GuiSetStyle(BUTTON, TEXT_COLOR_NORMAL, textLight);
    GuiSetStyle(BUTTON, BORDER_COLOR_FOCUSED, borderGoldHi);
    GuiSetStyle(BUTTON, BASE_COLOR_FOCUSED, transparent);
    GuiSetStyle(BUTTON, TEXT_COLOR_FOCUSED, textLight);
    GuiSetStyle(BUTTON, BORDER_COLOR_PRESSED, borderGoldHi);
    GuiSetStyle(BUTTON, BASE_COLOR_PRESSED, pressedTint);
    GuiSetStyle(BUTTON, TEXT_COLOR_PRESSED, textLight);
}

void ConfirmationDialog::Open(Focus initialFocus) {
    focus_ = initialFocus;
    suppressInputPressOnce_ = true;
}

ConfirmationDialogResult ConfirmationDialog::Render(
    const Spec& spec,
    const FrameInput& input) {
    ApplyRayGuiStyle(spec.buttonTextSize);
    const Rectangle confirmButton = {
        .x = spec.bounds.x + spec.buttonsSidePadding,
        .y = spec.bounds.y + spec.buttonsTop,
        .width = spec.buttonWidth,
        .height = spec.buttonHeight,
    };
    const Rectangle cancelButton = {
        .x = spec.bounds.x + spec.bounds.width - spec.buttonsSidePadding - spec.buttonWidth,
        .y = spec.bounds.y + spec.buttonsTop,
        .width = spec.buttonWidth,
        .height = spec.buttonHeight,
    };

    DrawRectangleRounded(spec.bounds, 0.12F, 8, Color{8, 8, 8, 124});
    DrawRectangleRoundedLinesEx(spec.bounds, 0.12F, 8, 1.0F, Color{180, 160, 70, 255});
    constexpr int kDialogMessageFontPx = 20;
    const int messageWidth = MeasureText(spec.message, kDialogMessageFontPx);
    const int messageX = static_cast<int>(
        spec.bounds.x + (spec.bounds.width - static_cast<float>(messageWidth)) * 0.5F);
    const int messageY = static_cast<int>(spec.bounds.y) + 24 + kDialogMessageFontPx;
    DrawText(spec.message, messageX, messageY, kDialogMessageFontPx, RAYWHITE);

    bool confirmPressed = GuiButton(confirmButton, spec.confirmButtonLabel);
    bool cancelPressed = GuiButton(cancelButton, spec.cancelButtonLabel);

    ConfirmationDialogResult result{};
    if (confirmPressed || cancelPressed) {
        result.interactionOccurred = true;
    }

    if (input.menuNavigateLeftPressed || input.menuNavigateRightPressed) {
        focus_ = focus_ == Focus::Confirm ? Focus::Cancel : Focus::Confirm;
        result.interactionOccurred = true;
    }

    if (input.menuSelectPressed && !suppressInputPressOnce_) {
        result.interactionOccurred = true;
        result.buttonActivatedViaMenuSelect = true;
        if (focus_ == Focus::Confirm) {
            confirmPressed = true;
        } else {
            cancelPressed = true;
        }
    }

    if (input.gameplayPausePressed && !suppressInputPressOnce_) {
        cancelPressed = true;
        result.interactionOccurred = true;
    }
    suppressInputPressOnce_ = false;

    ui::primitives::DrawFocusRing(confirmButton, focus_ == Focus::Confirm);
    ui::primitives::DrawFocusRing(cancelButton, focus_ == Focus::Cancel);

    result.confirmPressed = confirmPressed;
    result.cancelPressed = cancelPressed;
    GuiLoadStyleDefault();
    return result;
}
