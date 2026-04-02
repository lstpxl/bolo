#pragma once

struct FrameInput {
    float moveX = 0.0F;
    float moveY = 0.0F;
    float turnInput = 0.0F;
    float turretTurnInput = 0.0F;
    int gamepadAxis0Raw = 0;
    int gamepadAxis1Raw = 0;
    int gamepadAxis2Raw = 0;
    int gamepadAxis3Raw = 0;
    bool forwardButtonDown = false;
    bool forwardButtonPressed = false;
    bool forwardButtonReleased = false;
    bool reverseButtonDown = false;
    bool reverseButtonPressed = false;
    bool reverseButtonReleased = false;
    bool shootPressed = false;
    bool startPressed = false;
    bool gameplayPausePressed = false;
    bool quitRequested = false;
    bool menuNavigateUpPressed = false;
    bool menuNavigateDownPressed = false;
    bool menuNavigateLeftPressed = false;
    bool menuNavigateRightPressed = false;
    bool menuSelectPressed = false;
    bool panTogglePressed = false;
    bool invisibilityTogglePressed = false;
    bool panNorthPressed = false;
    bool panSouthPressed = false;
    bool panWestPressed = false;
    bool panEastPressed = false;
    bool anyInteractionPressed = false;
    bool anyInteractionDown = false;
};

/// Persistent input state that must be owned by the caller and reset on each game start.
/// Holds edge-detect state that would otherwise live as a static local inside PollFrameInput.
struct InputPollState {
    bool previousPauseDown = false;
};

FrameInput PollFrameInput(InputPollState& pollState);
