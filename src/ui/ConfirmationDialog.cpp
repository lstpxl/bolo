#include "ui/ConfirmationDialog.h"

#include "ui/UiPrimitives.h"
#include "raygui.h"
#include "raylib.h"

void ConfirmationDialog::Open(Focus initialFocus) {
    focus_ = initialFocus;
    suppressInputPressOnce_ = true;
}

ConfirmationDialogResult ConfirmationDialog::Render(
    const Spec& spec,
    const FrameInput& input) {
    const Rectangle confirmButton = {
        .x = spec.bounds.x + 24.0F,
        .y = spec.bounds.y + 98.0F,
        .width = 132.0F,
        .height = 30.0F,
    };
    const Rectangle cancelButton = {
        .x = spec.bounds.x + spec.bounds.width - 24.0F - 132.0F,
        .y = spec.bounds.y + 98.0F,
        .width = 132.0F,
        .height = 30.0F,
    };

    DrawRectangleRounded(spec.bounds, 0.12F, 8, Color{32, 32, 32, 248});
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

    if (IsKeyPressed(KEY_ESCAPE) && !suppressInputPressOnce_) {
        cancelPressed = true;
        result.interactionOccurred = true;
    }
    suppressInputPressOnce_ = false;

    ui::primitives::DrawFocusRing(confirmButton, focus_ == Focus::Confirm);
    ui::primitives::DrawFocusRing(cancelButton, focus_ == Focus::Cancel);

    result.confirmPressed = confirmPressed;
    result.cancelPressed = cancelPressed;
    return result;
}
