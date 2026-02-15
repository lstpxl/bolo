#pragma once

struct FrameInput {
    float moveX;
    float moveY;
    bool shootPressed;
    bool startPressed;
    bool quitRequested;
    bool menuDifficultyUpPressed;
    bool menuDifficultyDownPressed;
    bool menuDensityDecreasePressed;
    bool menuDensityIncreasePressed;
};

FrameInput PollFrameInput();
