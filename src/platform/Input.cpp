#include "platform/Input.h"

#include <cmath>
#include "raylib.h"

FrameInput PollFrameInput(InputPollState& pollState) {

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
        // D-pad up/down affect throttle only (forwardButtonDown/reverseButtonDown), not moveY,
        // to avoid velocity-snap artifact when braking (interpolating toward backward then
        // snapping to hull heading produced acceleration spikes).
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) {
            turretTurnInput -= 1.0F;
        }
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) {
            turretTurnInput += 1.0F;
        }
    }
    // A/D mirror Left/Right so the left hand can drive the tank while the right hand uses a mouse.
    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) {
        turnInput -= 1.0F;
    }
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) {
        turnInput += 1.0F;
    }
    // UP/DOWN affect throttle only (forwardButtonDown/reverseButtonDown), not moveY,
    // to avoid velocity-snap artifact when braking (interpolating toward backward then
    // snapping to hull heading produced acceleration spikes on Mac and elsewhere).
    // Turret rotate: L = clockwise, J = counter-clockwise (1/2 kept as legacy aliases).
    if (IsKeyDown(KEY_ONE) || IsKeyDown(KEY_J)) {
        turretTurnInput -= 1.0F;
    }
    if (IsKeyDown(KEY_TWO) || IsKeyDown(KEY_L)) {
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
    const bool escapeDown = IsKeyDown(KEY_ESCAPE);
    const bool gamepadForwardDown = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP);
    const bool gamepadReverseDown = IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
    const bool gamepadForwardPressed = IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP);
    const bool gamepadReversePressed = IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
    const bool gamepadForwardReleased = IsGamepadButtonReleased(0, GAMEPAD_BUTTON_LEFT_FACE_UP);
    const bool gamepadReverseReleased = IsGamepadButtonReleased(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN);

    // W/S mirror Up/Down (forward throttle / reverse throttle) for left-hand control.
    const bool keyForwardDown = IsKeyDown(KEY_UP) || IsKeyDown(KEY_W);
    const bool keyReverseDown = IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S);
    const bool keyForwardPressed = IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W);
    const bool keyReversePressed = IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S);
    const bool keyForwardReleased = IsKeyReleased(KEY_UP) || IsKeyReleased(KEY_W);
    const bool keyReverseReleased = IsKeyReleased(KEY_DOWN) || IsKeyReleased(KEY_S);
    const int anyKeyPressedCode = GetKeyPressed();
    const bool anyKnownKeyDown =
        IsKeyDown(KEY_LEFT) ||
        IsKeyDown(KEY_RIGHT) ||
        IsKeyDown(KEY_UP) ||
        IsKeyDown(KEY_DOWN) ||
        IsKeyDown(KEY_SPACE) ||
        IsKeyDown(KEY_ENTER) ||
        IsKeyDown(KEY_ESCAPE) ||
        IsKeyDown(KEY_P) ||
        IsKeyDown(KEY_W) ||
        IsKeyDown(KEY_A) ||
        IsKeyDown(KEY_S) ||
        IsKeyDown(KEY_D) ||
        IsKeyDown(KEY_T) ||
        IsKeyDown(KEY_F) ||
        IsKeyDown(KEY_G) ||
        IsKeyDown(KEY_H) ||
        IsKeyDown(KEY_I) ||
        IsKeyDown(KEY_J) ||
        IsKeyDown(KEY_K) ||
        IsKeyDown(KEY_L) ||
        IsKeyDown(KEY_Y) ||
        IsKeyDown(KEY_ONE) ||
        IsKeyDown(KEY_TWO);
    const bool anyGamepadButtonDown =
        IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP) ||
        IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) ||
        IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN) ||
        IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT) ||
        IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_UP) ||
        IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) ||
        IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) ||
        IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT) ||
        IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1) ||
        IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1) ||
        IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_2) ||
        IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_2) ||
        IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_LEFT) ||
        IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT) ||
        IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE);
    const bool anyAxisDown =
        std::abs(moveX) > 0.0F ||
        std::abs(moveY) > 0.0F ||
        std::abs(turnInput) > 0.0F ||
        std::abs(turretTurnInput) > 0.0F;

    const bool pauseDown = escapeDown || gamepadStartDown || gamepadMiddleDown;
    const bool pausePressed = pauseDown && !pollState.previousPauseDown;
    pollState.previousPauseDown = pauseDown;

    return FrameInput{
        .moveX = moveX,
        .moveY = moveY,
        .turnInput = turnInput,
        .turretTurnInput = turretTurnInput,
        .turretResetToHeadingPressed = IsKeyPressed(KEY_I),
        .turretResetToReverseHeadingPressed = IsKeyPressed(KEY_K),
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
        .gameplayPausePressed = pausePressed,
        .quitRequested = gamepadQuitComboPressed,
        .menuNavigateUpPressed =
            IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP),
        .menuNavigateDownPressed =
            IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN),
        .menuNavigateLeftPressed =
            IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT),
        .menuNavigateRightPressed =
            IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT),
        .menuSelectPressed = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || gamepadSouthPressed || gamepadEastPressed,
        .panTogglePressed = IsKeyPressed(KEY_P),
        .invisibilityTogglePressed = IsKeyPressed(KEY_Y),
        .panNorthPressed = IsKeyDown(KEY_T),
        .panSouthPressed = IsKeyDown(KEY_G),
        .panWestPressed = IsKeyDown(KEY_F),
        .panEastPressed = IsKeyDown(KEY_H),
        .anyInteractionPressed =
            anyKeyPressedCode != 0 ||
            keyForwardPressed ||
            keyReversePressed ||
            gamepadForwardPressed ||
            gamepadReversePressed ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_TRIGGER_2) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_2) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_LEFT) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE),
        .anyInteractionDown = anyKnownKeyDown || anyGamepadButtonDown || anyAxisDown,
    };
}
