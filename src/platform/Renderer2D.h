#pragma once

#include <array>
#include "app/AppConfig.h"
#include "game/GameState.h"
#include "raylib.h"

class Renderer2D {
public:
    bool LoadResources();
    void UnloadResources();
    void DrawWorld(const GameState& state, const AppConfig& config);

private:
    static constexpr int kMaxPlayerTankFrames = 8;
    static constexpr int kEnemyTankTypeCount = 4;
    static constexpr int kEnemyTankDirectionCount = 8;
    static constexpr int kEnemyTankFrameSizePx = 9;
    Texture2D playerTankSheet_{};
    bool playerTankSheetLoaded_ = false;
    int playerTankFrameSizePx_ = 9;
    int playerTankFrameCount_ = 8;
    std::array<Vector2, kMaxPlayerTankFrames> playerTankFrameOffsetsPixels_{};
    Texture2D enemyTankSheet_{};
    bool enemyTankSheetLoaded_ = false;
    Texture2D enemyExplosionSheet_{};
    bool enemyExplosionSheetLoaded_ = false;
    Texture2D playerExplosionSheet_{};
    bool playerExplosionSheetLoaded_ = false;
    Texture2D baseExplosionSheet_{};
    bool baseExplosionSheetLoaded_ = false;
};
