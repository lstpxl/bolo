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
constexpr int kHudLeftAccentPx = 2;
constexpr int kContentPadding = 10;
constexpr int kHudStackGap = 12;
constexpr int kHudRadarPairGap = 10;
constexpr int kFuelBarInnerHeightPx = 4;
// Same font as main menu "Beta Version" (MenuScreen: pixuf.ttf, base 16, DrawTextEx size 16).
constexpr int kHudScoreFontBaseSize = 16;
constexpr float kHudScoreFontRenderSize = 16.0F;
constexpr int kScoreFrameOuterHeightPx = 36;
constexpr int kBoltFontSize = 50;
constexpr int kBoltLogoNativeWidth = 140;
constexpr int kBoltLogoNativeHeight = 54;
constexpr int kFrameBorderTotalPx = 8;  // 2px accent + 2px black per side

constexpr Color kHudSurface{27, 31, 40, 255};     // #1b1f28
constexpr Color kHudAccent{64, 69, 79, 255};      // #40454f
constexpr Color kDarkGreen{19, 60, 26, 255};      // #133c1a
constexpr Color kTraceGreen{33, 73, 38, 255};     // #214926
constexpr Color kBrightGreen{3, 199, 3, 255};     // #03c703
constexpr Color kBrandYellow{223, 206, 4, 255};   // #dfce04

constexpr Color kDroneMapColor{66, 190, 143, 255};      // #42BE8F
constexpr Color kTorpedoMapColor{164, 173, 67, 255};    // #A4AD43
constexpr Color kHunterMapColor{221, 145, 67, 255};     // #DD9143
constexpr Color kAssassinMapColor{221, 145, 67, 255};   // #DD9143
constexpr Color kPlayerMapColor = kBrightGreen;
constexpr Color kBaseMapColor{255, 0, 255, 255};        // #FF00FF
constexpr Color kDestroyedBaseMapColor{96, 96, 96, 255};    // #606060
constexpr Color kQuadrantInactiveColor = kDarkGreen;
constexpr Color kQuadrantActiveColor = kBrightGreen;

constexpr int kSpriteSheetColumns = 2;
constexpr int kSpriteSheetRows = 10;
constexpr int kSpriteSheetCellSize = 9;
constexpr int kPlayerBodyRowIndex = 0;
constexpr int kPlayerBarrelRowIndex = 1;
constexpr int kEnemySpriteFirstRowIndex = 3;
/// HUD drone counter uses wander-state art (matches non-`Watch` in-world drone strip).
constexpr int kDroneWanderSpriteRowIndex = 8;

int HudEnemyIconSourceRow(int enemyTypeIndex) {
    if (enemyTypeIndex == 0) {
        return kDroneWanderSpriteRowIndex;
    }
    return kEnemySpriteFirstRowIndex + enemyTypeIndex;
}
constexpr int kEnemyCountIconSizePixels = 10;

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
    constexpr int kOuterMargin = 0;
    constexpr int kQuadGap = 2;
    const int innerW = blockSize - 2 * kOuterMargin;
    const int innerH = blockSize - 2 * kOuterMargin;
    if (innerW < kQuadGap + 2 || innerH < kQuadGap + 2) {
        return;
    }
    const int qW0 = (innerW - kQuadGap) / 2;
    const int qW1 = innerW - kQuadGap - qW0;
    const int qH0 = (innerH - kQuadGap) / 2;
    const int qH1 = innerH - kQuadGap - qH0;
    const int x0 = originX + kOuterMargin;
    const int y0 = originY + kOuterMargin;
    DrawRectangle(x0, y0, qW0, qH0, highlightedQuadrant == 0 ? kQuadrantActiveColor : kQuadrantInactiveColor);
    DrawRectangle(x0 + qW0 + kQuadGap, y0, qW1, qH0, highlightedQuadrant == 1 ? kQuadrantActiveColor : kQuadrantInactiveColor);
    DrawRectangle(x0, y0 + qH0 + kQuadGap, qW0, qH1, highlightedQuadrant == 2 ? kQuadrantActiveColor : kQuadrantInactiveColor);
    DrawRectangle(
        x0 + qW0 + kQuadGap,
        y0 + qH0 + kQuadGap,
        qW1,
        qH1,
        highlightedQuadrant == 3 ? kQuadrantActiveColor : kQuadrantInactiveColor);
}

