#include "ui/HudPanel.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <string>
#include "core/Log.h"
#include "core/Profiling.h"
#include "core/ResourceLocator.h"
#include "game/GameQueries.h"
#include "raylib.h"

namespace {
constexpr int kContentPadding = 10;
constexpr int kBoltFontSize = 50;
constexpr int kLivesBlockTopPadding = 12;
constexpr int kMinimapLogicalSizePixels = 60;
constexpr int kMinimapDisplayScale = 2;
constexpr int kMinimapDisplaySizePixels = kMinimapLogicalSizePixels * kMinimapDisplayScale;

constexpr Color kDroneMapColor{138, 43, 226, 255};      // #8A2BE2
constexpr Color kTorpedoMapColor{255, 255, 0, 255};     // #FFFF00
constexpr Color kHunterMapColor{255, 165, 0, 255};      // #FFA500
constexpr Color kAssassinMapColor{255, 0, 0, 255};      // #FF0000
constexpr Color kPlayerMapColor{0, 255, 255, 255};      // #00FFFF
constexpr Color kBaseMapColor{255, 0, 255, 255};        // #FF00FF
constexpr Color kDestroyedBaseMapColor{96, 96, 96, 255};    // #606060
constexpr Color kPanelColor{27, 31, 39, 255};
constexpr Color kPanelDividerColor{58, 66, 80, 255};
constexpr Color kCompassBackgroundColor{237, 126, 188, 255};
constexpr Color kQuadrantDimColor{18, 60, 26, 255};
constexpr Color kQuadrantBrightColor{160, 255, 120, 255};

constexpr int kSpriteSheetColumns = 2;
constexpr int kSpriteSheetRows = 7;
constexpr int kSpriteSheetCellSize = 9;
constexpr int kPlayerBodyRowIndex = 0;
constexpr int kPlayerBarrelRowIndex = 1;
constexpr int kEnemySpriteFirstRowIndex = 3;
constexpr int kEnemyCountIconSizePixels = 10;

constexpr Color ColorFromHexRGB(std::uint32_t hex) {
    return Color{
        static_cast<unsigned char>((hex >> 16U) & 0xFFU),
        static_cast<unsigned char>((hex >> 8U) & 0xFFU),
        static_cast<unsigned char>(hex & 0xFFU),
        255,
    };
}

bool TryLoadImageAtPath(Image& image, const char* path) {
    if (!FileExists(path)) {
        return false;
    }
    image = LoadImage(path);
    return image.data != nullptr;
}

bool TryLoadImageFromTextureDirectory(Image& image, const char* fileName) {
    const std::string path = core::resources::ResolveResourcePath("textures", fileName);
    return !path.empty() && TryLoadImageAtPath(image, path.c_str());
}

Image ExtractSpriteCell(const Image& spriteSheet, int columnIndex, int rowIndex, int cellSizePx) {
    const Rectangle sourceRect{
        .x = static_cast<float>(columnIndex * cellSizePx),
        .y = static_cast<float>(rowIndex * cellSizePx),
        .width = static_cast<float>(cellSizePx),
        .height = static_cast<float>(cellSizePx),
    };
    return ImageFromImage(spriteSheet, sourceRect);
}

void FillOpaquePixelsColor(Image& image, Color color) {
    Color* pixels = static_cast<Color*>(image.data);
    if (pixels == nullptr) {
        return;
    }
    const int pixelCount = image.width * image.height;
    for (int i = 0; i < pixelCount; ++i) {
        if (pixels[i].a == 0) {
            continue;
        }
        pixels[i].r = color.r;
        pixels[i].g = color.g;
        pixels[i].b = color.b;
    }
}

Image CombineCellsXor(const Image& bodyCell, const Image& barrelCell, Color color) {
    Image combined = GenImageColor(kSpriteSheetCellSize, kSpriteSheetCellSize, BLANK);
    Color* combinedPixels = static_cast<Color*>(combined.data);
    const Color* bodyPixels = static_cast<const Color*>(bodyCell.data);
    const Color* barrelPixels = static_cast<const Color*>(barrelCell.data);
    if (combinedPixels == nullptr || bodyPixels == nullptr || barrelPixels == nullptr) {
        return combined;
    }

    const int pixelCount = kSpriteSheetCellSize * kSpriteSheetCellSize;
    for (int i = 0; i < pixelCount; ++i) {
        const bool bodyOpaque = bodyPixels[i].a > 0;
        const bool barrelOpaque = barrelPixels[i].a > 0;
        const bool xorVisible = bodyOpaque != barrelOpaque;
        if (!xorVisible) {
            combinedPixels[i] = BLANK;
            continue;
        }
        combinedPixels[i] = color;
        combinedPixels[i].a = bodyOpaque ? bodyPixels[i].a : barrelPixels[i].a;
    }
    return combined;
}

Color EnemyMapColor(EnemyType type) {
    if (type == EnemyType::Drone) {
        return kDroneMapColor;
    }
    if (type == EnemyType::Torpedo) {
        return kTorpedoMapColor;
    }
    if (type == EnemyType::Hunter) {
        return kHunterMapColor;
    }
    return kAssassinMapColor;
}

void DrawBasesRadarQuadrantsAt(int originX, int originY, int blockSize, int highlightedQuadrant) {
    const int quadrantCell = blockSize / 2;
    DrawRectangle(
        originX + 2,
        originY + 2,
        quadrantCell - 3,
        quadrantCell - 3,
        highlightedQuadrant == 0 ? kQuadrantBrightColor : kQuadrantDimColor);
    DrawRectangle(
        originX + quadrantCell + 1,
        originY + 2,
        quadrantCell - 3,
        quadrantCell - 3,
        highlightedQuadrant == 1 ? kQuadrantBrightColor : kQuadrantDimColor);
    DrawRectangle(
        originX + 2,
        originY + quadrantCell + 1,
        quadrantCell - 3,
        quadrantCell - 3,
        highlightedQuadrant == 2 ? kQuadrantBrightColor : kQuadrantDimColor);
    DrawRectangle(
        originX + quadrantCell + 1,
        originY + quadrantCell + 1,
        quadrantCell - 3,
        quadrantCell - 3,
        highlightedQuadrant == 3 ? kQuadrantBrightColor : kQuadrantDimColor);
}
}  // namespace

