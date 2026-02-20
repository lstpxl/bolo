#pragma once

#include "app/AppConfig.h"
#include "game/GameState.h"
#include "raylib.h"

class Renderer2D {
public:
    bool LoadResources();
    void UnloadResources();
    void DrawWorld(const GameState& state, const AppConfig& config);

private:
    Texture2D playerTankSheet_{};
    bool playerTankSheetLoaded_ = false;
    int playerTankFrameSizePx_ = 20;
    int playerTankFrameCount_ = 24;
};
