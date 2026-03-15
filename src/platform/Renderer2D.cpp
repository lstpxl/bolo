#include "platform/Renderer2D.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include "core/Log.h"
#include "core/Profiling.h"
#include "core/ResourceLocator.h"
#include "game/geometry/WorldGeometry.h"
#include "game/systems/EnemySystem.h"
#include "raylib.h"

namespace {
constexpr int kSpriteSheetColumns = 2;
constexpr int kSpriteSheetRows = 7;
constexpr int kSpriteSheetCellSize = 9;
constexpr int kPlayerBodyRowIndex = 0;
constexpr int kPlayerBarrelRowIndex = 1;
constexpr int kEnemySpriteSheetCellSize = 9;
constexpr int kEnemySpriteFirstRowIndex = 3;
constexpr int kTankSpriteRenderScale = 2;
constexpr int kPlayerRenderSizePx = kSpriteSheetCellSize * kTankSpriteRenderScale;
constexpr int kEnemyRenderSizePx = kEnemySpriteSheetCellSize * kTankSpriteRenderScale;

constexpr Color ColorFromHexRGB(std::uint32_t hex) {
    return Color{
        static_cast<unsigned char>((hex >> 16U) & 0xFFU),
        static_cast<unsigned char>((hex >> 8U) & 0xFFU),
        static_cast<unsigned char>(hex & 0xFFU),
        255,
    };
}

constexpr std::uint32_t kBackgroundHex = 0x000000;
constexpr std::uint32_t kWallsHex = 0xCCCCCC;
constexpr std::uint32_t kDestroyedBaseHex = 0x404040;
constexpr std::uint32_t kPlayerHex = 0x00C030;
constexpr std::uint32_t kDroneHex = 0xA0FF00;
constexpr std::uint32_t kTorpedoHex = 0xFFFF00;
constexpr std::uint32_t kHunterHex = 0xFFA500;
constexpr std::uint32_t kAssassinHex = 0xFF6500;
constexpr std::uint32_t kEnemyBaseShellHex = 0xA050A0;
constexpr std::uint32_t kEnemyBaseHex = 0xFF00FF;
constexpr std::uint32_t kPlayerShellHex = 0xFFFFFF;
constexpr std::uint32_t kEnemyShellHex = 0xFFB000;

constexpr Color kBackgroundColor = ColorFromHexRGB(kBackgroundHex);
constexpr Color kWallsColor = ColorFromHexRGB(kWallsHex);
constexpr Color kDestroyedBaseColor = ColorFromHexRGB(kDestroyedBaseHex);
constexpr Color kPlayerColor = ColorFromHexRGB(kPlayerHex);
constexpr Color kDroneColor = ColorFromHexRGB(kDroneHex);
constexpr Color kTorpedoColor = ColorFromHexRGB(kTorpedoHex);
constexpr Color kHunterColor = ColorFromHexRGB(kHunterHex);
constexpr Color kAssassinColor = ColorFromHexRGB(kAssassinHex);
constexpr Color kEnemyBaseShellColor = ColorFromHexRGB(kEnemyBaseShellHex);
constexpr Color kEnemyBaseColor = ColorFromHexRGB(kEnemyBaseHex);
constexpr Color kPlayerShellColor = ColorFromHexRGB(kPlayerShellHex);
constexpr Color kEnemyShellColor = ColorFromHexRGB(kEnemyShellHex);
constexpr float kEnemyRenderCullMarginUnits = 2.0F;
constexpr float kProjectileRenderCullMarginUnits = 1.0F;
constexpr int kProjectileRenderSizePixels = 3;
constexpr int kProjectileRenderHalfSizePixels = kProjectileRenderSizePixels / 2;

// Pre-baked base sprite dimensions (must match the math in the old DrawWorld loop).
constexpr int kBaseSizePx = static_cast<int>(GameplayConstants::kEnemyBaseSizeUnits) * GameplayConstants::kPixelsPerUnit;
constexpr int kBaseHalfPx = kBaseSizePx / 2;
constexpr int kBaseHolePx = GameplayConstants::kPixelsPerUnit + 8;
constexpr int kBaseHoleHalfPx = kBaseHolePx / 2;
constexpr int kBaseHoleOffsetPx = kBaseHalfPx - kBaseHoleHalfPx;
constexpr int kBaseCoreDiameterPx = (kBaseHolePx - 10 > 2) ? (kBaseHolePx - 10) : 2;
constexpr int kBaseCoreRadiusPx = kBaseCoreDiameterPx / 2;

Vector2 SnapWorldToPixelGrid(const Vec2f& worldPosition) {
    const float pixelsPerUnit = static_cast<float>(GameplayConstants::kPixelsPerUnit);
    return Vector2{
        std::round(worldPosition.x * pixelsPerUnit) / pixelsPerUnit,
        std::round(worldPosition.y * pixelsPerUnit) / pixelsPerUnit,
    };
}

int RoundToInt(float value) {
    return static_cast<int>(std::round(value));
}


Color EnemyColorForType(EnemyType type) {
    if (type == EnemyType::Drone) {
        return kDroneColor;
    }
    if (type == EnemyType::Torpedo) {
        return kTorpedoColor;
    }
    if (type == EnemyType::Hunter) {
        return kHunterColor;
    }
    return kAssassinColor;
}

Vector2 WorldToSnappedScreen(const Vec2f& worldPosition, const Camera2D& camera) {
    const Vector2 snappedWorld = SnapWorldToPixelGrid(worldPosition);
    const Vector2 screen = GetWorldToScreen2D(snappedWorld, camera);
    return Vector2{
        static_cast<float>(RoundToInt(screen.x)),
        static_cast<float>(RoundToInt(screen.y)),
    };
}

void DrawFlowFieldArrow(const Camera2D& camera, float fromX, float fromY, float toX, float toY, float cellSizeUnits) {
    const Vector2 fromScreen = GetWorldToScreen2D(Vector2{fromX, fromY}, camera);
    const Vector2 toScreen = GetWorldToScreen2D(Vector2{toX, toY}, camera);
    const Vector2 delta{
        toScreen.x - fromScreen.x,
        toScreen.y - fromScreen.y,
    };
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (length <= 0.001F) {
        return;
    }

    const Vector2 dir{
        delta.x / length,
        delta.y / length,
    };
    const Vector2 perp{
        -dir.y,
        dir.x,
    };
    const float arrowLenPx = std::min(
        std::max(cellSizeUnits * camera.zoom * 0.35F, 4.0F),
        length * 0.75F);
    const Vector2 tail{
        fromScreen.x - dir.x * arrowLenPx * 0.2F,
        fromScreen.y - dir.y * arrowLenPx * 0.2F,
    };
    const Vector2 tip{
        fromScreen.x + dir.x * arrowLenPx * 0.8F,
        fromScreen.y + dir.y * arrowLenPx * 0.8F,
    };

    constexpr Color kFlowArrowColor{24, 50, 64, 180};
    const float headSize = std::clamp(arrowLenPx * 0.22F, 2.0F, 6.0F);
    DrawLineEx(tail, tip, 1.0F, kFlowArrowColor);
    DrawTriangle(
        tip,
        Vector2{
            tip.x - dir.x * headSize - perp.x * headSize * 0.6F,
            tip.y - dir.y * headSize - perp.y * headSize * 0.6F,
        },
        Vector2{
            tip.x - dir.x * headSize + perp.x * headSize * 0.6F,
            tip.y - dir.y * headSize + perp.y * headSize * 0.6F,
        },
        kFlowArrowColor);
}

bool IsWorldPointVisible(
    const Vec2f& point,
    float visibleLeft,
    float visibleRight,
    float visibleTop,
    float visibleBottom,
    float marginUnits) {
    return point.x >= (visibleLeft - marginUnits) &&
        point.x <= (visibleRight + marginUnits) &&
        point.y >= (visibleTop - marginUnits) &&
        point.y <= (visibleBottom + marginUnits);
}

void DrawHorizontalWallPixels(Vector2 a, Vector2 b, int thicknessPixels, Color color) {
    const int x1 = RoundToInt(std::min(a.x, b.x));
    const int x2 = RoundToInt(std::max(a.x, b.x));
    const int y = RoundToInt(a.y) - (thicknessPixels / 2);
    const int width = std::max(1, x2 - x1);
    DrawRectangle(x1, y, width, thicknessPixels, color);
}

void DrawVerticalWallPixels(Vector2 a, Vector2 b, int thicknessPixels, Color color) {
    const int y1 = RoundToInt(std::min(a.y, b.y));
    const int y2 = RoundToInt(std::max(a.y, b.y));
    const int x = RoundToInt(a.x) - (thicknessPixels / 2);
    const int height = std::max(1, y2 - y1);
    DrawRectangle(x, y1, thicknessPixels, height, color);
}

int PlayerFrameIndexFromHeading(float headingRadians, int frameCount) {
    const float twoPi = PI * 2.0F;
    float normalized = std::fmod(headingRadians, twoPi);
    if (normalized < 0.0F) {
        normalized += twoPi;
    }
    const float frameFloat = normalized / twoPi * static_cast<float>(frameCount);
    int frameIndex = static_cast<int>(std::round(frameFloat)) % frameCount;
    if (frameIndex < 0) {
        frameIndex += frameCount;
    }
    return frameIndex;
}

Vector2 ComputeFramePivotOffsetPixels(const Image& spriteSheet, int frameIndex, int frameSizePx) {
    const int frameStartX = frameIndex * frameSizePx;
    constexpr unsigned char alphaThreshold = 128;
    int minX = frameSizePx;
    int minY = frameSizePx;
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < frameSizePx; ++y) {
        for (int x = 0; x < frameSizePx; ++x) {
            const Color pixel = GetImageColor(spriteSheet, frameStartX + x, y);
            if (pixel.a < alphaThreshold) {
                continue;
            }
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }

    if (maxX < minX || maxY < minY) {
        return Vector2{0.0F, 0.0F};
    }

    const float frameCenter = (static_cast<float>(frameSizePx) - 1.0F) * 0.5F;
    const float contentCenterX = (static_cast<float>(minX + maxX)) * 0.5F;
    const float contentCenterY = (static_cast<float>(minY + maxY)) * 0.5F;
    return Vector2{
        std::round(frameCenter - contentCenterX),
        std::round(frameCenter - contentCenterY),
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

Image ExtractSpriteCell(const Image& spriteSheet, int columnIndex, int rowIndex, int cellSizePx) {
    const Rectangle sourceRect{
        .x = static_cast<float>(columnIndex * cellSizePx),
        .y = static_cast<float>(rowIndex * cellSizePx),
        .width = static_cast<float>(cellSizePx),
        .height = static_cast<float>(cellSizePx),
    };
    return ImageFromImage(spriteSheet, sourceRect);
}

void DrawSpriteCell(Image& destination, const Image& cellImage, int columnIndex, int rowIndex, int cellSizePx) {
    const Rectangle sourceRect{
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(cellSizePx),
        .height = static_cast<float>(cellSizePx),
    };
    const Rectangle destinationRect{
        .x = static_cast<float>(columnIndex * cellSizePx),
        .y = static_cast<float>(rowIndex * cellSizePx),
        .width = static_cast<float>(cellSizePx),
        .height = static_cast<float>(cellSizePx),
    };
    ImageDraw(&destination, cellImage, sourceRect, destinationRect, WHITE);
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

int EnemyTypeIndex(EnemyType type) {
    if (type == EnemyType::Drone) {
        return 0;
    }
    if (type == EnemyType::Torpedo) {
        return 1;
    }
    if (type == EnemyType::Hunter) {
        return 2;
    }
    return 3;
}
}  // namespace

bool Renderer2D::LoadResources() {
    UnloadResources();
    for (Vector2& offset : playerTankFrameOffsetsPixels_) {
        offset = Vector2{0.0F, 0.0F};
    }
    Image sourceSheet{};
    if (!TryLoadImageFromTextureDirectory(sourceSheet, "sprites.png")) {
        bolt::log::Warning("RENDER: sprites.png not found");
        return false;
    }

    if (sourceSheet.width != kSpriteSheetColumns * kSpriteSheetCellSize ||
        sourceSheet.height != kSpriteSheetRows * kSpriteSheetCellSize) {
        bolt::log::Warning(
            "RENDER: sprites.png has unexpected size (%i x %i), expected %i x %i",
            sourceSheet.width,
            sourceSheet.height,
            kSpriteSheetColumns * kSpriteSheetCellSize,
            kSpriteSheetRows * kSpriteSheetCellSize);
        UnloadImage(sourceSheet);
        return false;
    }

    Image playerBodyUp = ExtractSpriteCell(sourceSheet, 0, kPlayerBodyRowIndex, kSpriteSheetCellSize);
    Image playerBody45 = ExtractSpriteCell(sourceSheet, 1, kPlayerBodyRowIndex, kSpriteSheetCellSize);
    Image playerBarrelUp = ExtractSpriteCell(sourceSheet, 0, kPlayerBarrelRowIndex, kSpriteSheetCellSize);
    Image playerBarrel45 = ExtractSpriteCell(sourceSheet, 1, kPlayerBarrelRowIndex, kSpriteSheetCellSize);
    Image playerFrame0 = CombineCellsXor(playerBodyUp, playerBarrelUp, kPlayerColor);
    Image playerFrame1 = CombineCellsXor(playerBody45, playerBarrel45, kPlayerColor);
    Image playerFrame2 = ImageCopy(playerFrame0);
    ImageRotateCW(&playerFrame2);
    Image playerFrame3 = ImageCopy(playerFrame1);
    ImageRotateCW(&playerFrame3);
    Image playerFrame4 = ImageCopy(playerFrame2);
    ImageRotateCW(&playerFrame4);
    Image playerFrame5 = ImageCopy(playerFrame3);
    ImageRotateCW(&playerFrame5);
    Image playerFrame6 = ImageCopy(playerFrame4);
    ImageRotateCW(&playerFrame6);
    Image playerFrame7 = ImageCopy(playerFrame5);
    ImageRotateCW(&playerFrame7);

    Image playerSheet = GenImageColor(
        kEnemyTankDirectionCount * kSpriteSheetCellSize,
        kSpriteSheetCellSize,
        BLANK);
    DrawSpriteCell(playerSheet, playerFrame0, 0, 0, kSpriteSheetCellSize);
    DrawSpriteCell(playerSheet, playerFrame1, 1, 0, kSpriteSheetCellSize);
    DrawSpriteCell(playerSheet, playerFrame2, 2, 0, kSpriteSheetCellSize);
    DrawSpriteCell(playerSheet, playerFrame3, 3, 0, kSpriteSheetCellSize);
    DrawSpriteCell(playerSheet, playerFrame4, 4, 0, kSpriteSheetCellSize);
    DrawSpriteCell(playerSheet, playerFrame5, 5, 0, kSpriteSheetCellSize);
    DrawSpriteCell(playerSheet, playerFrame6, 6, 0, kSpriteSheetCellSize);
    DrawSpriteCell(playerSheet, playerFrame7, 7, 0, kSpriteSheetCellSize);

    playerTankSheet_ = LoadTextureFromImage(playerSheet);
    playerTankSheetLoaded_ = playerTankSheet_.id != 0;
    if (playerTankSheetLoaded_) {
        SetTextureFilter(playerTankSheet_, TEXTURE_FILTER_POINT);
        playerTankFrameSizePx_ = kSpriteSheetCellSize;
        playerTankFrameCount_ = kEnemyTankDirectionCount;
        const int frameCountToMeasure =
            std::min(playerTankFrameCount_, static_cast<int>(playerTankFrameOffsetsPixels_.size()));
        for (int frameIndex = 0; frameIndex < frameCountToMeasure; ++frameIndex) {
            playerTankFrameOffsetsPixels_[static_cast<std::size_t>(frameIndex)] =
                ComputeFramePivotOffsetPixels(playerSheet, frameIndex, playerTankFrameSizePx_);
        }
    } else {
        bolt::log::Warning("RENDER: failed to create player spritesheet texture from sprites.png");
    }

    Image enemySheet = GenImageColor(
        kEnemyTankDirectionCount * kEnemySpriteSheetCellSize,
        kEnemyTankTypeCount * kEnemySpriteSheetCellSize,
        BLANK);
    FillOpaquePixelsColor(sourceSheet, WHITE);
    const std::array<Color, kEnemyTankTypeCount> kEnemyTypeColors{
        kDroneColor, kTorpedoColor, kHunterColor, kAssassinColor};
    for (int typeIndex = 0; typeIndex < kEnemyTankTypeCount; ++typeIndex) {
        const int sourceRow = kEnemySpriteFirstRowIndex + typeIndex;
        Image frame0 = ExtractSpriteCell(sourceSheet, 0, sourceRow, kEnemySpriteSheetCellSize);
        Image frame1 = ExtractSpriteCell(sourceSheet, 1, sourceRow, kEnemySpriteSheetCellSize);
        Image frame2 = ImageCopy(frame0);
        ImageRotateCW(&frame2);
        Image frame3 = ImageCopy(frame1);
        ImageRotateCW(&frame3);
        Image frame4 = ImageCopy(frame2);
        ImageRotateCW(&frame4);
        Image frame5 = ImageCopy(frame3);
        ImageRotateCW(&frame5);
        Image frame6 = ImageCopy(frame4);
        ImageRotateCW(&frame6);
        Image frame7 = ImageCopy(frame5);
        ImageRotateCW(&frame7);

        DrawSpriteCell(enemySheet, frame0, 0, typeIndex, kEnemySpriteSheetCellSize);
        DrawSpriteCell(enemySheet, frame1, 1, typeIndex, kEnemySpriteSheetCellSize);
        DrawSpriteCell(enemySheet, frame2, 2, typeIndex, kEnemySpriteSheetCellSize);
        DrawSpriteCell(enemySheet, frame3, 3, typeIndex, kEnemySpriteSheetCellSize);
        DrawSpriteCell(enemySheet, frame4, 4, typeIndex, kEnemySpriteSheetCellSize);
        DrawSpriteCell(enemySheet, frame5, 5, typeIndex, kEnemySpriteSheetCellSize);
        DrawSpriteCell(enemySheet, frame6, 6, typeIndex, kEnemySpriteSheetCellSize);
        DrawSpriteCell(enemySheet, frame7, 7, typeIndex, kEnemySpriteSheetCellSize);

        // Pre-colorize this type's row so we can draw with WHITE tint and avoid batch flushes.
        const Color typeColor = kEnemyTypeColors[static_cast<std::size_t>(typeIndex)];
        Color* sheetPixels = static_cast<Color*>(enemySheet.data);
        if (sheetPixels != nullptr) {
            const int rowStartY = typeIndex * kEnemySpriteSheetCellSize;
            const int rowEndY = rowStartY + kEnemySpriteSheetCellSize;
            const int sheetWidth = enemySheet.width;
            for (int py = rowStartY; py < rowEndY; ++py) {
                for (int px = 0; px < sheetWidth; ++px) {
                    Color& pixel = sheetPixels[py * sheetWidth + px];
                    if (pixel.a == 0) {
                        continue;
                    }
                    pixel.r = typeColor.r;
                    pixel.g = typeColor.g;
                    pixel.b = typeColor.b;
                }
            }
        }

        UnloadImage(frame7);
        UnloadImage(frame6);
        UnloadImage(frame5);
        UnloadImage(frame4);
        UnloadImage(frame3);
        UnloadImage(frame2);
        UnloadImage(frame1);
        UnloadImage(frame0);
    }

    enemyTankSheet_ = LoadTextureFromImage(enemySheet);
    enemyTankSheetLoaded_ = enemyTankSheet_.id != 0;
    if (enemyTankSheetLoaded_) {
        SetTextureFilter(enemyTankSheet_, TEXTURE_FILTER_POINT);
    } else {
        bolt::log::Warning("RENDER: failed to create enemy spritesheet texture from sprites.png");
    }
    UnloadImage(enemySheet);

    auto loadExplosionSheet = [](
        const char* filename,
        int framePx,
        Texture2D& outTexture,
        bool& outLoaded) {
        constexpr int kFrames = GameplayConstants::kExplosionFrameCount;
        Image img{};
        if (!TryLoadImageFromTextureDirectory(img, filename)) {
            bolt::log::Warning("RENDER: %s not found", filename);
            return;
        }
        if (img.width == kFrames * framePx && img.height == framePx) {
            outTexture = LoadTextureFromImage(img);
            outLoaded = outTexture.id != 0;
            if (outLoaded) {
                SetTextureFilter(outTexture, TEXTURE_FILTER_POINT);
            } else {
                bolt::log::Warning("RENDER: failed to upload %s texture", filename);
            }
        } else {
            bolt::log::Warning(
                "RENDER: %s unexpected size (%i x %i), expected %i x %i",
                filename,
                img.width,
                img.height,
                kFrames * framePx,
                framePx);
        }
        UnloadImage(img);
    };

    loadExplosionSheet(
        "explosion-1.png",
        GameplayConstants::kEnemyExplosionSourceFrameSizePx,
        enemyExplosionSheet_,
        enemyExplosionSheetLoaded_);
    loadExplosionSheet(
        "explosion-2.png",
        GameplayConstants::kPlayerExplosionSourceFrameSizePx,
        playerExplosionSheet_,
        playerExplosionSheetLoaded_);
    loadExplosionSheet(
        "explosion-3-large.png",
        GameplayConstants::kBaseExplosionSourceFrameSizePx,
        baseExplosionSheet_,
        baseExplosionSheetLoaded_);

    UnloadImage(playerSheet);
    UnloadImage(playerFrame7);
    UnloadImage(playerFrame6);
    UnloadImage(playerFrame5);
    UnloadImage(playerFrame4);
    UnloadImage(playerFrame3);
    UnloadImage(playerFrame2);
    UnloadImage(playerFrame1);
    UnloadImage(playerFrame0);
    UnloadImage(playerBarrel45);
    UnloadImage(playerBarrelUp);
    UnloadImage(playerBody45);
    UnloadImage(playerBodyUp);
    UnloadImage(sourceSheet);

    // Pre-bake base sprites into textures so each base draws in one call instead of three.
    auto generateBaseTexture = [&](Color shellColor, Color coreColor, Texture2D& outTexture, bool& outLoaded) {
        Image img = GenImageColor(kBaseSizePx, kBaseSizePx, shellColor);
        ImageDrawRectangle(&img, kBaseHoleOffsetPx, kBaseHoleOffsetPx, kBaseHolePx, kBaseHolePx, kBackgroundColor);
        ImageDrawCircle(&img, kBaseHalfPx, kBaseHalfPx, kBaseCoreRadiusPx, coreColor);
        outTexture = LoadTextureFromImage(img);
        outLoaded = outTexture.id != 0;
        if (outLoaded) {
            SetTextureFilter(outTexture, TEXTURE_FILTER_POINT);
        }
        UnloadImage(img);
    };
    generateBaseTexture(kEnemyBaseShellColor, kEnemyBaseColor, baseAliveTexture_, baseAliveTextureLoaded_);
    generateBaseTexture(kDestroyedBaseColor, kDestroyedBaseColor, baseDestroyedTexture_, baseDestroyedTextureLoaded_);

    return playerTankSheetLoaded_;
}

void Renderer2D::UnloadResources() {
    if (playerTankSheetLoaded_) {
        UnloadTexture(playerTankSheet_);
        playerTankSheetLoaded_ = false;
        playerTankSheet_ = Texture2D{};
    }
    if (enemyTankSheetLoaded_) {
        UnloadTexture(enemyTankSheet_);
        enemyTankSheetLoaded_ = false;
        enemyTankSheet_ = Texture2D{};
    }
    if (enemyExplosionSheetLoaded_) {
        UnloadTexture(enemyExplosionSheet_);
        enemyExplosionSheetLoaded_ = false;
        enemyExplosionSheet_ = Texture2D{};
    }
    if (playerExplosionSheetLoaded_) {
        UnloadTexture(playerExplosionSheet_);
        playerExplosionSheetLoaded_ = false;
        playerExplosionSheet_ = Texture2D{};
    }
    if (baseExplosionSheetLoaded_) {
        UnloadTexture(baseExplosionSheet_);
        baseExplosionSheetLoaded_ = false;
        baseExplosionSheet_ = Texture2D{};
    }
    if (baseAliveTextureLoaded_) {
        UnloadTexture(baseAliveTexture_);
        baseAliveTextureLoaded_ = false;
        baseAliveTexture_ = Texture2D{};
    }
    if (baseDestroyedTextureLoaded_) {
        UnloadTexture(baseDestroyedTexture_);
        baseDestroyedTextureLoaded_ = false;
        baseDestroyedTexture_ = Texture2D{};
    }
}

void Renderer2D::DrawWorld(const GameState& state, const AppConfig& config) {
    profiling::ScopedProfile worldScope(profiling::Scope::RenderWorld, true);
    const int worldWidth = config.screenWidth - ComputeHudWidth(config);
    const Rectangle worldViewport = {
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(worldWidth),
        .height = static_cast<float>(config.screenHeight),
    };

    DrawRectangleRec(worldViewport, kBackgroundColor);

    Camera2D camera{};
    const Vec2f cameraTarget = state.world.panModeActive ? state.world.panTarget : state.world.player.position;
    camera.target = SnapWorldToPixelGrid(cameraTarget);
    camera.offset = Vector2{
        std::round(worldViewport.width * 0.5F),
        std::round(worldViewport.height * 0.5F),
    };
    camera.rotation = 0.0F;
    camera.zoom = static_cast<float>(GameplayConstants::kPixelsPerUnit);

#if defined(__APPLE__) && !defined(NDEBUG)
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        const Vector2 mouse = GetMousePosition();
        const bool clickInWorldViewport =
            mouse.x >= worldViewport.x &&
            mouse.x < worldViewport.x + worldViewport.width &&
            mouse.y >= worldViewport.y &&
            mouse.y < worldViewport.y + worldViewport.height;
        if (clickInWorldViewport) {
            const Vector2 clickWorldRaylib = GetScreenToWorld2D(mouse, camera);
            const Vec2f clickWorld{
                .x = clickWorldRaylib.x,
                .y = clickWorldRaylib.y,
            };
            const game::navigation::MazeCellCoord clickCell =
                state.world.navigationCache.cellCoords.WorldToCell(clickWorld);
            const bool clickInsideMaze =
                clickWorld.x >= 0.0F &&
                clickWorld.y >= 0.0F &&
                clickWorld.x < static_cast<float>(state.world.maze.widthCells * state.world.maze.cellSizeUnits) &&
                clickWorld.y < static_cast<float>(state.world.maze.heightCells * state.world.maze.cellSizeUnits);
            const game::navigation::MazeCellCoord playerCell =
                state.world.navigationCache.cellCoords.PlayerCell();
            bolt::log::Profile(
                "[ENEMY_CLICK_DEBUG] screen=(%.1f,%.1f) world=(%.3f,%.3f) inMaze=%d "
                "cell=(%d,%d) playerCell=(%d,%d) playerHash=%d flowHasBuild=%d\n",
                mouse.x,
                mouse.y,
                clickWorld.x,
                clickWorld.y,
                clickInsideMaze ? 1 : 0,
                clickCell.x,
                clickCell.y,
                playerCell.x,
                playerCell.y,
                state.world.navigationCache.cellCoords.PlayerCellHash(),
                state.world.navigationCache.playerFlowField.HasBuild() ? 1 : 0);
            DebugLogEnemiesAtPosition(state, clickWorld, clickCell);
        }
    }
#endif

    BeginScissorMode(0, 0, worldWidth, config.screenHeight);
    const float mazeWidthUnits = static_cast<float>(state.world.maze.widthCells * state.world.maze.cellSizeUnits);
    const float mazeHeightUnits = static_cast<float>(state.world.maze.heightCells * state.world.maze.cellSizeUnits);
    constexpr int wallThicknessPixels = 2;

    const float halfVisibleWidthUnits = worldViewport.width / (2.0F * camera.zoom);
    const float halfVisibleHeightUnits = worldViewport.height / (2.0F * camera.zoom);
    const float visibleLeft = camera.target.x - halfVisibleWidthUnits;
    const float visibleRight = camera.target.x + halfVisibleWidthUnits;
    const float visibleTop = camera.target.y - halfVisibleHeightUnits;
    const float visibleBottom = camera.target.y + halfVisibleHeightUnits;

    const int minCellX = std::max(
        0,
        static_cast<int>(std::floor(visibleLeft / static_cast<float>(state.world.maze.cellSizeUnits))) - 1);
    const int maxCellX = std::min(
        state.world.maze.widthCells - 1,
        static_cast<int>(std::ceil(visibleRight / static_cast<float>(state.world.maze.cellSizeUnits))) + 1);
    const int minCellY = std::max(
        0,
        static_cast<int>(std::floor(visibleTop / static_cast<float>(state.world.maze.cellSizeUnits))) - 1);
    const int maxCellY = std::min(
        state.world.maze.heightCells - 1,
        static_cast<int>(std::ceil(visibleBottom / static_cast<float>(state.world.maze.cellSizeUnits))) + 1);

    {
        profiling::ScopedProfile mazeScope(profiling::Scope::RenderWorldMaze);
        for (int y = minCellY; y <= maxCellY; ++y) {
            for (int x = minCellX; x <= maxCellX; ++x) {
                const MazeCell& cell =
                    state.world.maze.cells[static_cast<std::size_t>(y * state.world.maze.widthCells + x)];
                const float left = static_cast<float>(x * state.world.maze.cellSizeUnits);
                const float top = static_cast<float>(y * state.world.maze.cellSizeUnits);
                const float right = left + static_cast<float>(state.world.maze.cellSizeUnits);
                const float bottom = top + static_cast<float>(state.world.maze.cellSizeUnits);
                if (cell.northWall) {
                    DrawHorizontalWallPixels(
                        GetWorldToScreen2D(Vector2{left, top}, camera),
                        GetWorldToScreen2D(Vector2{right, top}, camera),
                        wallThicknessPixels,
                        kWallsColor);
                }
                if (cell.westWall) {
                    DrawVerticalWallPixels(
                        GetWorldToScreen2D(Vector2{left, top}, camera),
                        GetWorldToScreen2D(Vector2{left, bottom}, camera),
                        wallThicknessPixels,
                        kWallsColor);
                }
                if (x == state.world.maze.widthCells - 1 && cell.eastWall) {
                    DrawVerticalWallPixels(
                        GetWorldToScreen2D(Vector2{right, top}, camera),
                        GetWorldToScreen2D(Vector2{right, bottom}, camera),
                        wallThicknessPixels,
                        kWallsColor);
                }
                if (y == state.world.maze.heightCells - 1 && cell.southWall) {
                    DrawHorizontalWallPixels(
                        GetWorldToScreen2D(Vector2{left, bottom}, camera),
                        GetWorldToScreen2D(Vector2{right, bottom}, camera),
                        wallThicknessPixels,
                        kWallsColor);
                }
            }
        }

        const Vector2 borderTopLeft = GetWorldToScreen2D(Vector2{0.0F, 0.0F}, camera);
        const Vector2 borderTopRight = GetWorldToScreen2D(Vector2{mazeWidthUnits, 0.0F}, camera);
        const Vector2 borderBottomLeft = GetWorldToScreen2D(Vector2{0.0F, mazeHeightUnits}, camera);
        const Vector2 borderBottomRight = GetWorldToScreen2D(Vector2{mazeWidthUnits, mazeHeightUnits}, camera);
        DrawHorizontalWallPixels(borderTopLeft, borderTopRight, wallThicknessPixels, kWallsColor);
        DrawHorizontalWallPixels(borderBottomLeft, borderBottomRight, wallThicknessPixels, kWallsColor);
        DrawVerticalWallPixels(borderTopLeft, borderBottomLeft, wallThicknessPixels, kWallsColor);
        DrawVerticalWallPixels(borderTopRight, borderBottomRight, wallThicknessPixels, kWallsColor);
    }

    const bool showFlowField =
#if defined(__APPLE__) && !defined(NDEBUG)
        state.world.navigationCache.playerFlowField.HasBuild();
#else
        state.menuSettings.debugInfo && state.world.navigationCache.playerFlowField.HasBuild();
#endif
    if (showFlowField) {
        const auto& flowField = state.world.navigationCache.playerFlowField;
        const float cellSizeUnits = static_cast<float>(state.world.maze.cellSizeUnits);
        const float cellHalfUnits = cellSizeUnits * 0.5F;
        for (int cellY = minCellY; cellY <= maxCellY; ++cellY) {
            for (int cellX = minCellX; cellX <= maxCellX; ++cellX) {
                const int fromHash = cellY * state.world.maze.widthCells + cellX;
                const int toHash = flowField.NextCellHash(fromHash);
                if (toHash < 0 || toHash == fromHash) {
                    continue;
                }
                const int toX = toHash % state.world.maze.widthCells;
                const int toY = toHash / state.world.maze.widthCells;
                DrawFlowFieldArrow(
                    camera,
                    static_cast<float>(cellX) * cellSizeUnits + cellHalfUnits,
                    static_cast<float>(cellY) * cellSizeUnits + cellHalfUnits,
                    static_cast<float>(toX) * cellSizeUnits + cellHalfUnits,
                    static_cast<float>(toY) * cellSizeUnits + cellHalfUnits,
                    cellSizeUnits);
            }
        }
    }

    const Vector2 playerRenderPosition = SnapWorldToPixelGrid(state.world.player.position);
    {
        profiling::ScopedProfile enemiesScope(profiling::Scope::RenderWorldEnemies);
        constexpr std::size_t kMaxTrackedVisibleEnemies = GameplayConstants::kMaxAliveEnemies;
        std::array<const EnemyTank*, kMaxTrackedVisibleEnemies> visibleEnemies{};
        int visibleEnemyCount = 0;
        {
            profiling::ScopedProfile cullScope(profiling::Scope::RenderWorldEnemiesCull);
            for (const EnemyTank& enemy : state.world.enemies) {
                if (!enemy.alive) {
                    continue;
                }
                if (!IsWorldPointVisible(
                        enemy.position,
                        visibleLeft,
                        visibleRight,
                        visibleTop,
                        visibleBottom,
                        kEnemyRenderCullMarginUnits)) {
                    continue;
                }
                if (visibleEnemyCount < static_cast<int>(visibleEnemies.size())) {
                    visibleEnemies[static_cast<std::size_t>(visibleEnemyCount)] = &enemy;
                    ++visibleEnemyCount;
                }
            }
        }

        std::sort(
            visibleEnemies.begin(),
            visibleEnemies.begin() + visibleEnemyCount,
            [](const EnemyTank* a, const EnemyTank* b) {
                return EnemyTypeIndex(a->type) < EnemyTypeIndex(b->type);
            });

        {
            profiling::ScopedProfile drawScope(profiling::Scope::RenderWorldEnemiesDraw);
            for (const EnemyBase& base : state.world.enemyBases) {
                const Vector2 baseScreenPosition = WorldToSnappedScreen(base.position, camera);
                const int topLeftX = RoundToInt(baseScreenPosition.x) - kBaseHalfPx;
                const int topLeftY = RoundToInt(baseScreenPosition.y) - kBaseHalfPx;
                if (!base.destroyed && baseAliveTextureLoaded_) {
                    DrawTexture(baseAliveTexture_, topLeftX, topLeftY, WHITE);
                } else if (base.destroyed && baseDestroyedTextureLoaded_) {
                    DrawTexture(baseDestroyedTexture_, topLeftX, topLeftY, WHITE);
                } else {
                    // Fallback if textures didn't load.
                    const Color shellColor = base.destroyed ? kDestroyedBaseColor : kEnemyBaseShellColor;
                    const Color coreColor = base.destroyed ? kDestroyedBaseColor : kEnemyBaseColor;
                    DrawRectangle(topLeftX, topLeftY, kBaseSizePx, kBaseSizePx, shellColor);
                    DrawRectangle(
                        topLeftX + kBaseHoleOffsetPx, topLeftY + kBaseHoleOffsetPx,
                        kBaseHolePx, kBaseHolePx, kBackgroundColor);
                    DrawCircle(topLeftX + kBaseHalfPx, topLeftY + kBaseHalfPx,
                               static_cast<float>(kBaseCoreRadiusPx), coreColor);
                }
            }

            const int enemySizePixels = kEnemyRenderSizePx;
            const int enemyHalfPixels = enemySizePixels / 2;
            for (int i = 0; i < visibleEnemyCount; ++i) {
                const EnemyTank& enemy = *visibleEnemies[static_cast<std::size_t>(i)];
                const Vector2 enemyScreenPosition = WorldToSnappedScreen(enemy.position, camera);
                const Rectangle destRect{
                    .x = static_cast<float>(RoundToInt(enemyScreenPosition.x) - enemyHalfPixels),
                    .y = static_cast<float>(RoundToInt(enemyScreenPosition.y) - enemyHalfPixels),
                    .width = static_cast<float>(enemySizePixels),
                    .height = static_cast<float>(enemySizePixels),
                };
                if (enemyTankSheetLoaded_) {
                    const int directionIndex = PlayerFrameIndexFromHeading(enemy.headingRadians, kEnemyTankDirectionCount);
                    const int typeIndex = EnemyTypeIndex(enemy.type);
                    const Rectangle sourceRect{
                        .x = static_cast<float>(directionIndex * kEnemyTankFrameSizePx),
                        .y = static_cast<float>(typeIndex * kEnemyTankFrameSizePx),
                        .width = static_cast<float>(kEnemyTankFrameSizePx),
                        .height = static_cast<float>(kEnemyTankFrameSizePx),
                    };
                    DrawTexturePro(enemyTankSheet_, sourceRect, destRect, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
                } else {
                    DrawRectangleRec(destRect, EnemyColorForType(enemy.type));
                }
                if (state.menuSettings.debugInfo) {
                    if (enemy.type == EnemyType::Hunter &&
                        enemy.hunterScoutPathActive &&
                        enemy.hunterScoutSegmentIndex >= 0 &&
                        enemy.hunterScoutSegmentIndex < enemy.hunterScoutSegmentCount) {
                        const Vec2f segmentTarget =
                            enemy.hunterScoutSegmentPoints[static_cast<std::size_t>(enemy.hunterScoutSegmentIndex)];
                        const Vector2 targetScreenPosition = WorldToSnappedScreen(segmentTarget, camera);
                        DrawLine(
                            static_cast<int>(enemyScreenPosition.x),
                            static_cast<int>(enemyScreenPosition.y),
                            static_cast<int>(targetScreenPosition.x),
                            static_cast<int>(targetScreenPosition.y),
                            GREEN);
                    } else if (enemy.offscreenSegmentActive) {
                        const Vector2 targetScreenPosition =
                            WorldToSnappedScreen(enemy.offscreenSegmentEnd, camera);
                        DrawLine(
                            static_cast<int>(enemyScreenPosition.x),
                            static_cast<int>(enemyScreenPosition.y),
                            static_cast<int>(targetScreenPosition.x),
                            static_cast<int>(targetScreenPosition.y),
                            GREEN);
                    }
                    const float cx = enemyScreenPosition.x;
                    const float cy = enemyScreenPosition.y;
                    const float radiusHardPx =
                        GameplayConstants::kWallClearanceForHard * camera.zoom;
                    const float radiusAvoidPx =
                        GameplayConstants::kWallClearanceForAvoidance * camera.zoom;
                    DrawCircleLines(static_cast<int>(cx), static_cast<int>(cy), radiusHardPx, WHITE);
                    DrawCircleLines(static_cast<int>(cx), static_cast<int>(cy), radiusAvoidPx, GRAY);
                    const float distToWall = game::geometry::DistanceToNearestWall(
                        state.world, enemy.position, 20.0F);
                    const bool avoidViolation = game::geometry::IsPointInWall(
                        state.world, enemy.position, GameplayConstants::kWallClearanceForAvoidance);
                    const bool hardViolation = game::geometry::IsPointInWall(
                        state.world, enemy.position, GameplayConstants::kWallClearanceForHard);
                    char buf[32];
                    Color textColor = WHITE;
                    if (hardViolation) {
                        std::snprintf(buf, sizeof(buf), "[%.1f]", distToWall);
                        textColor = RED;
                    } else if (avoidViolation) {
                        std::snprintf(buf, sizeof(buf), "(%.1f)", distToWall);
                        textColor = YELLOW;
                    } else {
                        std::snprintf(buf, sizeof(buf), "%.1f", distToWall);
                    }
                    constexpr int kDebugFontSize = 10;
                    const int textW = MeasureText(buf, kDebugFontSize);
                    const float textX = cx - static_cast<float>(textW) * 0.5F;
                    const float textY = cy - radiusAvoidPx - static_cast<float>(kDebugFontSize) - 2.0F;
                    DrawText(buf, static_cast<int>(textX), static_cast<int>(textY), kDebugFontSize, textColor);
                }
            }

            if (enemyExplosionSheetLoaded_) {
                constexpr int kExpFramePx = GameplayConstants::kEnemyExplosionSourceFrameSizePx;
                constexpr int kExpRenderPx = GameplayConstants::kEnemyExplosionRenderSizePx;
                constexpr int kExpHalfRenderPx = kExpRenderPx / 2;
                constexpr float kExpFrameDur = GameplayConstants::kEnemyExplosionFrameDurationSeconds;
                constexpr int kExpFrameCount = GameplayConstants::kEnemyExplosionFrameCount;
                for (const EnemyExplosion& explosion : state.world.enemyExplosions) {
                    if (!explosion.active) {
                        continue;
                    }
                    if (!IsWorldPointVisible(
                            explosion.position,
                            visibleLeft,
                            visibleRight,
                            visibleTop,
                            visibleBottom,
                            kEnemyRenderCullMarginUnits)) {
                        continue;
                    }
                    const int frameIndex = std::min(
                        static_cast<int>(explosion.elapsedSeconds / kExpFrameDur),
                        kExpFrameCount - 1);
                    const Rectangle srcRect{
                        .x = static_cast<float>(frameIndex * kExpFramePx),
                        .y = 0.0F,
                        .width = static_cast<float>(kExpFramePx),
                        .height = static_cast<float>(kExpFramePx),
                    };
                    const Vector2 expScreenPos = WorldToSnappedScreen(explosion.position, camera);
                    const Rectangle dstRect{
                        .x = static_cast<float>(RoundToInt(expScreenPos.x) - kExpHalfRenderPx),
                        .y = static_cast<float>(RoundToInt(expScreenPos.y) - kExpHalfRenderPx),
                        .width = static_cast<float>(kExpRenderPx),
                        .height = static_cast<float>(kExpRenderPx),
                    };
                    DrawTexturePro(enemyExplosionSheet_, srcRect, dstRect, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
                }
            }
        }
    }

    {
        profiling::ScopedProfile effectsScope(profiling::Scope::RenderWorldEffects);
        if (!state.world.projectiles.empty()) {
            profiling::ScopedProfile projectileScope(profiling::Scope::RenderWorldEffectsProjectiles);
            for (const Projectile& projectile : state.world.projectiles) {
                if (!projectile.alive) {
                    continue;
                }
                if (!IsWorldPointVisible(
                        projectile.position,
                        visibleLeft,
                        visibleRight,
                        visibleTop,
                        visibleBottom,
                        kProjectileRenderCullMarginUnits)) {
                    continue;
                }
                const Color color = projectile.owner == ProjectileOwner::Player ? kPlayerShellColor : kEnemyShellColor;
                const Vector2 projectileScreenPosition = WorldToSnappedScreen(projectile.position, camera);
                const int px = RoundToInt(projectileScreenPosition.x) - kProjectileRenderHalfSizePixels;
                const int py = RoundToInt(projectileScreenPosition.y) - kProjectileRenderHalfSizePixels;
                DrawRectangle(px, py, kProjectileRenderSizePixels, kProjectileRenderSizePixels, color);
            }
        }

        BeginMode2D(camera);

        if (state.world.deathExplosionRemainingSeconds > 0.0F) {
            profiling::ScopedProfile explosionScope(profiling::Scope::RenderWorldEffectsExplosion);
            const float elapsed =
                GameplayConstants::kDeathExplosionDurationSeconds - state.world.deathExplosionRemainingSeconds;
            const int frameIndex = static_cast<int>(elapsed / GameplayConstants::kExplosionFrameDurationSeconds);
            if (playerExplosionSheetLoaded_ && frameIndex < GameplayConstants::kExplosionFrameCount) {
                constexpr int kSrcPx = GameplayConstants::kPlayerExplosionSourceFrameSizePx;
                constexpr float kHalf = GameplayConstants::kPlayerExplosionRenderWorldUnits * 0.5F;
                const Rectangle srcRect{
                    .x = static_cast<float>(frameIndex * kSrcPx),
                    .y = 0.0F,
                    .width = static_cast<float>(kSrcPx),
                    .height = static_cast<float>(kSrcPx),
                };
                const Rectangle dstRect{
                    .x = state.world.deathExplosionPosition.x - kHalf,
                    .y = state.world.deathExplosionPosition.y - kHalf,
                    .width = GameplayConstants::kPlayerExplosionRenderWorldUnits,
                    .height = GameplayConstants::kPlayerExplosionRenderWorldUnits,
                };
                DrawTexturePro(playerExplosionSheet_, srcRect, dstRect, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
            }
        }

        if (baseExplosionSheetLoaded_) {
            constexpr int kSrcPx = GameplayConstants::kBaseExplosionSourceFrameSizePx;
            constexpr float kHalf = GameplayConstants::kBaseExplosionRenderWorldUnits * 0.5F;
            constexpr float kSize = GameplayConstants::kBaseExplosionRenderWorldUnits;
            constexpr float kFrameDur = GameplayConstants::kExplosionFrameDurationSeconds;
            constexpr int kFrameCount = GameplayConstants::kExplosionFrameCount;
            for (const EnemyExplosion& explosion : state.world.baseExplosions) {
                if (!explosion.active) {
                    continue;
                }
                const int frameIndex = std::min(
                    static_cast<int>(explosion.elapsedSeconds / kFrameDur),
                    kFrameCount - 1);
                const Rectangle srcRect{
                    .x = static_cast<float>(frameIndex * kSrcPx),
                    .y = 0.0F,
                    .width = static_cast<float>(kSrcPx),
                    .height = static_cast<float>(kSrcPx),
                };
                const Rectangle dstRect{
                    .x = explosion.position.x - kHalf,
                    .y = explosion.position.y - kHalf,
                    .width = kSize,
                    .height = kSize,
                };
                DrawTexturePro(baseExplosionSheet_, srcRect, dstRect, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
            }
        }

        if (!playerTankSheetLoaded_ && state.world.player.alive) {
            profiling::ScopedProfile playerFallbackScope(profiling::Scope::RenderWorldEffectsPlayerFallback);
            const float half = GameplayConstants::kEntitySizeUnits * 0.5F;
            DrawRectangleRec(
                Rectangle{
                    .x = state.world.player.position.x - half,
                    .y = state.world.player.position.y - half,
                    .width = GameplayConstants::kEntitySizeUnits,
                    .height = GameplayConstants::kEntitySizeUnits,
                },
                kPlayerColor);
        }

        EndMode2D();
    }
    if (playerTankSheetLoaded_ && state.world.player.alive) {
        const int frameIndex = PlayerFrameIndexFromHeading(state.world.player.hullHeadingRadians, playerTankFrameCount_);
        const Vector2 sourceOffsetPixels =
            playerTankFrameOffsetsPixels_[static_cast<std::size_t>(frameIndex)];
        const float offsetScale =
            static_cast<float>(kPlayerRenderSizePx) / static_cast<float>(playerTankFrameSizePx_);
        const Vector2 scaledOffsetPixels{
            .x = std::round(sourceOffsetPixels.x * offsetScale),
            .y = std::round(sourceOffsetPixels.y * offsetScale),
        };
        const Rectangle sourceRect{
            .x = static_cast<float>(frameIndex * playerTankFrameSizePx_),
            .y = 0.0F,
            .width = static_cast<float>(playerTankFrameSizePx_),
            .height = static_cast<float>(playerTankFrameSizePx_),
        };
        const Vector2 playerScreenPosition = GetWorldToScreen2D(playerRenderPosition, camera);
        const Rectangle destRect{
            .x = static_cast<float>(RoundToInt(playerScreenPosition.x + scaledOffsetPixels.x)),
            .y = static_cast<float>(RoundToInt(playerScreenPosition.y + scaledOffsetPixels.y)),
            .width = static_cast<float>(kPlayerRenderSizePx),
            .height = static_cast<float>(kPlayerRenderSizePx),
        };
        const float halfFrame = static_cast<float>(kPlayerRenderSizePx) * 0.5F;
        DrawTexturePro(playerTankSheet_, sourceRect, destRect, Vector2{halfFrame, halfFrame}, 0.0F, WHITE);
    }
    EndScissorMode();
}