void HudPanel::ReleaseResources() {
    if (staticLayerTargetLoaded_) {
        UnloadRenderTexture(staticLayerTarget_);
        staticLayerTarget_ = RenderTexture2D{};
        staticLayerTargetLoaded_ = false;
    }
    if (minimapMarkersTargetLoaded_) {
        UnloadRenderTexture(minimapMarkersTarget_);
        minimapMarkersTarget_ = RenderTexture2D{};
        minimapMarkersTargetLoaded_ = false;
    }
    if (basesRadarTargetLoaded_) {
        UnloadRenderTexture(basesRadarTarget_);
        basesRadarTarget_ = RenderTexture2D{};
        basesRadarTargetLoaded_ = false;
    }
    if (livesIconTextureLoaded_) {
        UnloadTexture(livesIconTexture_);
        livesIconTexture_ = Texture2D{};
        livesIconTextureLoaded_ = false;
    }
    for (std::size_t i = 0; i < enemyCountIconTextures_.size(); ++i) {
        if (!enemyCountIconTexturesLoaded_[i]) {
            continue;
        }
        UnloadTexture(enemyCountIconTextures_[i]);
        enemyCountIconTextures_[i] = Texture2D{};
        enemyCountIconTexturesLoaded_[i] = false;
    }

    livesIconTextureLoadAttempted_ = false;
    enemyCountIconTexturesLoadAttempted_ = false;
    staticLayerWidth_ = 0;
    staticLayerHeight_ = 0;
    minimapMarkersSize_ = 0;
    basesRadarSize_ = 0;
    staticLayerDirty_ = true;
    minimapMarkersDirty_ = true;
    basesRadarDirty_ = true;
    boltMetricsWidth_ = -1;
    lastMinimapEnemyUpdateFrame_ = 0;
    minimapEntityCursorIndex_ = -kMinimapTrackedBaseCount;
    minimapEntityCellValid_.fill(false);
    lastBasesRadarUpdateFrame_ = 0;
    cachedBasesRadarQuadrant_ = -2;
}

HudPanel::HudLayout HudPanel::BuildHudLayout(int panelX, int hudWidth, int screenHeight) {
    HudLayout layout{};
    layout.panelX = panelX;
    layout.contentX = panelX + kContentPadding;
    layout.contentWidth = hudWidth - (kContentPadding * 2);

    int cursorY = 8;
    cursorY += 56;
    layout.scoreY = cursorY;
    cursorY += 44;
    layout.livesY = cursorY;
    cursorY += kLivesIconSizePixels + kLivesBlockTopPadding;
    layout.fuelY = cursorY;
    cursorY += 20;
    layout.speedY = cursorY;

    layout.mapSize = std::min(layout.contentWidth, kMinimapDisplaySizePixels);
    const int mapMargin = std::max(0, (layout.contentWidth - layout.mapSize) / 2);
    layout.mapX = layout.contentX + mapMargin;
    layout.mapY = screenHeight - mapMargin - layout.mapSize;
    layout.leftBlockSize = (layout.contentWidth - 8) / 2;
    layout.blocksY = layout.mapY - layout.leftBlockSize - mapMargin;
    layout.compassX = layout.contentX + layout.leftBlockSize + 8;
    return layout;
}

void HudPanel::EnsureBoltMetrics(int contentWidth) const {
    if (boltMetricsWidth_ == contentWidth) {
        return;
    }

    boltMetricsWidth_ = contentWidth;
    boltBaseWidth_ = MeasureText("BOLT", kBoltFontSize);
    boltSpacing_ = std::max(1.0F, (static_cast<float>(contentWidth) - static_cast<float>(boltBaseWidth_)) / 3.0F) +
        4.0F;
    staticLayerDirty_ = true;
}

void HudPanel::EnsureStaticLayerTarget(int hudWidth, int screenHeight) const {
    if (hudWidth <= 0 || screenHeight <= 0) {
        return;
    }

    const bool sizeChanged =
        staticLayerTargetLoaded_ && (staticLayerWidth_ != hudWidth || staticLayerHeight_ != screenHeight);
    if (sizeChanged) {
        UnloadRenderTexture(staticLayerTarget_);
        staticLayerTarget_ = RenderTexture2D{};
        staticLayerTargetLoaded_ = false;
        staticLayerWidth_ = 0;
        staticLayerHeight_ = 0;
    }

    if (staticLayerTargetLoaded_) {
        return;
    }

    staticLayerTarget_ = LoadRenderTexture(hudWidth, screenHeight);
    staticLayerTargetLoaded_ = staticLayerTarget_.id != 0;
    if (!staticLayerTargetLoaded_) {
        return;
    }
    SetTextureFilter(staticLayerTarget_.texture, TEXTURE_FILTER_POINT);
    staticLayerWidth_ = hudWidth;
    staticLayerHeight_ = screenHeight;
    staticLayerDirty_ = true;
}

