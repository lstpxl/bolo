#pragma once

#include "platform/Input.h"
#include "raylib.h"

struct ConfirmationDialogResult {
    bool confirmPressed = false;
    bool cancelPressed = false;
    bool interactionOccurred = false;
    /// Confirm or cancel was chosen with menu Select/Enter (not mouse, not Escape).
    bool buttonActivatedViaMenuSelect = false;
};

class ConfirmationDialog {
public:
    enum class Focus {
        Confirm = 0,
        Cancel = 1,
    };

    struct Spec {
        Rectangle bounds{};
        const char* message = "";
        const char* confirmButtonLabel = "";
        const char* cancelButtonLabel = "";
        int buttonTextSize = 20;
        float buttonWidth = 132.0F;
        float buttonHeight = 30.0F;
        float buttonsSidePadding = 24.0F;
        float buttonsTop = 98.0F;
    };

    static void ApplyRayGuiStyle(int buttonTextSize = 20);
    void Open(Focus initialFocus);

    ConfirmationDialogResult Render(
        const Spec& spec,
        const FrameInput& input);

private:
    Focus focus_ = Focus::Cancel;
    bool suppressInputPressOnce_ = false;
};
