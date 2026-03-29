#pragma once

#include <array>
#include <cstdint>
#include "app/AppConfig.h"
#include "game/model/GameplayConstants.h"
#include "platform/Input.h"
#include "raylib.h"

struct GameState;

class HudPanel {
public:
    void ReleaseResources();
    void ResetTransientState() const;
    void PrepareRenderTargets(const GameState& state, const AppConfig& config, const FrameInput& input) const;
    void DrawPrepared(const GameState& state, const AppConfig& config, const FrameInput& input) const;

private:
    static constexpr double kEnemySnapshotIntervalSeconds = 0.5;
    static constexpr double kFuelSnapshotIntervalSeconds = 0.5;
    static constexpr std::uint64_t kMinimapEnemyUpdateIntervalFrames = 1;
    static constexpr std::uint64_t kBasesRadarUpdateIntervalFrames = 1;
    static constexpr int kMinimapTrackedBaseCount = 6;
    static constexpr int kLivesIconSizePixels = 36;
    static constexpr int kLivesIconGapPixels = 1;

    struct HudLayout {
        int panelX = 0;
        int contentX = 0;
        int contentWidth = 0;
        int scoreY = 0;
        int livesY = 0;
        int fuelY = 0;
        int speedY = 0;
        int mapX = 0;
        int mapY = 0;
        int mapSize = 0;
        int blocksY = 0;
        int leftBlockSize = 0;
        int compassX = 0;
    };

    static HudLayout BuildHudLayout(int panelX, int hudWidth, int screenHeight);
    void EnsureBoltMetrics(int contentWidth) const;
    void EnsureStaticLayerTarget(int hudWidth, int screenHeight) const;
    void EnsureMinimapMarkersTarget(int mapSize) const;
    void EnsureBasesRadarTarget(int blockSize) const;
    void EnsureLivesIconTexture() const;
    void EnsureEnemyCountIconTextures() const;
    void RebuildStaticLayer(const AppConfig& config) const;
    void ResetMinimapMarkersLayer() const;
    void UpdateOneMinimapEntityMarker(const GameState& state) const;
    void UpdateBasesRadarLayer(int blockSize, int highlightedQuadrant) const;
    static int ComputeHighlightedQuadrant(const GameState& state);

    mutable int boltMetricsWidth_ = -1;
    mutable int boltBaseWidth_ = 0;
    mutable float boltSpacing_ = 1.0F;

    mutable bool cacheInitialized_ = false;
    mutable double lastEnemySnapshotSeconds_ = 0.0;
    mutable double lastFuelSnapshotSeconds_ = 0.0;

    mutable float cachedFuel_ = GameplayConstants::kFuelMax;
    mutable int cachedEnemyCount_ = 0;
    mutable int cachedAliveBases_ = 0;
    mutable int cachedDronesAlive_ = 0;
    mutable int cachedTorpedoesAlive_ = 0;
    mutable int cachedHuntersAlive_ = 0;
    mutable int cachedAssassinsAlive_ = 0;

    mutable bool staticLayerDirty_ = true;
    mutable RenderTexture2D staticLayerTarget_{};
    mutable bool staticLayerTargetLoaded_ = false;
    mutable int staticLayerWidth_ = 0;
    mutable int staticLayerHeight_ = 0;

    mutable bool minimapMarkersDirty_ = true;
    mutable RenderTexture2D minimapMarkersTarget_{};
    mutable bool minimapMarkersTargetLoaded_ = false;
    mutable int minimapMarkersSize_ = 0;
    mutable std::uint64_t lastMinimapEnemyUpdateFrame_ = 0;
    mutable int minimapEntityCursorIndex_ = -kMinimapTrackedBaseCount;
    mutable std::array<int, GameplayConstants::kMaxAliveEnemies + kMinimapTrackedBaseCount> minimapEntityCellX_{};
    mutable std::array<int, GameplayConstants::kMaxAliveEnemies + kMinimapTrackedBaseCount> minimapEntityCellY_{};
    mutable std::array<bool, GameplayConstants::kMaxAliveEnemies + kMinimapTrackedBaseCount> minimapEntityCellValid_{};

    mutable bool basesRadarDirty_ = true;
    mutable RenderTexture2D basesRadarTarget_{};
    mutable bool basesRadarTargetLoaded_ = false;
    mutable int basesRadarSize_ = 0;
    mutable std::uint64_t lastBasesRadarUpdateFrame_ = 0;
    mutable int cachedBasesRadarQuadrant_ = -2;

    mutable bool livesIconTextureLoadAttempted_ = false;
    mutable Texture2D livesIconTexture_{};
    mutable bool livesIconTextureLoaded_ = false;
    mutable bool enemyCountIconTexturesLoadAttempted_ = false;
    mutable std::array<Texture2D, 4> enemyCountIconTextures_{};
    mutable std::array<bool, 4> enemyCountIconTexturesLoaded_{};
};