void HudPanel::EnsureMinimapMarkersTarget(int mapSize) const {
    if (mapSize <= 0) {
        return;
    }

    const bool sizeChanged = minimapMarkersTargetLoaded_ && minimapMarkersSize_ != mapSize;
    if (sizeChanged) {
        UnloadRenderTexture(minimapMarkersTarget_);
        minimapMarkersTarget_ = RenderTexture2D{};
        minimapMarkersTargetLoaded_ = false;
        minimapMarkersSize_ = 0;
    }

    if (minimapMarkersTargetLoaded_) {
        return;
    }

    minimapMarkersTarget_ = LoadRenderTexture(mapSize, mapSize);
    minimapMarkersTargetLoaded_ = minimapMarkersTarget_.id != 0;
    if (!minimapMarkersTargetLoaded_) {
        return;
    }
    SetTextureFilter(minimapMarkersTarget_.texture, TEXTURE_FILTER_POINT);
    minimapMarkersSize_ = mapSize;
    minimapMarkersDirty_ = true;
}

void HudPanel::EnsureBasesRadarTarget(int blockSize) const {
    if (blockSize <= 0) {
        return;
    }

    const bool sizeChanged = basesRadarTargetLoaded_ && basesRadarSize_ != blockSize;
    if (sizeChanged) {
        UnloadRenderTexture(basesRadarTarget_);
        basesRadarTarget_ = RenderTexture2D{};
        basesRadarTargetLoaded_ = false;
        basesRadarSize_ = 0;
    }

    if (basesRadarTargetLoaded_) {
        return;
    }

    basesRadarTarget_ = LoadRenderTexture(blockSize, blockSize);
    basesRadarTargetLoaded_ = basesRadarTarget_.id != 0;
    if (!basesRadarTargetLoaded_) {
        return;
    }

    SetTextureFilter(basesRadarTarget_.texture, TEXTURE_FILTER_POINT);
    basesRadarSize_ = blockSize;
    basesRadarDirty_ = true;
    lastBasesRadarUpdateFrame_ = 0;
    cachedBasesRadarQuadrant_ = -2;
}

void HudPanel::EnsureLivesIconTexture() const {
    if (livesIconTextureLoaded_ || livesIconTextureLoadAttempted_) {
        return;
    }
    livesIconTextureLoadAttempted_ = true;

    Image sourceSheet{};
    if (!TryLoadImageFromTextureDirectory(sourceSheet, "sprites.png")) {
        return;
    }
    if (sourceSheet.width != kSpriteSheetColumns * kSpriteSheetCellSize ||
        sourceSheet.height != kSpriteSheetRows * kSpriteSheetCellSize) {
        UnloadImage(sourceSheet);
        return;
    }

    Image playerBodyUp = ExtractSpriteCell(sourceSheet, 0, kPlayerBodyRowIndex, kSpriteSheetCellSize);
    Image playerBarrelUp = ExtractSpriteCell(sourceSheet, 0, kPlayerBarrelRowIndex, kSpriteSheetCellSize);
    const Color playerColor = ColorFromHexRGB(0x00C030U);
    Image playerFrame = CombineCellsXor(playerBodyUp, playerBarrelUp, playerColor);
    ImageResizeNN(&playerFrame, kLivesIconSizePixels, kLivesIconSizePixels);
    livesIconTexture_ = LoadTextureFromImage(playerFrame);
    livesIconTextureLoaded_ = livesIconTexture_.id != 0;
    if (livesIconTextureLoaded_) {
        SetTextureFilter(livesIconTexture_, TEXTURE_FILTER_POINT);
    }

    UnloadImage(playerFrame);
    UnloadImage(playerBarrelUp);
    UnloadImage(playerBodyUp);
    UnloadImage(sourceSheet);
}

void HudPanel::EnsureEnemyCountIconTextures() const {
    if (enemyCountIconTexturesLoadAttempted_) {
        return;
    }
    enemyCountIconTexturesLoadAttempted_ = true;

    Image sourceSheet{};
    if (!TryLoadImageFromTextureDirectory(sourceSheet, "sprites.png")) {
        return;
    }
    if (sourceSheet.width != kSpriteSheetColumns * kSpriteSheetCellSize ||
        sourceSheet.height != kSpriteSheetRows * kSpriteSheetCellSize) {
        UnloadImage(sourceSheet);
        return;
    }

    FillOpaquePixelsColor(sourceSheet, WHITE);
    for (int typeIndex = 0; typeIndex < 4; ++typeIndex) {
        const int sourceRow = kEnemySpriteFirstRowIndex + typeIndex;
        Image iconFrame = ExtractSpriteCell(sourceSheet, 0, sourceRow, kSpriteSheetCellSize);
        ImageResizeNN(&iconFrame, kEnemyCountIconSizePixels, kEnemyCountIconSizePixels);
        enemyCountIconTextures_[static_cast<std::size_t>(typeIndex)] = LoadTextureFromImage(iconFrame);
        enemyCountIconTexturesLoaded_[static_cast<std::size_t>(typeIndex)] =
            enemyCountIconTextures_[static_cast<std::size_t>(typeIndex)].id != 0;
        if (enemyCountIconTexturesLoaded_[static_cast<std::size_t>(typeIndex)]) {
            SetTextureFilter(enemyCountIconTextures_[static_cast<std::size_t>(typeIndex)], TEXTURE_FILTER_POINT);
        }
        UnloadImage(iconFrame);
    }
    UnloadImage(sourceSheet);
}

