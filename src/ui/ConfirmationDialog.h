#pragma once

#include "platform/Input.h"
#include "raylib.h"

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

    struct Spec {
        Rectangle bounds{};
        const char* message = "";
        const char* confirmButtonLabel = "";
        const char* cancelButtonLabel = "";
    };

    void Open(Focus initialFocus);

    ConfirmationDialogResult Render(
        const Spec& spec,
        const FrameInput& input);

private:
    Focus focus_ = Focus::Cancel;
    bool suppressInputPressOnce_ = false;
};
