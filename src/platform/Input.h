#pragma once

struct FrameInput {
    float moveX = 0.0F;
    float moveY = 0.0F;
    float turnInput = 0.0F;
    bool shootPressed = false;
    bool startPressed = false;
    bool quitRequested = false;
    bool menuNavigateUpPressed = false;
    bool menuNavigateDownPressed = false;
    bool menuNavigateLeftPressed = false;
    bool menuNavigateRightPressed = false;
    bool menuSelectPressed = false;
};

FrameInput PollFrameInput();