void HudPanel::RebuildStaticLayer(const AppConfig& config) const {
    if (!staticLayerTargetLoaded_) {
        return;
    }

    const int hudWidth = ComputeHudWidth(config);
    const HudLayout layout = BuildHudLayout(0, hudWidth, config.screenHeight);

    BeginTextureMode(staticLayerTarget_);
    ClearBackground(BLANK);

    DrawRectangle(0, 0, hudWidth, config.screenHeight, kPanelColor);
    DrawLine(0, 0, 0, config.screenHeight, kPanelDividerColor);
    DrawTextEx(
        GetFontDefault(),
        "BOLT",
        Vector2{static_cast<float>(layout.contentX), 8.0F},
        static_cast<float>(kBoltFontSize),
        boltSpacing_,
        PURPLE);

    DrawRectangle(layout.contentX, layout.scoreY, layout.contentWidth, 36, BLACK);
    DrawRectangleLines(layout.contentX, layout.scoreY, layout.contentWidth, 36, RAYWHITE);

    DrawRectangle(layout.contentX, layout.fuelY, layout.contentWidth, 12, DARKGRAY);
    DrawRectangleLines(layout.contentX, layout.fuelY, layout.contentWidth, 12, RAYWHITE);

    DrawRectangle(layout.contentX, layout.speedY, layout.contentWidth, 12, DARKGRAY);
    DrawRectangleLines(layout.contentX, layout.speedY, layout.contentWidth, 12, RAYWHITE);

    DrawRectangle(layout.mapX, layout.mapY, layout.mapSize, layout.mapSize, BLACK);
    DrawRectangleLines(layout.mapX - 1, layout.mapY - 1, layout.mapSize + 2, layout.mapSize + 2, RAYWHITE);

    DrawRectangle(layout.contentX, layout.blocksY, layout.leftBlockSize, layout.leftBlockSize, BLACK);
    DrawRectangleLines(layout.contentX, layout.blocksY, layout.leftBlockSize, layout.leftBlockSize, RAYWHITE);

    DrawRectangle(layout.compassX, layout.blocksY, layout.leftBlockSize, layout.leftBlockSize, kCompassBackgroundColor);
    DrawRectangleLines(layout.compassX, layout.blocksY, layout.leftBlockSize, layout.leftBlockSize, RAYWHITE);
    const int compassPadding = 4;
    DrawRectangle(
        layout.compassX + compassPadding,
        layout.blocksY + compassPadding,
        layout.leftBlockSize - compassPadding * 2,
        layout.leftBlockSize - compassPadding * 2,
        kCompassBackgroundColor);
    DrawCircle(
        layout.compassX + (layout.leftBlockSize / 2),
        layout.blocksY + (layout.leftBlockSize / 2),
        (static_cast<float>(layout.leftBlockSize) / 3.0F) * 1.15F,
        BLACK);

    EndTextureMode();
    staticLayerDirty_ = false;
}

void HudPanel::ResetMinimapMarkersLayer() const {
    if (!minimapMarkersTargetLoaded_) {
        return;
    }

    BeginTextureMode(minimapMarkersTarget_);
    ClearBackground(BLANK);
    EndTextureMode();

    lastMinimapEnemyUpdateFrame_ = 0;
    minimapEntityCursorIndex_ = -kMinimapTrackedBaseCount;
    minimapEntityCellValid_.fill(false);
    minimapMarkersDirty_ = false;
}

