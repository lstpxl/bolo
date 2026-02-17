#pragma once

struct FrameInput {
    float moveX = 0.0F;
    float moveY = 0.0F;
    float turnInput = 0.0F;
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
};

FrameInput PollFrameInput();
