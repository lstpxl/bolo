#pragma once

#include "platform/Input.h"
#include "raylib.h"

struct ConfirmationDialogSpec {
    Rectangle bounds{};
    const char* message = "";
    const char* confirmButtonLabel = "";
    const char* cancelButtonLabel = "";
};

struct ConfirmationDialogResult {
    bool confirmPressed = false;
    bool cancelPressed = false;
    bool interactionOccurred = false;
};

class ConfirmationDialog {
public:
    enum class Focus {
        Confirm = 0,
        Cancel = 1,
    };

    void Open(Focus initialFocus);

    ConfirmationDialogResult Render(
        const ConfirmationDialogSpec& spec,
        const FrameInput& input);

private:
    Focus focus_ = Focus::Cancel;
};