void HudPanel::UpdateOneMinimapEntityMarker(const GameState& state) const {
    if (!minimapMarkersTargetLoaded_) {
        return;
    }

    if (minimapMarkersDirty_) {
        ResetMinimapMarkersLayer();
    }

    const int mazeWidthCells = std::max(1, state.world.maze.widthCells);
    const int mazeHeightCells = std::max(1, state.world.maze.heightCells);
    const int mazeCellSizeUnits = std::max(1, state.world.maze.cellSizeUnits);
    const int maxEnemyIndex = std::min(
        static_cast<int>(state.world.enemies.size()) - 1,
        GameplayConstants::kMaxAliveEnemies - 1);
    const int logicalMaxEntityIndex = std::max(-1, maxEnemyIndex);
    if (minimapEntityCursorIndex_ > logicalMaxEntityIndex) {
        minimapEntityCursorIndex_ = -kMinimapTrackedBaseCount;
    }
    const int entityIndex = minimapEntityCursorIndex_;
    const int cacheIndex = entityIndex + kMinimapTrackedBaseCount;
    if (cacheIndex < 0 || cacheIndex >= static_cast<int>(minimapEntityCellValid_.size())) {
        minimapEntityCursorIndex_ = -kMinimapTrackedBaseCount;
        return;
    }

    auto worldToLogicalCellX = [&](float worldX) {
        const int mazeCellX = std::max(
            0,
            std::min(
                mazeWidthCells - 1,
                static_cast<int>(std::floor(worldX / static_cast<float>(mazeCellSizeUnits)))));
        if (mazeWidthCells == 1) {
            return 0;
        }
        return (mazeCellX * (kMinimapLogicalSizePixels - 1)) / (mazeWidthCells - 1);
    };
    auto worldToLogicalCellY = [&](float worldY) {
        const int mazeCellY = std::max(
            0,
            std::min(
                mazeHeightCells - 1,
                static_cast<int>(std::floor(worldY / static_cast<float>(mazeCellSizeUnits)))));
        if (mazeHeightCells == 1) {
            return 0;
        }
        return (mazeCellY * (kMinimapLogicalSizePixels - 1)) / (mazeHeightCells - 1);
    };

    BeginTextureMode(minimapMarkersTarget_);
    if (minimapEntityCellValid_[static_cast<std::size_t>(cacheIndex)]) {
        DrawPixel(
            minimapEntityCellX_[static_cast<std::size_t>(cacheIndex)],
            minimapEntityCellY_[static_cast<std::size_t>(cacheIndex)],
            BLACK);
    }

    bool shouldDrawCurrentEntity = false;
    Color entityColor = BLACK;
    int entityCellX = 0;
    int entityCellY = 0;
    if (entityIndex < 0) {
        const int baseSlot = entityIndex + kMinimapTrackedBaseCount;
        if (baseSlot >= 0 && baseSlot < static_cast<int>(state.world.enemyBases.size())) {
            const EnemyBase& base = state.world.enemyBases[static_cast<std::size_t>(baseSlot)];
            shouldDrawCurrentEntity = true;
            entityColor = base.destroyed ? kDestroyedBaseMapColor : kBaseMapColor;
            entityCellX = worldToLogicalCellX(base.position.x);
            entityCellY = worldToLogicalCellY(base.position.y);
        }
    } else if (entityIndex >= 0 && entityIndex < static_cast<int>(state.world.enemies.size())) {
        const EnemyTank& enemy = state.world.enemies[static_cast<std::size_t>(entityIndex)];
        if (enemy.alive) {
            shouldDrawCurrentEntity = true;
            entityColor = EnemyMapColor(enemy.type);
            entityCellX = worldToLogicalCellX(enemy.position.x);
            entityCellY = worldToLogicalCellY(enemy.position.y);
        }
    }
    if (shouldDrawCurrentEntity) {
        DrawPixel(entityCellX, entityCellY, entityColor);
        minimapEntityCellX_[static_cast<std::size_t>(cacheIndex)] = entityCellX;
        minimapEntityCellY_[static_cast<std::size_t>(cacheIndex)] = entityCellY;
        minimapEntityCellValid_[static_cast<std::size_t>(cacheIndex)] = true;
    } else {
        minimapEntityCellValid_[static_cast<std::size_t>(cacheIndex)] = false;
    }

    EndTextureMode();

    ++minimapEntityCursorIndex_;
    if (minimapEntityCursorIndex_ > logicalMaxEntityIndex) {
        minimapEntityCursorIndex_ = -kMinimapTrackedBaseCount;
    }
}

void HudPanel::UpdateBasesRadarLayer(int blockSize, int highlightedQuadrant) const {
    if (!basesRadarTargetLoaded_ || blockSize <= 0) {
        return;
    }

    BeginTextureMode(basesRadarTarget_);
    ClearBackground(BLANK);
    DrawBasesRadarQuadrantsAt(0, 0, blockSize, highlightedQuadrant);
    EndTextureMode();
    basesRadarDirty_ = false;
    cachedBasesRadarQuadrant_ = highlightedQuadrant;
}

int HudPanel::ComputeHighlightedQuadrant(const GameState& state) {
    int highlightedQuadrant = -1;
    float nearestDistanceSq = 0.0F;
    for (const EnemyBase& base : state.world.enemyBases) {
        if (base.destroyed) {
            continue;
        }
        const float dx = base.position.x - state.world.player.position.x;
        const float dy = base.position.y - state.world.player.position.y;
        const float distanceSq = dx * dx + dy * dy;
        if (highlightedQuadrant == -1 || distanceSq < nearestDistanceSq) {
            nearestDistanceSq = distanceSq;
            const bool right = dx >= 0.0F;
            const bool down = dy >= 0.0F;
            if (!right && !down) {
                highlightedQuadrant = 0;  // top-left
            } else if (right && !down) {
                highlightedQuadrant = 1;  // top-right
            } else if (!right && down) {
                highlightedQuadrant = 2;  // bottom-left
            } else {
                highlightedQuadrant = 3;  // bottom-right
            }
        }
    }
    return highlightedQuadrant;
}

