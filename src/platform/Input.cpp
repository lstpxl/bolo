#include "platform/Input.h"

#include <cmath>
#include "raylib.h"

FrameInput PollFrameInput() {
    constexpr float axisDeadzone = 0.2F;
    constexpr float axisToRawScale = 32767.0F;
    float moveX = 0.0F;
    float moveY = 0.0F;
    float turnInput = 0.0F;
    float turretTurnInput = 0.0F;
    int gamepadAxis0Raw = 0;
    int gamepadAxis1Raw = 0;
    int gamepadAxis2Raw = 0;
    int gamepadAxis3Raw = 0;

    if (IsGamepadAvailable(0)) {
        const float axisX = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
        const float axisY = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
        const float axisZ = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X);
        const float axisW = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y);
        gamepadAxis0Raw = static_cast<int>(std::lround(axisX * axisToRawScale));
        gamepadAxis1Raw = static_cast<int>(std::lround(axisY * axisToRawScale));
        gamepadAxis2Raw = static_cast<int>(std::lround(axisZ * axisToRawScale));
        gamepadAxis3Raw = static_cast<int>(std::lround(axisW * axisToRawScale));
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
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) {
            turretTurnInput -= 1.0F;
        }
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) {
            turretTurnInput += 1.0F;
        }
    }
    if (IsKeyDown(KEY_LEFT)) {
        moveX -= 1.0F;
        turnInput -= 1.0F;
    }
    if (IsKeyDown(KEY_RIGHT)) {
        moveX += 1.0F;
        turnInput += 1.0F;
    }
    if (IsKeyDown(KEY_UP)) {
        moveY -= 1.0F;
    }
    if (IsKeyDown(KEY_DOWN)) {
        moveY += 1.0F;
    }
    if (IsKeyDown(KEY_ONE)) {
        turretTurnInput -= 1.0F;
    }
    if (IsKeyDown(KEY_TWO)) {
        turretTurnInput += 1.0F;
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
    if (turretTurnInput > 1.0F) {
        turretTurnInput = 1.0F;
    }
    if (turretTurnInput < -1.0F) {
        turretTurnInput = -1.0F;
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
    const bool escapePressed = IsKeyPressed(KEY_ESCAPE);
    const bool gamepadForwardDown = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP);
    const bool gamepadReverseDown = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
    const bool gamepadForwardPressed = IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP);
    const bool gamepadReversePressed = IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
    const bool gamepadForwardReleased = IsGamepadButtonReleased(0, GAMEPAD_BUTTON_LEFT_FACE_UP);
    const bool gamepadReverseReleased = IsGamepadButtonReleased(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN);

    const bool keyForwardDown = IsKeyDown(KEY_UP);
    const bool keyReverseDown = IsKeyDown(KEY_DOWN);
    const bool keyForwardPressed = IsKeyPressed(KEY_UP);
    const bool keyReversePressed = IsKeyPressed(KEY_DOWN);
    const bool keyForwardReleased = IsKeyReleased(KEY_UP);
    const bool keyReverseReleased = IsKeyReleased(KEY_DOWN);

    return FrameInput{
        .moveX = moveX,
        .moveY = moveY,
        .turnInput = turnInput,
        .turretTurnInput = turretTurnInput,
        .gamepadAxis0Raw = gamepadAxis0Raw,
        .gamepadAxis1Raw = gamepadAxis1Raw,
        .gamepadAxis2Raw = gamepadAxis2Raw,
        .gamepadAxis3Raw = gamepadAxis3Raw,
        .forwardButtonDown = keyForwardDown || gamepadForwardDown,
        .forwardButtonPressed = keyForwardPressed || gamepadForwardPressed,
        .forwardButtonReleased = keyForwardReleased || gamepadForwardReleased,
        .reverseButtonDown = keyReverseDown || gamepadReverseDown,
        .reverseButtonPressed = keyReversePressed || gamepadReversePressed,
        .reverseButtonReleased = keyReverseReleased || gamepadReverseReleased,
        .shootPressed = IsKeyPressed(KEY_SPACE) || gamepadSouthPressed,
        .startPressed = IsKeyPressed(KEY_ENTER) || gamepadStartPressed,
        .gameplayPausePressed = escapePressed || gamepadStartPressed,
        .quitRequested = gamepadQuitComboPressed,
        .menuNavigateUpPressed =
            IsKeyPressed(KEY_UP) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP),
        .menuNavigateDownPressed =
            IsKeyPressed(KEY_DOWN) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN),
        .menuNavigateLeftPressed =
            IsKeyPressed(KEY_LEFT) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT),
        .menuNavigateRightPressed =
            IsKeyPressed(KEY_RIGHT) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT),
        .menuSelectPressed = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || gamepadSouthPressed || gamepadEastPressed,
        .panTogglePressed = IsKeyPressed(KEY_P),
        .panNorthPressed = IsKeyDown(KEY_W),
        .panSouthPressed = IsKeyDown(KEY_S),
        .panWestPressed = IsKeyDown(KEY_A),
        .panEastPressed = IsKeyDown(KEY_D),
    };
}