void DrawHudDoubleFrame(int ox, int oy, int ow, int oh, Color innerFill) {
    if (ow < kFrameBorderTotalPx || oh < kFrameBorderTotalPx) {
        return;
    }
    DrawRectangle(ox + 4, oy + 4, ow - 8, oh - 8, innerFill);
    DrawRectangle(ox, oy, ow, 2, kHudAccent);
    DrawRectangle(ox, oy + oh - 2, ow, 2, kHudAccent);
    DrawRectangle(ox, oy + 2, 2, oh - 4, kHudAccent);
    DrawRectangle(ox + ow - 2, oy + 2, 2, oh - 4, kHudAccent);
    DrawRectangle(ox + 2, oy + 2, ow - 4, 2, BLACK);
    DrawRectangle(ox + 2, oy + oh - 4, ow - 4, 2, BLACK);
    DrawRectangle(ox + 2, oy + 4, 2, oh - 8, BLACK);
    DrawRectangle(ox + ow - 4, oy + 4, 2, oh - 8, BLACK);
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
    if (lifeHudIconsLoaded_) {
        if (lifeHudIconActiveTexture_.id != 0) {
            UnloadTexture(lifeHudIconActiveTexture_);
            lifeHudIconActiveTexture_ = Texture2D{};
        }
        if (lifeHudIconSpentTexture_.id != 0) {
            UnloadTexture(lifeHudIconSpentTexture_);
            lifeHudIconSpentTexture_ = Texture2D{};
        }
        lifeHudIconsLoaded_ = false;
    }
    if (boltLogoTextureLoaded_) {
        UnloadTexture(boltLogoTexture_);
        boltLogoTexture_ = Texture2D{};
        boltLogoTextureLoaded_ = false;
    }
    if (hudScoreFontLoaded_) {
        UnloadFont(hudScoreFont_);
        hudScoreFont_ = Font{};
        hudScoreFontLoaded_ = false;
    }
    for (std::size_t i = 0; i < enemyCountIconTextures_.size(); ++i) {
        if (!enemyCountIconTexturesLoaded_[i]) {
            continue;
        }
        UnloadTexture(enemyCountIconTextures_[i]);
        enemyCountIconTextures_[i] = Texture2D{};
        enemyCountIconTexturesLoaded_[i] = false;
    }

    lifeHudIconLoadAttempted_ = false;
    boltLogoTextureLoadAttempted_ = false;
    hudScoreFontLoadAttempted_ = false;
    enemyCountIconTexturesLoadAttempted_ = false;
    staticLayerWidth_ = 0;
    staticLayerHeight_ = 0;
    minimapMarkersW_ = 0;
    minimapMarkersH_ = 0;
    cachedLayoutMazeW_ = 0;
    cachedLayoutMazeH_ = 0;
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

void HudPanel::ResetTransientState() const {
    cacheInitialized_ = false;
    lastEnemySnapshotSeconds_ = 0.0;
    lastFuelSnapshotSeconds_ = 0.0;

    cachedFuel_ = GameplayConstants::kFuelMax;
    cachedEnemyCount_ = 0;
    cachedAliveBases_ = 0;
    cachedDronesAlive_ = 0;
    cachedTorpedoesAlive_ = 0;
    cachedHuntersAlive_ = 0;
    cachedAssassinsAlive_ = 0;

    minimapMarkersDirty_ = true;
    lastMinimapEnemyUpdateFrame_ = 0;
    minimapEntityCursorIndex_ = -kMinimapTrackedBaseCount;
    minimapEntityCellValid_.fill(false);
    if (minimapMarkersTargetLoaded_) {
        ResetMinimapMarkersLayer();
    }

    basesRadarDirty_ = true;
    lastBasesRadarUpdateFrame_ = 0;
    cachedBasesRadarQuadrant_ = -2;
}

HudPanel::HudLayout HudPanel::BuildHudLayout(
    int panelX, int hudWidth, int screenHeight, int mazeWidthCells, int mazeHeightCells) {
    HudLayout layout{};
    layout.panelX = panelX;
    layout.contentX = panelX + kHudLeftAccentPx + kContentPadding;
    layout.contentWidth = hudWidth - kHudLeftAccentPx - (kContentPadding * 2);

    const int mazeW = std::max(1, mazeWidthCells);
    const int mazeH = std::max(1, mazeHeightCells);
    layout.mapInnerW = mazeW * 2;
    layout.mapInnerH = mazeH * 2;
    layout.mapOuterW = layout.mapInnerW + kFrameBorderTotalPx;
    layout.mapOuterH = layout.mapInnerH + kFrameBorderTotalPx;

    const int sideAndBottomMargin = std::max(0, (hudWidth - layout.mapOuterW) / 2);
    layout.mapOuterX = panelX + sideAndBottomMargin;
    layout.mapOuterY = screenHeight - sideAndBottomMargin - layout.mapOuterH;

    constexpr int kTopMargin = 8;
    constexpr int kMinFlexForCounters = 24;
    constexpr int scoreOuterH = kScoreFrameOuterHeightPx;
    const int boltLogoDrawW = std::min(kBoltLogoNativeWidth, layout.contentWidth);
    const int logoH = std::max(1, (kBoltLogoNativeHeight * boltLogoDrawW) / kBoltLogoNativeWidth);
    constexpr int livesOuterH = kLifeHudIconSizePixels + kLifeHudInnerExtraHeightPx + kFrameBorderTotalPx;
    constexpr int fuelOuterH = kFuelBarInnerHeightPx + kFrameBorderTotalPx;
    constexpr int velocityOuterH = 12 + kFrameBorderTotalPx;

    layout.scoreY = kTopMargin;
    layout.logoY = layout.scoreY + scoreOuterH + kHudStackGap;
    layout.livesY = layout.logoY + logoH + kHudStackGap;
    layout.fuelY = layout.livesY + livesOuterH + kHudStackGap;

    const int topStackEnd = layout.fuelY + fuelOuterH;
    layout.flexTop = topStackEnd + kHudStackGap;

    int blockOuter = (layout.mapOuterW - kHudRadarPairGap) / 2;
    int innerB = std::max(8, blockOuter - kFrameBorderTotalPx);
    blockOuter = innerB + kFrameBorderTotalPx;

    const int minimalVelocityTop = layout.flexTop + kMinFlexForCounters;
    const int maxBlockOuterByHeight =
        layout.mapOuterY - kHudStackGap - velocityOuterH - kHudStackGap - minimalVelocityTop;
    if (blockOuter > maxBlockOuterByHeight && maxBlockOuterByHeight >= 24) {
        blockOuter = maxBlockOuterByHeight;
        innerB = std::max(8, blockOuter - kFrameBorderTotalPx);
        blockOuter = innerB + kFrameBorderTotalPx;
    }

    const int radarRowOuterW = 2 * blockOuter + kHudRadarPairGap;
    const int radarRowPadX = std::max(0, layout.mapOuterW - radarRowOuterW) / 2;

    layout.leftBlockSize = innerB;
    layout.blocksY = layout.mapOuterY - kHudStackGap - blockOuter;
    layout.velocityY = layout.blocksY - kHudStackGap - velocityOuterH;
    layout.flexBottom = layout.velocityY - kHudStackGap;

    layout.leftBlockOuterX = layout.mapOuterX + radarRowPadX;
    layout.compassOuterX = layout.leftBlockOuterX + blockOuter + kHudRadarPairGap;
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

void HudPanel::EnsureBoltLogoTexture() const {
    if (boltLogoTextureLoaded_ || boltLogoTextureLoadAttempted_) {
        return;
    }
    boltLogoTextureLoadAttempted_ = true;

    Image image{};
    if (!TryLoadImageFromTextureDirectory(image, "bolt-logo-140px.png")) {
        return;
    }
    boltLogoTexture_ = LoadTextureFromImage(image);
    UnloadImage(image);
    if (boltLogoTexture_.id == 0) {
        return;
    }
    SetTextureFilter(boltLogoTexture_, TEXTURE_FILTER_POINT);
    boltLogoTextureLoaded_ = true;
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

void HudPanel::EnsureMinimapMarkersTarget(int mapWidthPixels, int mapHeightPixels) const {
    if (mapWidthPixels <= 0 || mapHeightPixels <= 0) {
        return;
    }

    const bool sizeChanged = minimapMarkersTargetLoaded_ &&
        (minimapMarkersW_ != mapWidthPixels || minimapMarkersH_ != mapHeightPixels);
    if (sizeChanged) {
        UnloadRenderTexture(minimapMarkersTarget_);
        minimapMarkersTarget_ = RenderTexture2D{};
        minimapMarkersTargetLoaded_ = false;
        minimapMarkersW_ = 0;
        minimapMarkersH_ = 0;
    }

    if (minimapMarkersTargetLoaded_) {
        return;
    }

    minimapMarkersTarget_ = LoadRenderTexture(mapWidthPixels, mapHeightPixels);
    minimapMarkersTargetLoaded_ = minimapMarkersTarget_.id != 0;
    if (!minimapMarkersTargetLoaded_) {
        return;
    }
    SetTextureFilter(minimapMarkersTarget_.texture, TEXTURE_FILTER_POINT);
    minimapMarkersW_ = mapWidthPixels;
    minimapMarkersH_ = mapHeightPixels;
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

void HudPanel::PreloadHudResources() const {
    EnsureHudScoreFont();
    EnsureLifeHudIconTextures();
}

void HudPanel::EnsureHudScoreFont() const {
    if (hudScoreFontLoaded_ || hudScoreFontLoadAttempted_) {
        return;
    }
    hudScoreFontLoadAttempted_ = true;

    const std::string fontPath = core::resources::ResolveResourcePath("fonts", "pixuf.ttf");
    if (fontPath.empty() || !FileExists(fontPath.c_str())) {
        bolt::log::Warning("HUD: pixuf.ttf not found in resources/fonts (score uses default font)");
        return;
    }
    hudScoreFont_ = LoadFontEx(fontPath.c_str(), kHudScoreFontBaseSize, nullptr, 0);
    if (hudScoreFont_.texture.id == 0) {
        bolt::log::Warning("HUD: failed to load score font from %s", fontPath.c_str());
        hudScoreFont_ = Font{};
        return;
    }
    SetTextureFilter(hudScoreFont_.texture, TEXTURE_FILTER_POINT);
    hudScoreFontLoaded_ = true;
}

void HudPanel::EnsureLifeHudIconTextures() const {
    if (lifeHudIconLoadAttempted_) {
        return;
    }
    lifeHudIconLoadAttempted_ = true;

    auto unloadPartialPngTextures = [&]() {
        if (lifeHudIconActiveTexture_.id != 0) {
            UnloadTexture(lifeHudIconActiveTexture_);
            lifeHudIconActiveTexture_ = Texture2D{};
        }
        if (lifeHudIconSpentTexture_.id != 0) {
            UnloadTexture(lifeHudIconSpentTexture_);
            lifeHudIconSpentTexture_ = Texture2D{};
        }
    };

    Image base{};
    if (TryLoadImageFromTextureDirectory(base, "live-icon-20x20px.png") && base.data != nullptr) {
        Image activeImg = ImageCopy(base);
        FillOpaquePixelsColor(activeImg, kBrightGreen);
        lifeHudIconActiveTexture_ = LoadTextureFromImage(activeImg);
        UnloadImage(activeImg);

        Image spentImg = ImageCopy(base);
        FillOpaquePixelsColor(spentImg, kTraceGreen);
        lifeHudIconSpentTexture_ = LoadTextureFromImage(spentImg);
        UnloadImage(spentImg);
        UnloadImage(base);

        if (lifeHudIconActiveTexture_.id != 0 && lifeHudIconSpentTexture_.id != 0) {
            SetTextureFilter(lifeHudIconActiveTexture_, TEXTURE_FILTER_POINT);
            SetTextureFilter(lifeHudIconSpentTexture_, TEXTURE_FILTER_POINT);
            lifeHudIconsLoaded_ = true;
            return;
        }
        unloadPartialPngTextures();
    } else {
        if (base.data != nullptr) {
            UnloadImage(base);
        }
    }

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
    Image playerFrameActive = CombineCellsXor(playerBodyUp, playerBarrelUp, kBrightGreen);
    ImageResizeNN(&playerFrameActive, kLifeHudIconSizePixels, kLifeHudIconSizePixels);
    lifeHudIconActiveTexture_ = LoadTextureFromImage(playerFrameActive);
    UnloadImage(playerFrameActive);

    Image playerFrameSpent = CombineCellsXor(playerBodyUp, playerBarrelUp, kTraceGreen);
    ImageResizeNN(&playerFrameSpent, kLifeHudIconSizePixels, kLifeHudIconSizePixels);
    lifeHudIconSpentTexture_ = LoadTextureFromImage(playerFrameSpent);
    UnloadImage(playerFrameSpent);

    UnloadImage(playerBarrelUp);
    UnloadImage(playerBodyUp);
    UnloadImage(sourceSheet);

    if (lifeHudIconActiveTexture_.id != 0 && lifeHudIconSpentTexture_.id != 0) {
        SetTextureFilter(lifeHudIconActiveTexture_, TEXTURE_FILTER_POINT);
        SetTextureFilter(lifeHudIconSpentTexture_, TEXTURE_FILTER_POINT);
        lifeHudIconsLoaded_ = true;
    } else {
        unloadPartialPngTextures();
    }
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
        const int sourceRow = HudEnemyIconSourceRow(typeIndex);
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

void HudPanel::RebuildStaticLayer(const AppConfig& config, int mazeWidthCells, int mazeHeightCells) const {
    if (!staticLayerTargetLoaded_) {
        return;
    }

    const int hudWidth = ComputeHudWidth(config);
    const HudLayout layout = BuildHudLayout(0, hudWidth, config.screenHeight, mazeWidthCells, mazeHeightCells);

    BeginTextureMode(staticLayerTarget_);
    ClearBackground(BLANK);

    DrawRectangle(0, 0, kHudLeftAccentPx, config.screenHeight, kHudAccent);
    DrawRectangle(kHudLeftAccentPx, 0, hudWidth - kHudLeftAccentPx, config.screenHeight, kHudSurface);

    const int itemsW = layout.mapOuterW;
    DrawHudDoubleFrame(layout.mapOuterX, layout.scoreY, itemsW, kScoreFrameOuterHeightPx, kDarkGreen);

    if (boltLogoTextureLoaded_) {
        const int drawW = std::min(static_cast<int>(boltLogoTexture_.width), layout.contentWidth);
        const int drawH = std::max(
            1,
            static_cast<int>(boltLogoTexture_.height) * drawW / std::max(1, static_cast<int>(boltLogoTexture_.width)));
        const float dstX = static_cast<float>(layout.contentX) +
            (static_cast<float>(layout.contentWidth) - static_cast<float>(drawW)) * 0.5F;
        const float dstY = static_cast<float>(layout.logoY);
        const Rectangle source{
            .x = 0.0F,
            .y = 0.0F,
            .width = static_cast<float>(boltLogoTexture_.width),
            .height = static_cast<float>(boltLogoTexture_.height),
        };
        const Rectangle destination{
            .x = dstX,
            .y = dstY,
            .width = static_cast<float>(drawW),
            .height = static_cast<float>(drawH),
        };
        DrawTexturePro(boltLogoTexture_, source, destination, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
    } else {
        const Vector2 boltSize =
            MeasureTextEx(GetFontDefault(), "BOLT", static_cast<float>(kBoltFontSize), boltSpacing_);
        const float boltX =
            static_cast<float>(layout.contentX) +
            (static_cast<float>(layout.contentWidth) - boltSize.x) * 0.5F;
        DrawTextEx(
            GetFontDefault(),
            "BOLT",
            Vector2{boltX, static_cast<float>(layout.logoY)},
            static_cast<float>(kBoltFontSize),
            boltSpacing_,
            kBrandYellow);
    }

    DrawHudDoubleFrame(
        layout.mapOuterX,
        layout.livesY,
        itemsW,
        kLifeHudIconSizePixels + kLifeHudInnerExtraHeightPx + kFrameBorderTotalPx,
        kDarkGreen);

    DrawHudDoubleFrame(
        layout.mapOuterX, layout.fuelY, itemsW, kFuelBarInnerHeightPx + kFrameBorderTotalPx, DARKGRAY);

    DrawHudDoubleFrame(layout.mapOuterX, layout.velocityY, itemsW, 12 + kFrameBorderTotalPx, kDarkGreen);

    DrawHudDoubleFrame(layout.mapOuterX, layout.mapOuterY, layout.mapOuterW, layout.mapOuterH, BLACK);

    const int blockOuter = layout.leftBlockSize + kFrameBorderTotalPx;
    DrawHudDoubleFrame(layout.leftBlockOuterX, layout.blocksY, blockOuter, blockOuter, BLACK);
    DrawHudDoubleFrame(layout.compassOuterX, layout.blocksY, blockOuter, blockOuter, kDarkGreen);

    const int compassInnerX = layout.compassOuterX + 4;
    const int compassInnerY = layout.blocksY + 4;
    const int compassPadding = 4;
    DrawRectangle(
        compassInnerX + compassPadding,
        compassInnerY + compassPadding,
        layout.leftBlockSize - compassPadding * 2,
        layout.leftBlockSize - compassPadding * 2,
        kDarkGreen);
    DrawCircle(
        compassInnerX + (layout.leftBlockSize / 2),
        compassInnerY + (layout.leftBlockSize / 2),
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

    auto worldToMarkerCorner = [&](float worldX, float worldY, int& outPixelX, int& outPixelY) {
        const int mazeCellX = std::max(
            0,
            std::min(
                mazeWidthCells - 1,
                static_cast<int>(std::floor(worldX / static_cast<float>(mazeCellSizeUnits)))));
        const int mazeCellY = std::max(
            0,
            std::min(
                mazeHeightCells - 1,
                static_cast<int>(std::floor(worldY / static_cast<float>(mazeCellSizeUnits)))));
        outPixelX = mazeCellX * 2;
        outPixelY = mazeCellY * 2;
    };

    BeginTextureMode(minimapMarkersTarget_);
    if (minimapEntityCellValid_[static_cast<std::size_t>(cacheIndex)]) {
        DrawRectangle(
            minimapEntityCellX_[static_cast<std::size_t>(cacheIndex)],
            minimapEntityCellY_[static_cast<std::size_t>(cacheIndex)],
            2,
            2,
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
            worldToMarkerCorner(base.position.x, base.position.y, entityCellX, entityCellY);
        }
    } else if (entityIndex >= 0 && entityIndex < static_cast<int>(state.world.enemies.size())) {
        const EnemyTank& enemy = state.world.enemies[static_cast<std::size_t>(entityIndex)];
        if (enemy.alive) {
            shouldDrawCurrentEntity = true;
            entityColor = EnemyMapColor(enemy.type);
            worldToMarkerCorner(enemy.position.x, enemy.position.y, entityCellX, entityCellY);
        }
    }
    if (shouldDrawCurrentEntity) {
        DrawRectangle(entityCellX, entityCellY, 2, 2, entityColor);
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
    const int mazeW = std::max(1, state.world.maze.widthCells);
    const int mazeH = std::max(1, state.world.maze.heightCells);
    if (mazeW != cachedLayoutMazeW_ || mazeH != cachedLayoutMazeH_) {
        staticLayerDirty_ = true;
        minimapMarkersDirty_ = true;
        cachedLayoutMazeW_ = mazeW;
        cachedLayoutMazeH_ = mazeH;
    }

    const HudLayout layout = BuildHudLayout(panelX, hudWidth, config.screenHeight, mazeW, mazeH);

    EnsureBoltLogoTexture();
    EnsureBoltMetrics(layout.contentWidth);
    EnsureStaticLayerTarget(hudWidth, config.screenHeight);
    EnsureMinimapMarkersTarget(mazeW * 2, mazeH * 2);
    EnsureBasesRadarTarget(layout.leftBlockSize);
    EnsureLifeHudIconTextures();
    EnsureEnemyCountIconTextures();

    if (staticLayerTargetLoaded_ && staticLayerDirty_) {
        RebuildStaticLayer(config, mazeW, mazeH);
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
    const int mazeW = std::max(1, state.world.maze.widthCells);
    const int mazeH = std::max(1, state.world.maze.heightCells);
    const HudLayout layout = BuildHudLayout(panelX, hudWidth, config.screenHeight, mazeW, mazeH);

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
            DrawRectangle(panelX, 0, kHudLeftAccentPx, config.screenHeight, kHudAccent);
            DrawRectangle(
                panelX + kHudLeftAccentPx,
                0,
                hudWidth - kHudLeftAccentPx,
                config.screenHeight,
                kHudSurface);
        }
    }

    {
        profiling::ScopedProfile textScope(profiling::Scope::RenderHudText);
        if (!hudScoreFontLoaded_) {
            EnsureHudScoreFont();
        }
        char scoreText[32]{};
        std::snprintf(scoreText, sizeof(scoreText), "SCORE %04d", state.world.score);
        const int scoreInnerLeft = layout.mapOuterX + 4;
        const int scoreInnerW = layout.mapOuterW - 8;
        const float scoreInnerTop = static_cast<float>(layout.scoreY + 4);
        const float scoreInnerH =
            static_cast<float>(kScoreFrameOuterHeightPx - kFrameBorderTotalPx);
        if (hudScoreFontLoaded_) {
            const Vector2 textSize =
                MeasureTextEx(hudScoreFont_, scoreText, kHudScoreFontRenderSize, 0.0F);
            const float innerLeftF = static_cast<float>(scoreInnerLeft);
            const float innerWF = static_cast<float>(scoreInnerW);
            const float textX = innerLeftF + std::max(0.0F, (innerWF - textSize.x) * 0.5F);
            const float textY = scoreInnerTop + std::max(0.0F, (scoreInnerH - textSize.y) * 0.5F);
            DrawTextEx(
                hudScoreFont_,
                scoreText,
                Vector2{textX, textY},
                kHudScoreFontRenderSize,
                0.0F,
                kBrightGreen);
        } else {
            constexpr int kFallbackScoreFontPx = 20;
            const int textW = MeasureText(scoreText, kFallbackScoreFontPx);
            const int textX = scoreInnerLeft + std::max(0, (scoreInnerW - textW) / 2);
            const int textY = static_cast<int>(scoreInnerTop) +
                std::max(0, static_cast<int>(scoreInnerH) - kFallbackScoreFontPx) / 2;
            DrawText(scoreText, textX, textY, kFallbackScoreFontPx, kBrightGreen);
        }
    }

    {
        profiling::ScopedProfile livesScope(profiling::Scope::RenderHudLives);
        if (!lifeHudIconsLoaded_) {
            EnsureLifeHudIconTextures();
        }
        const int livesRemaining =
            std::max(0, std::min(kLifeHudMaxSlots, state.world.player.lives));
        const int livesInnerTop = layout.livesY + 4;
        const int livesInnerHeight = kLifeHudIconSizePixels + kLifeHudInnerExtraHeightPx;
        const int livesStartX = layout.mapOuterX + 4 + kLifeHudInnerPaddingHPx;
        const int iconY = livesInnerTop + (livesInnerHeight - kLifeHudIconSizePixels) / 2;
        for (int i = 0; i < kLifeHudMaxSlots; ++i) {
            const Texture2D& iconTex = (i < livesRemaining) ? lifeHudIconActiveTexture_ : lifeHudIconSpentTexture_;
            if (iconTex.id == 0) {
                continue;
            }
            const int iconX = livesStartX + (i * (kLifeHudIconSizePixels + kLivesIconGapPixels));
            const Rectangle sourceRect{
                .x = 0.0F,
                .y = 0.0F,
                .width = static_cast<float>(iconTex.width),
                .height = static_cast<float>(iconTex.height),
            };
            const Rectangle destinationRect{
                .x = static_cast<float>(iconX),
                .y = static_cast<float>(iconY),
                .width = static_cast<float>(kLifeHudIconSizePixels),
                .height = static_cast<float>(kLifeHudIconSizePixels),
            };
            DrawTexturePro(iconTex, sourceRect, destinationRect, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
        }
    }

    {
        profiling::ScopedProfile barsScope(profiling::Scope::RenderHudBars);
        const float fuelClamped = std::max(0.0F, std::min(100.0F, cachedFuel_));
        const int barInnerX = layout.mapOuterX + 4;
        const int barInnerY = layout.fuelY + 4;
        const int barInnerWidth = std::max(0, layout.mapOuterW - 8);
        constexpr int kVelocityBarInnerHeightPx = 12;
        const int fuelWidth = static_cast<int>((fuelClamped / 100.0F) * static_cast<float>(barInnerWidth));
        DrawRectangle(barInnerX, barInnerY, fuelWidth, kFuelBarInnerHeightPx, kBrandYellow);

        const float speed = std::sqrt(
            state.world.player.velocity.x * state.world.player.velocity.x +
            state.world.player.velocity.y * state.world.player.velocity.y);
        const float speedNormalized =
            std::max(0.0F, std::min(1.0F, speed / GameplayConstants::kPlayerFullVelocity));
        const int speedWidth = static_cast<int>(speedNormalized * static_cast<float>(barInnerWidth));
        constexpr int kVelocityTipWidthPx = 4;
        const int velBarY = layout.velocityY + 4;
        if (speedWidth > 0) {
            if (speedWidth <= kVelocityTipWidthPx) {
                DrawRectangle(barInnerX, velBarY, speedWidth, kVelocityBarInnerHeightPx, kBrightGreen);
            } else {
                const int traceW = speedWidth - kVelocityTipWidthPx;
                DrawRectangle(barInnerX, velBarY, traceW, kVelocityBarInnerHeightPx, kTraceGreen);
                DrawRectangle(
                    barInnerX + traceW,
                    velBarY,
                    kVelocityTipWidthPx,
                    kVelocityBarInnerHeightPx,
                    kBrightGreen);
            }
        }
    }

    {
        profiling::ScopedProfile minimapScope(profiling::Scope::RenderHudMinimap);
        constexpr int iconTextGapPixels = 4;
        const int flexMidY = layout.flexTop + std::max(0, (layout.flexBottom - layout.flexTop - 16) / 2);
        const int countersY = flexMidY;
        const int slotWidth = std::max(1, layout.mapOuterW / 5);
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
            const int slotX = layout.mapOuterX + slotIndex * slotWidth;
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
        const int mapInnerX = layout.mapOuterX + 4;
        const int mapInnerY = layout.mapOuterY + 4;
        const auto mapPixelX = [&](float worldX) {
            const float normalized = mazeWidth > 0.0F ? worldX / mazeWidth : 0.5F;
            return mapInnerX + static_cast<int>(
                std::max(0.0F, std::min(1.0F, normalized)) * static_cast<float>(layout.mapInnerW - 1));
        };
        const auto mapPixelY = [&](float worldY) {
            const float normalized = mazeHeight > 0.0F ? worldY / mazeHeight : 0.5F;
            return mapInnerY + static_cast<int>(
                std::max(0.0F, std::min(1.0F, normalized)) * static_cast<float>(layout.mapInnerH - 1));
        };

        if (minimapMarkersTargetLoaded_) {
            const Rectangle source{
                .x = 0.0F,
                .y = 0.0F,
                .width = static_cast<float>(minimapMarkersW_),
                .height = -static_cast<float>(minimapMarkersH_),
            };
            const Rectangle destination{
                .x = static_cast<float>(mapInnerX),
                .y = static_cast<float>(mapInnerY),
                .width = static_cast<float>(layout.mapInnerW),
                .height = static_cast<float>(layout.mapInnerH),
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
        const int dotX = mapInnerX + static_cast<int>(
            std::max(0.0F, std::min(1.0F, normalizedX)) * static_cast<float>(layout.mapInnerW - 1));
        const int dotY = mapInnerY + static_cast<int>(
            std::max(0.0F, std::min(1.0F, normalizedY)) * static_cast<float>(layout.mapInnerH - 1));
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

        const int radarInnerX = layout.leftBlockOuterX + 4;
        const int radarInnerY = layout.blocksY + 4;
        const int compassInnerX = layout.compassOuterX + 4;
        const int compassInnerY = layout.blocksY + 4;

        if (basesRadarTargetLoaded_) {
            const Rectangle source{
                .x = 0.0F,
                .y = 0.0F,
                .width = static_cast<float>(basesRadarSize_),
                .height = -static_cast<float>(basesRadarSize_),
            };
            const Rectangle destination{
                .x = static_cast<float>(radarInnerX),
                .y = static_cast<float>(radarInnerY),
                .width = static_cast<float>(layout.leftBlockSize),
                .height = static_cast<float>(layout.leftBlockSize),
            };
            DrawTexturePro(basesRadarTarget_.texture, source, destination, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
        } else {
            DrawBasesRadarQuadrantsAt(radarInnerX, radarInnerY, layout.leftBlockSize, highlightedQuadrant);
        }

        const float headingX = std::sin(state.world.player.hullHeadingRadians);
        const float headingY = -std::cos(state.world.player.hullHeadingRadians);
        const int centerX = compassInnerX + layout.leftBlockSize / 2;
        const int centerY = compassInnerY + layout.leftBlockSize / 2;
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
            DrawText("P", centerX - panLabelW / 2, centerY - kPanLabelFontSize / 2, kPanLabelFontSize, kBrandYellow);
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
            const int blockOuter = layout.leftBlockSize + kFrameBorderTotalPx;
            const int invisLabelW = MeasureText("I", kInvisibilityLabelFontSize);
            const int invisX =
                layout.compassOuterX + blockOuter - invisLabelW - kInvisibilityLabelPadding;
            const int invisY = layout.blocksY + kInvisibilityLabelPadding;
            DrawText("I", invisX, invisY, kInvisibilityLabelFontSize, kBrandYellow);
        }

        if (state.world.levelCleared || state.world.levelClearMessageSeconds > 0.0F) {
            DrawText("LEVEL CLEARED", layout.mapOuterX + 6, layout.blocksY - 26, 20, kBrightGreen);
        }
    }
}
