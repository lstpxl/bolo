#pragma once

struct FrameInput {
    float moveX;
    float moveY;
    bool shootPressed;
    bool startPressed;
    bool quitRequested;
};

FrameInput PollFrameInput();
