#pragma once

#include <array>
#include <vector>
#include "app/AppConfig.h"
#include "core/Types.h"
#include "game/model/EntityTypes.h"
#include "game/model/GameplayConstants.h"
#include "raylib.h"

struct GameState;

class Renderer2D {
public:
    bool LoadResources();
    void UnloadResources();
    void DrawWorld(const GameState& state, const AppConfig& config, bool reserveHudViewport = true);
    void DrawMenuBackground(
        const MazeState& maze,
        const std::vector<EnemyTank>& enemies,
        const Vec2f& cameraTarget,
        const AppConfig& config);

private:
    static constexpr int kMaxPlayerTankFrames = 8;
    static constexpr int kEnemyTankTypeCount = 4;
    /// GPU enemy sheet rows: drone watch, drone wander, torpedo, hunter, assassin.
    static constexpr int kEnemyTankSheetRowCount = 5;
    static constexpr int kEnemyTankDirectionCount = 8;
    static constexpr int kEnemyTankFrameSizePx = 9;
    static constexpr int kBaseDamageCacheSlotCount = GameplayConstants::kEnemyBaseCount;
    struct BaseHealthSnapshot {
        int top = GameplayConstants::kBaseOuterSegmentMaxHealth;
        int right = GameplayConstants::kBaseOuterSegmentMaxHealth;
        int bottom = GameplayConstants::kBaseOuterSegmentMaxHealth;
        int left = GameplayConstants::kBaseOuterSegmentMaxHealth;
        bool initialized = false;
    };
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
    Texture2D baseHealthyTexture_{};
    bool baseHealthyTextureLoaded_ = false;
    std::array<Texture2D, kBaseDamageCacheSlotCount> baseDamagedTextures_{};
    std::array<bool, kBaseDamageCacheSlotCount> baseDamagedTextureLoaded_{};
    std::array<BaseHealthSnapshot, kBaseDamageCacheSlotCount> baseHealthSnapshots_{};
    std::array<bool, kBaseDamageCacheSlotCount> baseDamageCacheDisabled_{};
    Texture2D baseDestroyedTexture_{};
    bool baseDestroyedTextureLoaded_ = false;
};