void HudPanel::PrepareRenderTargets(const GameState& state, const AppConfig& config, const FrameInput& input) const {
    (void)input;
    const double nowSeconds = GetTime();
    const std::uint64_t frameIndex = profiling::Profiler::Instance().FrameIndex();
    const bool needEnemySnapshot =
        !cacheInitialized_ || (nowSeconds - lastEnemySnapshotSeconds_) >= kEnemySnapshotIntervalSeconds;
    if (needEnemySnapshot) {
        cachedEnemyCount_ = 0;
        cachedDronesAlive_ = 0;
        cachedTorpedoesAlive_ = 0;
        cachedHuntersAlive_ = 0;
        cachedAssassinsAlive_ = 0;
        for (const EnemyTank& enemy : state.world.enemies) {
            if (!enemy.alive) {
                continue;
            }
            ++cachedEnemyCount_;
            if (enemy.type == EnemyType::Drone) {
                ++cachedDronesAlive_;
            } else if (enemy.type == EnemyType::Torpedo) {
                ++cachedTorpedoesAlive_;
            } else if (enemy.type == EnemyType::Hunter) {
                ++cachedHuntersAlive_;
            } else {
                ++cachedAssassinsAlive_;
            }
        }
        cachedAliveBases_ = game::queries::CountAliveBases(state);
        lastEnemySnapshotSeconds_ = nowSeconds;
    }

    const bool needFuelSnapshot =
        !cacheInitialized_ || (nowSeconds - lastFuelSnapshotSeconds_) >= kFuelSnapshotIntervalSeconds;
    if (needFuelSnapshot) {
        cachedFuel_ = state.world.player.fuel;
        lastFuelSnapshotSeconds_ = nowSeconds;
    }

    cacheInitialized_ = true;

    const int hudWidth = ComputeHudWidth(config);
    const int panelX = config.screenWidth - hudWidth;
    const HudLayout layout = BuildHudLayout(panelX, hudWidth, config.screenHeight);

    EnsureBoltMetrics(layout.contentWidth);
    EnsureStaticLayerTarget(hudWidth, config.screenHeight);
    EnsureMinimapMarkersTarget(kMinimapLogicalSizePixels);
    EnsureBasesRadarTarget(layout.leftBlockSize);
    EnsureLivesIconTexture();
    EnsureEnemyCountIconTextures();

    if (staticLayerTargetLoaded_ && staticLayerDirty_) {
        RebuildStaticLayer(config);
    }

    if (minimapMarkersTargetLoaded_) {
        const bool shouldUpdateEnemyLayer = minimapMarkersDirty_ ||
            frameIndex >= (lastMinimapEnemyUpdateFrame_ + kMinimapEnemyUpdateIntervalFrames);
        if (shouldUpdateEnemyLayer) {
            UpdateOneMinimapEntityMarker(state);
            lastMinimapEnemyUpdateFrame_ = frameIndex;
        }
    }

    const int highlightedQuadrant = ComputeHighlightedQuadrant(state);
    if (basesRadarTargetLoaded_) {
        const bool shouldUpdateBasesRadar = basesRadarDirty_ ||
            highlightedQuadrant != cachedBasesRadarQuadrant_ ||
            frameIndex >= (lastBasesRadarUpdateFrame_ + kBasesRadarUpdateIntervalFrames);
        if (shouldUpdateBasesRadar) {
            UpdateBasesRadarLayer(layout.leftBlockSize, highlightedQuadrant);
            lastBasesRadarUpdateFrame_ = frameIndex;
        }
    } else {
        cachedBasesRadarQuadrant_ = highlightedQuadrant;
    }
}

