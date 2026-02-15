#include "platform/Input.h"

#include "raylib.h"

FrameInput PollFrameInput() {
    constexpr float axisDeadzone = 0.2F;
    float moveX = 0.0F;
    float moveY = 0.0F;

    if (IsGamepadAvailable(0)) {
        const float axisX = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
        const float axisY = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
        if (axisX > axisDeadzone || axisX < -axisDeadzone) {
            moveX += axisX;
        }
        if (axisY > axisDeadzone || axisY < -axisDeadzone) {
            moveY += axisY;
        }

        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) {
            moveX -= 1.0F;
        }
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) {
            moveX += 1.0F;
        }
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP)) {
            moveY -= 1.0F;
        }
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) {
            moveY += 1.0F;
        }
    } else {
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) {
            moveX -= 1.0F;
        }
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) {
            moveX += 1.0F;
        }
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) {
            moveY -= 1.0F;
        }
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) {
            moveY += 1.0F;
        }
    }

    if (moveX > 1.0F) {
        moveX = 1.0F;
    }
    if (moveX < -1.0F) {
        moveX = -1.0F;
    }
    if (moveY > 1.0F) {
        moveY = 1.0F;
    }
    if (moveY < -1.0F) {
        moveY = -1.0F;
    }

    const bool gamepadStartPressed = IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT) ||
        IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE);
    const bool gamepadSelectDown = IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_LEFT);
    const bool gamepadStartDown = IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT);
    const bool gamepadMiddleDown = IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE);
    const bool gamepadQuitComboPressed =
        (gamepadSelectDown && (gamepadStartDown || gamepadMiddleDown)) ||
        (gamepadMiddleDown && gamepadStartDown);

    return FrameInput{
        .moveX = moveX,
        .moveY = moveY,
        .shootPressed = IsKeyPressed(KEY_SPACE) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN),
        .startPressed = IsKeyPressed(KEY_ENTER) || gamepadStartPressed,
        .quitRequested = IsKeyPressed(KEY_ESCAPE) || gamepadQuitComboPressed,
        .menuDifficultyUpPressed =
            IsKeyPressed(KEY_UP) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP),
        .menuDifficultyDownPressed =
            IsKeyPressed(KEY_DOWN) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN),
        .menuDensityDecreasePressed =
            IsKeyPressed(KEY_LEFT) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT),
        .menuDensityIncreasePressed =
            IsKeyPressed(KEY_RIGHT) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT),
    };
}
