#include "platform/Input.h"

#include "raylib.h"

FrameInput PollFrameInput() {
    constexpr float axisDeadzone = 0.2F;
    float moveX = 0.0F;
    float moveY = 0.0F;
    float turnInput = 0.0F;

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
            turnInput -= 1.0F;
        }
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) {
            turnInput += 1.0F;
        }
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP)) {
            moveY -= 1.0F;
        }
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) {
            moveY += 1.0F;
        }
    } else {
        if (IsKeyDown(KEY_A)) {
            moveX -= 1.0F;
        }
        if (IsKeyDown(KEY_D)) {
            moveX += 1.0F;
        }
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) {
            moveY -= 1.0F;
        }
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) {
            moveY += 1.0F;
        }
        if (IsKeyDown(KEY_LEFT)) {
            turnInput -= 1.0F;
        }
        if (IsKeyDown(KEY_RIGHT)) {
            turnInput += 1.0F;
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
    if (turnInput > 1.0F) {
        turnInput = 1.0F;
    }
    if (turnInput < -1.0F) {
        turnInput = -1.0F;
    }

    const bool gamepadStartPressed = IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT) ||
        IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE);
    const bool gamepadSelectDown = IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_LEFT);
    const bool gamepadStartDown = IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT);
    const bool gamepadMiddleDown = IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE);
    const bool gamepadQuitComboPressed =
        (gamepadSelectDown && (gamepadStartDown || gamepadMiddleDown)) ||
        (gamepadMiddleDown && gamepadStartDown);
    const bool gamepadSouthPressed = IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    const bool gamepadEastPressed = IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
    const bool gamepadForwardDown = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP);
    const bool gamepadReverseDown = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
    const bool gamepadForwardPressed = IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP);
    const bool gamepadReversePressed = IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
    const bool gamepadForwardReleased = IsGamepadButtonReleased(0, GAMEPAD_BUTTON_LEFT_FACE_UP);
    const bool gamepadReverseReleased = IsGamepadButtonReleased(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN);

    const bool keyForwardDown = IsKeyDown(KEY_W) || IsKeyDown(KEY_UP);
    const bool keyReverseDown = IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN);
    const bool keyForwardPressed = IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP);
    const bool keyReversePressed = IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN);
    const bool keyForwardReleased = IsKeyReleased(KEY_W) || IsKeyReleased(KEY_UP);
    const bool keyReverseReleased = IsKeyReleased(KEY_S) || IsKeyReleased(KEY_DOWN);

    return FrameInput{
        .moveX = moveX,
        .moveY = moveY,
        .turnInput = turnInput,
        .forwardButtonDown = keyForwardDown || gamepadForwardDown,
        .forwardButtonPressed = keyForwardPressed || gamepadForwardPressed,
        .forwardButtonReleased = keyForwardReleased || gamepadForwardReleased,
        .reverseButtonDown = keyReverseDown || gamepadReverseDown,
        .reverseButtonPressed = keyReversePressed || gamepadReversePressed,
        .reverseButtonReleased = keyReverseReleased || gamepadReverseReleased,
        .shootPressed = IsKeyPressed(KEY_SPACE) || gamepadSouthPressed,
        .startPressed = IsKeyPressed(KEY_ENTER) || gamepadStartPressed,
        .quitRequested = IsKeyPressed(KEY_ESCAPE) || gamepadQuitComboPressed,
        .menuNavigateUpPressed =
            IsKeyPressed(KEY_UP) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP),
        .menuNavigateDownPressed =
            IsKeyPressed(KEY_DOWN) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN),
        .menuNavigateLeftPressed =
            IsKeyPressed(KEY_LEFT) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT),
        .menuNavigateRightPressed =
            IsKeyPressed(KEY_RIGHT) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT),
        .menuSelectPressed = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || gamepadSouthPressed || gamepadEastPressed,
    };
}