void HudPanel::DrawPrepared(const GameState& state, const AppConfig& config, const FrameInput& input) const {
    const int hudWidth = ComputeHudWidth(config);
    const int panelX = config.screenWidth - hudWidth;
    const HudLayout layout = BuildHudLayout(panelX, hudWidth, config.screenHeight);

    {
        profiling::ScopedProfile staticScope(profiling::Scope::RenderHudStatic);
        if (staticLayerTargetLoaded_) {
            const Rectangle source{
                .x = 0.0F,
                .y = 0.0F,
                .width = static_cast<float>(staticLayerWidth_),
                .height = -static_cast<float>(staticLayerHeight_),
            };
            const Rectangle destination{
                .x = static_cast<float>(panelX),
                .y = 0.0F,
                .width = static_cast<float>(staticLayerWidth_),
                .height = static_cast<float>(staticLayerHeight_),
            };
            DrawTexturePro(staticLayerTarget_.texture, source, destination, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
        } else {
            DrawRectangle(panelX, 0, hudWidth, config.screenHeight, kPanelColor);
            DrawLine(panelX, 0, panelX, config.screenHeight, kPanelDividerColor);
        }
    }

    {
        profiling::ScopedProfile textScope(profiling::Scope::RenderHudText);
        DrawText(TextFormat("SCORE %04d", state.world.score), layout.contentX + 8, layout.scoreY + 9, 20, RAYWHITE);
    }

    {
        profiling::ScopedProfile livesScope(profiling::Scope::RenderHudLives);
        const int livesToRender = std::max(0, std::min(4, state.world.player.lives));
        const int livesStartX = layout.contentX - 2;
        const Rectangle sourceRect{
            .x = 0.0F,
            .y = 0.0F,
            .width = static_cast<float>(kLivesIconSizePixels),
            .height = static_cast<float>(kLivesIconSizePixels),
        };
        for (int i = 0; i < livesToRender; ++i) {
            if (!livesIconTextureLoaded_) continue;
            const int iconX = livesStartX + (i * (kLivesIconSizePixels + kLivesIconGapPixels));
            const Rectangle destinationRect{
                .x = static_cast<float>(iconX),
                .y = static_cast<float>(layout.livesY),
                .width = static_cast<float>(kLivesIconSizePixels),
                .height = static_cast<float>(kLivesIconSizePixels),
            };
            DrawTexturePro(livesIconTexture_, sourceRect, destinationRect, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
        }
    }

    {
        profiling::ScopedProfile barsScope(profiling::Scope::RenderHudBars);
        const float fuelClamped = std::max(0.0F, std::min(100.0F, cachedFuel_));
        const int barInnerX = layout.contentX + 1;
        const int barInnerY = layout.fuelY + 1;
        const int barInnerWidth = std::max(0, layout.contentWidth - 2);
        const int barInnerHeight = 10;
        const int fuelWidth = static_cast<int>((fuelClamped / 100.0F) * static_cast<float>(barInnerWidth));
        DrawRectangle(barInnerX, barInnerY, fuelWidth, barInnerHeight, ORANGE);

        const float speed = std::sqrt(
            state.world.player.velocity.x * state.world.player.velocity.x +
            state.world.player.velocity.y * state.world.player.velocity.y);
        const float speedNormalized =
            std::max(0.0F, std::min(1.0F, speed / GameplayConstants::kPlayerFullVelocity));
        const int speedWidth = static_cast<int>(speedNormalized * static_cast<float>(barInnerWidth));
        DrawRectangle(layout.contentX + 1, layout.speedY + 1, speedWidth, barInnerHeight, SKYBLUE);
    }

    {
        profiling::ScopedProfile minimapScope(profiling::Scope::RenderHudMinimap);
        constexpr int iconTextGapPixels = 4;
        const int countersY = layout.blocksY - 16;
        const int slotWidth = std::max(1, layout.contentWidth / 5);
        const int baseIconY = countersY;
        const int enemyIconY = countersY - 1;
        char countBuffer[16]{};
        const int counterValues[5] = {
            cachedAliveBases_,
            cachedDronesAlive_,
            cachedTorpedoesAlive_,
            cachedHuntersAlive_,
            cachedAssassinsAlive_,
        };
        const Color counterColors[5] = {
            kBaseMapColor,
            EnemyMapColor(EnemyType::Drone),
            EnemyMapColor(EnemyType::Torpedo),
            EnemyMapColor(EnemyType::Hunter),
            EnemyMapColor(EnemyType::Assassin),
        };
        for (int slotIndex = 0; slotIndex < 5; ++slotIndex) {
            const int slotX = layout.contentX + slotIndex * slotWidth;
            const int iconX = slotX;
            const int countX = iconX + kEnemyCountIconSizePixels + iconTextGapPixels;
            if (slotIndex == 0) {
                DrawRectangle(iconX, baseIconY, kEnemyCountIconSizePixels - 2, kEnemyCountIconSizePixels - 2, kBaseMapColor);
            } else {
                const int enemyTypeIndex = slotIndex - 1;
                if (enemyCountIconTexturesLoaded_[static_cast<std::size_t>(enemyTypeIndex)]) {
                    DrawTexture(
                        enemyCountIconTextures_[static_cast<std::size_t>(enemyTypeIndex)],
                        iconX,
                        enemyIconY,
                        counterColors[slotIndex]);
                } else {
                    DrawRectangle(
                        iconX,
                        enemyIconY + 1,
                        kEnemyCountIconSizePixels,
                        kEnemyCountIconSizePixels - 2,
                        counterColors[slotIndex]);
                }
            }
            std::snprintf(countBuffer, sizeof(countBuffer), "%d", counterValues[slotIndex]);
            DrawText(countBuffer, countX, countersY, 10, counterColors[slotIndex]);
        }

        const float mazeWidth = static_cast<float>(state.world.maze.widthCells * state.world.maze.cellSizeUnits);
        const float mazeHeight = static_cast<float>(state.world.maze.heightCells * state.world.maze.cellSizeUnits);
        const auto mapPixelX = [&](float worldX) {
            const float normalized = mazeWidth > 0.0F ? worldX / mazeWidth : 0.5F;
            return layout.mapX + static_cast<int>(
                std::max(0.0F, std::min(1.0F, normalized)) * static_cast<float>(layout.mapSize - 1));
        };
        const auto mapPixelY = [&](float worldY) {
            const float normalized = mazeHeight > 0.0F ? worldY / mazeHeight : 0.5F;
            return layout.mapY + static_cast<int>(
                std::max(0.0F, std::min(1.0F, normalized)) * static_cast<float>(layout.mapSize - 1));
        };

        if (minimapMarkersTargetLoaded_) {
            const Rectangle source{
                .x = 0.0F,
                .y = 0.0F,
                .width = static_cast<float>(minimapMarkersSize_),
                .height = -static_cast<float>(minimapMarkersSize_),
            };
            const Rectangle destination{
                .x = static_cast<float>(layout.mapX),
                .y = static_cast<float>(layout.mapY),
                .width = static_cast<float>(layout.mapSize),
                .height = static_cast<float>(layout.mapSize),
            };
            DrawTexturePro(minimapMarkersTarget_.texture, source, destination, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
        } else {
            for (const EnemyBase& base : state.world.enemyBases) {
                const int px = mapPixelX(base.position.x);
                const int py = mapPixelY(base.position.y);
                DrawPixel(px, py, base.destroyed ? kDestroyedBaseMapColor : kBaseMapColor);
            }
            int drawnEnemies = 0;
            for (const EnemyTank& enemy : state.world.enemies) {
                if (!enemy.alive) {
                    continue;
                }
                DrawPixel(mapPixelX(enemy.position.x), mapPixelY(enemy.position.y), EnemyMapColor(enemy.type));
                ++drawnEnemies;
                if (drawnEnemies >= GameplayConstants::kMaxAliveEnemies) {
                    break;
                }
            }
        }

        const float normalizedX = mazeWidth > 0.0F ? state.world.player.position.x / mazeWidth : 0.5F;
        const float normalizedY = mazeHeight > 0.0F ? state.world.player.position.y / mazeHeight : 0.5F;
        const int dotX = layout.mapX + static_cast<int>(
            std::max(0.0F, std::min(1.0F, normalizedX)) * static_cast<float>(layout.mapSize - 1));
        const int dotY = layout.mapY + static_cast<int>(
            std::max(0.0F, std::min(1.0F, normalizedY)) * static_cast<float>(layout.mapSize - 1));
        DrawCircle(dotX, dotY, 2.0F, kPlayerMapColor);
    }

    {
        profiling::ScopedProfile compassScope(profiling::Scope::RenderHudCompass);
        const int highlightedQuadrant =
            cachedBasesRadarQuadrant_ >= -1 ? cachedBasesRadarQuadrant_ : ComputeHighlightedQuadrant(state);

        constexpr float joystickAxisRawMax = 32768.0F;
        const float leftRawAxisX = static_cast<float>(input.gamepadAxis0Raw);
        const float leftRawAxisY = static_cast<float>(input.gamepadAxis1Raw);
        const float leftRawMagnitude = std::sqrt(leftRawAxisX * leftRawAxisX + leftRawAxisY * leftRawAxisY);
        const float leftJoystickAmplitude = std::min(1.0F, leftRawMagnitude / joystickAxisRawMax);
        float leftJoystickDirX = 0.0F;
        float leftJoystickDirY = 0.0F;
        if (leftRawMagnitude > 0.0F) {
            leftJoystickDirX = leftRawAxisX / leftRawMagnitude;
            leftJoystickDirY = leftRawAxisY / leftRawMagnitude;
        }

        const float rightRawAxisX = static_cast<float>(input.gamepadAxis2Raw);
        const float rightRawAxisY = static_cast<float>(input.gamepadAxis3Raw);
        const float rightRawMagnitude = std::sqrt(rightRawAxisX * rightRawAxisX + rightRawAxisY * rightRawAxisY);
        const float rightJoystickAmplitude = std::min(1.0F, rightRawMagnitude / joystickAxisRawMax);
        float rightJoystickDirX = 0.0F;
        float rightJoystickDirY = 0.0F;
        if (rightRawMagnitude > 0.0F) {
            rightJoystickDirX = rightRawAxisX / rightRawMagnitude;
            rightJoystickDirY = rightRawAxisY / rightRawMagnitude;
        }

        if (basesRadarTargetLoaded_) {
            const Rectangle source{
                .x = 0.0F,
                .y = 0.0F,
                .width = static_cast<float>(basesRadarSize_),
                .height = -static_cast<float>(basesRadarSize_),
            };
            const Rectangle destination{
                .x = static_cast<float>(layout.contentX),
                .y = static_cast<float>(layout.blocksY),
                .width = static_cast<float>(basesRadarSize_),
                .height = static_cast<float>(basesRadarSize_),
            };
            DrawTexturePro(basesRadarTarget_.texture, source, destination, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
        } else {
            DrawBasesRadarQuadrantsAt(layout.contentX, layout.blocksY, layout.leftBlockSize, highlightedQuadrant);
        }

        const float headingX = std::sin(state.world.player.hullHeadingRadians);
        const float headingY = -std::cos(state.world.player.hullHeadingRadians);
        const int centerX = layout.compassX + layout.leftBlockSize / 2;
        const int centerY = layout.blocksY + layout.leftBlockSize / 2;
        const float armLength = static_cast<float>(layout.leftBlockSize) * 0.28F;
        const int headingToX = static_cast<int>(std::lround(static_cast<float>(centerX) + headingX * armLength));
        const int headingToY = static_cast<int>(std::lround(static_cast<float>(centerY) + headingY * armLength));
        const int leftToX = static_cast<int>(std::lround(
            static_cast<float>(centerX) + leftJoystickDirX * armLength * leftJoystickAmplitude));
        const int leftToY = static_cast<int>(std::lround(
            static_cast<float>(centerY) + leftJoystickDirY * armLength * leftJoystickAmplitude));
        const int rightToX = static_cast<int>(std::lround(
            static_cast<float>(centerX) + rightJoystickDirX * armLength * rightJoystickAmplitude));
        const int rightToY = static_cast<int>(std::lround(
            static_cast<float>(centerY) + rightJoystickDirY * armLength * rightJoystickAmplitude));
        DrawLine(centerX, centerY, headingToX, headingToY, RAYWHITE);
        DrawLine(centerX, centerY, leftToX, leftToY, SKYBLUE);
        DrawLine(centerX, centerY, rightToX, rightToY, RED);

        if (state.world.panModeActive) {
            constexpr int kPanLabelFontSize = 20;
            const int panLabelW = MeasureText("P", kPanLabelFontSize);
            DrawText("P", centerX - panLabelW / 2, centerY - kPanLabelFontSize / 2, kPanLabelFontSize, YELLOW);
        }

        {
            static int hudInvisLogCount = 0;
            if (++hudInvisLogCount <= 10) {
                bolt::log::Debug("[INVIS] HUD compass block invisibility=%d (log #%d)",
                    state.menuSettings.invisibility ? 1 : 0, hudInvisLogCount);
            }
        }
        if (state.menuSettings.invisibility) {
            constexpr int kInvisibilityLabelFontSize = 20;
            constexpr int kInvisibilityLabelPadding = 4;
            const int invisLabelW = MeasureText("I", kInvisibilityLabelFontSize);
            const int invisX = layout.contentX + layout.leftBlockSize - invisLabelW - kInvisibilityLabelPadding;
            const int invisY = layout.blocksY + kInvisibilityLabelPadding;
            DrawText("I", invisX, invisY, kInvisibilityLabelFontSize, YELLOW);
        }

        if (state.world.levelCleared || state.world.levelClearMessageSeconds > 0.0F) {
            DrawText("LEVEL CLEARED", layout.contentX + 6, layout.blocksY - 26, 20, LIME);
        }
    }
}
