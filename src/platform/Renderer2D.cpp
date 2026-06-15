#include "platform/Renderer2D.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include "core/Log.h"
#include "core/Profiling.h"
#include "core/ResourceLocator.h"
#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"
#include "game/model/WorldState.h"
#include "game/systems/EnemySystem.h"
#include "raylib.h"

namespace {
constexpr int kSpriteSheetColumns = 2;
constexpr int kSpriteSheetRows = 10;
constexpr int kSpriteSheetCellSize = 9;
constexpr int kPlayerBodyRowIndex = 0;
constexpr int kPlayerBarrelRowIndex = 1;
constexpr int kEnemySpriteSheetCellSize = 9;
/// Torpedo/Hunter/Assassin source rows use `kEnemySpriteFirstRowIndex + sheetRow - 1` for sheet rows `2..4`.
constexpr int kEnemySpriteFirstRowIndex = 3;
constexpr int kDroneWatchSpriteRowIndex = 7;
constexpr int kDroneWanderSpriteRowIndex = 8;
/// Reserved for future `Tadpole` enemy type; not used by the renderer yet.
constexpr int kTadpoleEnemySpriteRowIndex = 9;
static_assert(kTadpoleEnemySpriteRowIndex == kSpriteSheetRows - 1);

int EnemySheetSourceRow(int sheetRow) {
    if (sheetRow == 0) {
        return kDroneWatchSpriteRowIndex;
    }
    if (sheetRow == 1) {
        return kDroneWanderSpriteRowIndex;
    }
    return kEnemySpriteFirstRowIndex + sheetRow - 1;
}
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
constexpr std::uint32_t kDestroyedBaseHex = 0x303030;
constexpr std::uint32_t kPlayerHex = 0x03C703;
constexpr std::uint32_t kDroneHex = 0x42BE8F;
constexpr std::uint32_t kTorpedoHex = 0xA4AD43;
constexpr std::uint32_t kHunterHex = 0xDD9143;
constexpr std::uint32_t kAssassinHex = 0xDD9143;
constexpr std::uint32_t kEnemyBaseShellHex = 0x5E6CC0;
constexpr std::uint32_t kEnemyBaseHex = 0x8C9DF6;
constexpr std::uint32_t kPlayerShellHex = 0xFFFFFF;
constexpr std::uint32_t kEnemyShellHex = 0xFFB000;
constexpr std::uint32_t kAlarmIndicatorHex = 0xDC2626;

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
constexpr Color kAlarmIndicatorColor = ColorFromHexRGB(kAlarmIndicatorHex);
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
constexpr float kBaseCoreClearRadiusPx = static_cast<float>(kBaseHoleHalfPx);
constexpr int kBaseOuterThicknessPx = 12;
constexpr int kBaseThicknessStepPx = 3;
constexpr float kBaseCoreHealingPulseCycleSeconds = 2.0F;
constexpr float kHealingCoreRadiusPulsePx = 2.0F;
constexpr float kSpawnCoreGrowthPx = 3.0F;
constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = kPi * 2.0F;
static_assert(kBaseSizePx == 48, "Enemy base sprite size must stay 48x48 px");
static_assert(kBaseHoleOffsetPx == kBaseOuterThicknessPx, "Base outer shell must be 12 px at full health");
static_assert(
    GameplayConstants::kBaseOuterSegmentMaxHealth * kBaseThicknessStepPx == kBaseOuterThicknessPx,
    "Base segment thickness step must be 3 px per health point");

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

int BaseSegmentThicknessPixels(int segmentHealth) {
    const int clampedHealth = std::clamp(segmentHealth, 0, GameplayConstants::kBaseOuterSegmentMaxHealth);
    const int thickness = clampedHealth * kBaseThicknessStepPx;
    return std::clamp(thickness, 0, kBaseOuterThicknessPx);
}

int BaseSideInsetPixelsFromDamage(int segmentHealth) {
    const int thickness = BaseSegmentThicknessPixels(segmentHealth);
    return std::clamp(kBaseOuterThicknessPx - thickness, 0, kBaseOuterThicknessPx);
}

float Clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

float LinearFromSrgb(float srgb) {
    const float clamped = Clamp01(srgb);
    if (clamped <= 0.04045F) {
        return clamped / 12.92F;
    }
    return std::pow((clamped + 0.055F) / 1.055F, 2.4F);
}

float SrgbFromLinear(float linear) {
    const float clamped = Clamp01(linear);
    if (clamped <= 0.0031308F) {
        return clamped * 12.92F;
    }
    return 1.055F * std::pow(clamped, 1.0F / 2.4F) - 0.055F;
}

std::uint8_t ByteFromUnit(float value) {
    return static_cast<std::uint8_t>(std::round(Clamp01(value) * 255.0F));
}

struct OklabColor {
    float l = 0.0F;
    float a = 0.0F;
    float b = 0.0F;
};

OklabColor OklabFromColor(Color color) {
    const float r = LinearFromSrgb(static_cast<float>(color.r) / 255.0F);
    const float g = LinearFromSrgb(static_cast<float>(color.g) / 255.0F);
    const float b = LinearFromSrgb(static_cast<float>(color.b) / 255.0F);

    const float l = 0.4122214708F * r + 0.5363325363F * g + 0.0514459929F * b;
    const float m = 0.2119034982F * r + 0.6806995451F * g + 0.1073969566F * b;
    const float s = 0.0883024619F * r + 0.2817188376F * g + 0.6299787005F * b;

    const float lRoot = std::cbrt(l);
    const float mRoot = std::cbrt(m);
    const float sRoot = std::cbrt(s);

    return OklabColor{
        .l = 0.2104542553F * lRoot + 0.7936177850F * mRoot - 0.0040720468F * sRoot,
        .a = 1.9779984951F * lRoot - 2.4285922050F * mRoot + 0.4505937099F * sRoot,
        .b = 0.0259040371F * lRoot + 0.7827717662F * mRoot - 0.8086757660F * sRoot,
    };
}

Color ColorFromOklab(const OklabColor& lab, std::uint8_t alpha) {
    const float lRoot = lab.l + 0.3963377774F * lab.a + 0.2158037573F * lab.b;
    const float mRoot = lab.l - 0.1055613458F * lab.a - 0.0638541728F * lab.b;
    const float sRoot = lab.l - 0.0894841775F * lab.a - 1.2914855480F * lab.b;

    const float l = lRoot * lRoot * lRoot;
    const float m = mRoot * mRoot * mRoot;
    const float s = sRoot * sRoot * sRoot;

    const float linearR = 4.0767416621F * l - 3.3077115913F * m + 0.2309699292F * s;
    const float linearG = -1.2684380046F * l + 2.6097574011F * m - 0.3413193965F * s;
    const float linearB = -0.0041960863F * l - 0.7034186147F * m + 1.7076147010F * s;

    return Color{
        ByteFromUnit(SrgbFromLinear(linearR)),
        ByteFromUnit(SrgbFromLinear(linearG)),
        ByteFromUnit(SrgbFromLinear(linearB)),
        alpha,
    };
}

Color ApplyOklchLightnessDelta(Color color, float deltaL) {
    OklabColor lab = OklabFromColor(color);
    lab.l = Clamp01(lab.l + deltaL);
    return ColorFromOklab(lab, color.a);
}

float EaseInOut01(float value) {
    const float t = Clamp01(value);
    return t * t * (3.0F - 2.0F * t);
}

float Repeat01(double elapsedSeconds, double periodSeconds) {
    if (periodSeconds <= 0.0) {
        return 0.0F;
    }
    double wrapped = std::fmod(elapsedSeconds, periodSeconds);
    if (wrapped < 0.0) {
        wrapped += periodSeconds;
    }
    return static_cast<float>(wrapped / periodSeconds);
}

struct BaseCoreVisualState {
    float radiusPixels = static_cast<float>(kBaseCoreRadiusPx);
    Color color = kEnemyBaseColor;
    bool animated = false;
};

BaseCoreVisualState ComputeAnimatedBaseCoreState(
    const EnemyBase& base,
    float spawnGrowthProgress01) {
    BaseCoreVisualState state{};
    float radiusOffset = 0.0F;
    float lightnessDelta = 0.0F;

    if (base.HasDamagedSegments()) {
        const float phase = Repeat01(GetTime(), kBaseCoreHealingPulseCycleSeconds);
        const float oscillation = std::sin((phase * kTwoPi) - (kPi * 0.5F));
        radiusOffset += kHealingCoreRadiusPulsePx * oscillation;
        // Smaller core -> brighter, larger core -> darker.
        lightnessDelta += -0.15F * oscillation;
        state.animated = true;
    }

    if (spawnGrowthProgress01 > 0.0F) {
        const float growth = EaseInOut01(Clamp01(spawnGrowthProgress01));
        radiusOffset += kSpawnCoreGrowthPx * growth;
        lightnessDelta += 0.2F * growth;
        state.animated = true;
    }

    state.radiusPixels = std::clamp(
        static_cast<float>(kBaseCoreRadiusPx) + radiusOffset,
        1.0F,
        static_cast<float>(kBaseHoleHalfPx));
    state.color = ApplyOklchLightnessDelta(kEnemyBaseColor, lightnessDelta);
    return state;
}

bool IsBaseFullyHealthy(const EnemyBase& base) {
    return base.topSegmentHealth >= GameplayConstants::kBaseOuterSegmentMaxHealth &&
           base.rightSegmentHealth >= GameplayConstants::kBaseOuterSegmentMaxHealth &&
           base.bottomSegmentHealth >= GameplayConstants::kBaseOuterSegmentMaxHealth &&
           base.leftSegmentHealth >= GameplayConstants::kBaseOuterSegmentMaxHealth;
}

/// Rasterize alive base at pixel origin (same geometry as world draw); `img` must be `kBaseSizePx`².
void RasterizeAliveBaseToImage(Image& img, const EnemyBase& base, Color shellColor, Color coreColor) {
    const int topThickness = BaseSegmentThicknessPixels(base.topSegmentHealth);
    const int rightThickness = BaseSegmentThicknessPixels(base.rightSegmentHealth);
    const int bottomThickness = BaseSegmentThicknessPixels(base.bottomSegmentHealth);
    const int leftThickness = BaseSegmentThicknessPixels(base.leftSegmentHealth);
    constexpr int kOrigin = 0;
    const int innerLeft = kOrigin + kBaseHoleOffsetPx;
    const int innerTop = kOrigin + kBaseHoleOffsetPx;
    const int innerRight = kOrigin + kBaseSizePx - kBaseHoleOffsetPx;
    const int innerBottom = kOrigin + kBaseSizePx - kBaseHoleOffsetPx;

    ImageDrawRectangle(&img, 0, 0, kBaseSizePx, kBaseSizePx, kBackgroundColor);

    if (topThickness > 0) {
        ImageDrawRectangle(&img, kOrigin, innerTop - topThickness, kBaseSizePx, topThickness, shellColor);
    }
    if (bottomThickness > 0) {
        ImageDrawRectangle(&img, kOrigin, innerBottom, kBaseSizePx, bottomThickness, shellColor);
    }
    if (leftThickness > 0) {
        ImageDrawRectangle(&img, innerLeft - leftThickness, kOrigin, leftThickness, kBaseSizePx, shellColor);
    }
    if (rightThickness > 0) {
        ImageDrawRectangle(&img, innerRight, kOrigin, rightThickness, kBaseSizePx, shellColor);
    }

    ImageDrawRectangle(&img, kBaseHoleOffsetPx, kBaseHoleOffsetPx, kBaseHolePx, kBaseHolePx, kBackgroundColor);
    ImageDrawCircle(&img, kBaseHalfPx, kBaseHalfPx, kBaseCoreRadiusPx, coreColor);
}

void RasterizeDestroyedBaseToImage(Image& img) {
    ImageDrawRectangle(&img, 0, 0, kBaseSizePx, kBaseSizePx, kDestroyedBaseColor);
    ImageDrawRectangle(&img, kBaseHoleOffsetPx, kBaseHoleOffsetPx, kBaseHolePx, kBaseHolePx, kBackgroundColor);
    ImageDrawCircle(&img, kBaseHalfPx, kBaseHalfPx, kBaseCoreRadiusPx, kDestroyedBaseColor);
}

bool EnsureHealthyBaseTexture(Texture2D& texture, bool& loaded) {
    if (loaded) {
        return true;
    }
    Image img = GenImageColor(kBaseSizePx, kBaseSizePx, kBackgroundColor);
    const EnemyBase healthyBase{};
    RasterizeAliveBaseToImage(img, healthyBase, kEnemyBaseShellColor, kEnemyBaseColor);
    texture = LoadTextureFromImage(img);
    loaded = texture.id != 0;
    if (loaded) {
        SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    } else {
        bolt::log::Warning("RENDER: failed to upload healthy base texture");
    }
    UnloadImage(img);
    return loaded;
}

bool EnsureDestroyedBaseTexture(Texture2D& texture, bool& loaded) {
    if (loaded) {
        return true;
    }
    Image img = GenImageColor(kBaseSizePx, kBaseSizePx, kDestroyedBaseColor);
    RasterizeDestroyedBaseToImage(img);
    texture = LoadTextureFromImage(img);
    loaded = texture.id != 0;
    if (loaded) {
        SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    } else {
        bolt::log::Warning("RENDER: failed to upload destroyed base texture");
    }
    UnloadImage(img);
    return loaded;
}

bool BuildDamagedBaseTextureFromHealthy(const Texture2D& healthyTexture, const EnemyBase& base, Texture2D& outTexture) {
    if (healthyTexture.id == 0) {
        return false;
    }
    Image healthyImage = LoadImageFromTexture(healthyTexture);
    if (healthyImage.data == nullptr || healthyImage.width <= 0 || healthyImage.height <= 0) {
        return false;
    }
    const int leftInsetPx = BaseSideInsetPixelsFromDamage(base.leftSegmentHealth);
    const int rightInsetPx = BaseSideInsetPixelsFromDamage(base.rightSegmentHealth);
    const int topInsetPx = BaseSideInsetPixelsFromDamage(base.topSegmentHealth);
    const int bottomInsetPx = BaseSideInsetPixelsFromDamage(base.bottomSegmentHealth);
    const int visibleWidthPx = std::max(1, kBaseSizePx - leftInsetPx - rightInsetPx);
    const int visibleHeightPx = std::max(1, kBaseSizePx - topInsetPx - bottomInsetPx);

    // Keep removed pixels transparent so the first pass (destroyed base texture) shows through.
    Image outputImage = GenImageColor(kBaseSizePx, kBaseSizePx, BLANK);
    const Rectangle sourceRect{
        static_cast<float>(leftInsetPx),
        static_cast<float>(topInsetPx),
        static_cast<float>(visibleWidthPx),
        static_cast<float>(visibleHeightPx),
    };
    Image clippedImage = ImageFromImage(healthyImage, sourceRect);
    if (clippedImage.data != nullptr && clippedImage.width > 0 && clippedImage.height > 0) {
        const Rectangle clippedRect{
            0.0F,
            0.0F,
            static_cast<float>(clippedImage.width),
            static_cast<float>(clippedImage.height),
        };
        const Rectangle destinationRect{
            static_cast<float>(leftInsetPx),
            static_cast<float>(topInsetPx),
            static_cast<float>(visibleWidthPx),
            static_cast<float>(visibleHeightPx),
        };
        ImageDraw(&outputImage, clippedImage, clippedRect, destinationRect, WHITE);
    }
    UnloadImage(clippedImage);
    UnloadImage(healthyImage);

    outTexture = LoadTextureFromImage(outputImage);
    const bool loaded = outTexture.id != 0;
    if (loaded) {
        SetTextureFilter(outTexture, TEXTURE_FILTER_POINT);
    }
    UnloadImage(outputImage);
    return loaded;
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

    constexpr Color kFlowArrowColor{0, 56, 0, 230};
    const float headSize = std::clamp(arrowLenPx * 0.22F, 2.0F, 6.0F);
    DrawLineEx(tail, tip, 2.0F, kFlowArrowColor);
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

void DrawBaseDistanceLabel(
    const Camera2D& camera,
    int cellX,
    int cellY,
    int cellSizeUnits,
    int baseDistanceCells) {
    constexpr int kDebugFontSize = 10;
    constexpr Color kDebugTextColor{104, 0, 144, 235};
    constexpr Color kDebugTextShadow{0, 0, 0, 200};
    char text[12];
    std::snprintf(text, sizeof(text), "%d", baseDistanceCells);
    const int textWidth = MeasureText(text, kDebugFontSize);
    const float centerX =
        (static_cast<float>(cellX) + 0.5F) * static_cast<float>(cellSizeUnits);
    const float centerY =
        (static_cast<float>(cellY) + 0.5F) * static_cast<float>(cellSizeUnits);
    const Vector2 centerScreen = GetWorldToScreen2D(Vector2{centerX, centerY}, camera);
    const int drawX = RoundToInt(centerScreen.x - static_cast<float>(textWidth) * 0.5F);
    const int drawY = RoundToInt(centerScreen.y - static_cast<float>(kDebugFontSize) * 0.5F);
    DrawText(text, drawX + 1, drawY + 1, kDebugFontSize, kDebugTextShadow);
    DrawText(text, drawX, drawY, kDebugFontSize, kDebugTextColor);
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

// Builds the 8 direction frames from the cardinal (up) and diagonal (45 deg) cells by rotating
// both clockwise to cover all eight directions. Caller owns and must unload the returned images.
std::array<Image, 8> BuildEightDirectionFrames(const Image& upCell, const Image& deg45Cell) {
    std::array<Image, 8> frames{};
    frames[0] = ImageCopy(upCell);
    frames[1] = ImageCopy(deg45Cell);
    for (std::size_t frameIndex = 2; frameIndex < frames.size(); ++frameIndex) {
        frames[frameIndex] = ImageCopy(frames[frameIndex - 2]);
        ImageRotateCW(&frames[frameIndex]);
    }
    return frames;
}

// XOR-combines two single-bit cells: a pixel is drawn (in color) only where exactly one of the
// two cells is opaque, so green-over-green overlap becomes transparent.
Image XorCells(const Image& cellA, const Image& cellB, Color color) {
    Image combined = GenImageColor(kSpriteSheetCellSize, kSpriteSheetCellSize, BLANK);
    Color* combinedPixels = static_cast<Color*>(combined.data);
    const Color* pixelsA = static_cast<const Color*>(cellA.data);
    const Color* pixelsB = static_cast<const Color*>(cellB.data);
    if (combinedPixels == nullptr || pixelsA == nullptr || pixelsB == nullptr) {
        return combined;
    }
    const int pixelCount = kSpriteSheetCellSize * kSpriteSheetCellSize;
    for (int i = 0; i < pixelCount; ++i) {
        const bool opaqueA = pixelsA[i].a > 0;
        const bool opaqueB = pixelsB[i].a > 0;
        if (opaqueA == opaqueB) {
            combinedPixels[i] = BLANK;
            continue;
        }
        combinedPixels[i] = color;
        combinedPixels[i].a = opaqueA ? pixelsA[i].a : pixelsB[i].a;
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

int EnemyTankTextureSheetRow(const EnemyTank& enemy) {
    if (enemy.type == EnemyType::Drone) {
        return (enemy.aiMode == EnemyAiMode::Watch || enemy.aiMode == EnemyAiMode::Defend) ? 0 : 1;
    }
    return EnemyTypeIndex(enemy.type) + 1;
}
}  // namespace

bool Renderer2D::LoadResources() {
    UnloadResources();
    for (Vector2& offset : playerTankFrameOffsetsPixels_) {
        offset = Vector2{0.0F, 0.0F};
    }
    baseHealthyTexture_ = Texture2D{};
    baseHealthyTextureLoaded_ = false;
    for (Texture2D& texture : baseDamagedTextures_) {
        texture = Texture2D{};
    }
    for (bool& loaded : baseDamagedTextureLoaded_) {
        loaded = false;
    }
    for (BaseHealthSnapshot& snapshot : baseHealthSnapshots_) {
        snapshot = BaseHealthSnapshot{};
    }
    for (bool& disabled : baseDamageCacheDisabled_) {
        disabled = false;
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

    // Body (hull) row and barrel (turret) row are baked into a single sheet of every hull/turret
    // rotation combination, XOR-composited so green-over-green overlap drops out (keeping the turret
    // outline visible) while still letting the turret aim independently of the hull heading.
    Image playerBodyUp = ExtractSpriteCell(sourceSheet, 0, kPlayerBodyRowIndex, kSpriteSheetCellSize);
    Image playerBody45 = ExtractSpriteCell(sourceSheet, 1, kPlayerBodyRowIndex, kSpriteSheetCellSize);
    Image playerBarrelUp = ExtractSpriteCell(sourceSheet, 0, kPlayerBarrelRowIndex, kSpriteSheetCellSize);
    Image playerBarrel45 = ExtractSpriteCell(sourceSheet, 1, kPlayerBarrelRowIndex, kSpriteSheetCellSize);
    FillOpaquePixelsColor(playerBodyUp, kPlayerColor);
    FillOpaquePixelsColor(playerBody45, kPlayerColor);
    FillOpaquePixelsColor(playerBarrelUp, kPlayerColor);
    FillOpaquePixelsColor(playerBarrel45, kPlayerColor);

    const std::array<Image, 8> hullFrames = BuildEightDirectionFrames(playerBodyUp, playerBody45);
    const std::array<Image, 8> turretFrames = BuildEightDirectionFrames(playerBarrelUp, playerBarrel45);
    UnloadImage(playerBarrel45);
    UnloadImage(playerBarrelUp);
    UnloadImage(playerBody45);
    UnloadImage(playerBodyUp);

    const int directionCount = kEnemyTankDirectionCount;
    // Hull-only sheet drives the per-hull-frame pivot offsets so the body stays put as the turret turns.
    Image hullSheet = GenImageColor(directionCount * kSpriteSheetCellSize, kSpriteSheetCellSize, BLANK);
    for (int hullDir = 0; hullDir < directionCount; ++hullDir) {
        DrawSpriteCell(hullSheet, hullFrames[static_cast<std::size_t>(hullDir)], hullDir, 0, kSpriteSheetCellSize);
    }
    // Combined sheet indexed by (hullDir * directionCount + turretDir).
    Image playerSheet = GenImageColor(
        directionCount * directionCount * kSpriteSheetCellSize, kSpriteSheetCellSize, BLANK);
    for (int hullDir = 0; hullDir < directionCount; ++hullDir) {
        for (int turretDir = 0; turretDir < directionCount; ++turretDir) {
            Image combined = XorCells(
                hullFrames[static_cast<std::size_t>(hullDir)],
                turretFrames[static_cast<std::size_t>(turretDir)],
                kPlayerColor);
            DrawSpriteCell(playerSheet, combined, hullDir * directionCount + turretDir, 0, kSpriteSheetCellSize);
            UnloadImage(combined);
        }
    }
    for (int frameIndex = 0; frameIndex < directionCount; ++frameIndex) {
        UnloadImage(hullFrames[static_cast<std::size_t>(frameIndex)]);
        UnloadImage(turretFrames[static_cast<std::size_t>(frameIndex)]);
    }

    playerTankSheet_ = LoadTextureFromImage(playerSheet);
    playerTankSheetLoaded_ = playerTankSheet_.id != 0;
    if (playerTankSheetLoaded_) {
        SetTextureFilter(playerTankSheet_, TEXTURE_FILTER_POINT);
        playerTankFrameSizePx_ = kSpriteSheetCellSize;
        playerTankFrameCount_ = directionCount;
        const int frameCountToMeasure =
            std::min(playerTankFrameCount_, static_cast<int>(playerTankFrameOffsetsPixels_.size()));
        for (int frameIndex = 0; frameIndex < frameCountToMeasure; ++frameIndex) {
            playerTankFrameOffsetsPixels_[static_cast<std::size_t>(frameIndex)] =
                ComputeFramePivotOffsetPixels(hullSheet, frameIndex, playerTankFrameSizePx_);
        }
    } else {
        bolt::log::Warning("RENDER: failed to create player spritesheet texture from sprites.png");
    }

    UnloadImage(playerSheet);
    UnloadImage(hullSheet);

    Image enemySheet = GenImageColor(
        kEnemyTankDirectionCount * kEnemySpriteSheetCellSize,
        Renderer2D::kEnemyTankSheetRowCount * kEnemySpriteSheetCellSize,
        BLANK);
    FillOpaquePixelsColor(sourceSheet, WHITE);
    const std::array<Color, Renderer2D::kEnemyTankSheetRowCount> kEnemySheetRowColors{
        kDroneColor, kDroneColor, kTorpedoColor, kHunterColor, kAssassinColor};
    for (int sheetRow = 0; sheetRow < Renderer2D::kEnemyTankSheetRowCount; ++sheetRow) {
        const int sourceRow = EnemySheetSourceRow(sheetRow);
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

        DrawSpriteCell(enemySheet, frame0, 0, sheetRow, kEnemySpriteSheetCellSize);
        DrawSpriteCell(enemySheet, frame1, 1, sheetRow, kEnemySpriteSheetCellSize);
        DrawSpriteCell(enemySheet, frame2, 2, sheetRow, kEnemySpriteSheetCellSize);
        DrawSpriteCell(enemySheet, frame3, 3, sheetRow, kEnemySpriteSheetCellSize);
        DrawSpriteCell(enemySheet, frame4, 4, sheetRow, kEnemySpriteSheetCellSize);
        DrawSpriteCell(enemySheet, frame5, 5, sheetRow, kEnemySpriteSheetCellSize);
        DrawSpriteCell(enemySheet, frame6, 6, sheetRow, kEnemySpriteSheetCellSize);
        DrawSpriteCell(enemySheet, frame7, 7, sheetRow, kEnemySpriteSheetCellSize);

        // Pre-colorize this type's row so we can draw with WHITE tint and avoid batch flushes.
        const Color typeColor = kEnemySheetRowColors[static_cast<std::size_t>(sheetRow)];
        Color* sheetPixels = static_cast<Color*>(enemySheet.data);
        if (sheetPixels != nullptr) {
            const int rowStartY = sheetRow * kEnemySpriteSheetCellSize;
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

    UnloadImage(sourceSheet);

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
    if (baseHealthyTextureLoaded_) {
        UnloadTexture(baseHealthyTexture_);
        baseHealthyTextureLoaded_ = false;
        baseHealthyTexture_ = Texture2D{};
    }
    for (std::size_t i = 0; i < baseDamagedTextures_.size(); ++i) {
        if (!baseDamagedTextureLoaded_[i]) {
            continue;
        }
        UnloadTexture(baseDamagedTextures_[i]);
        baseDamagedTextureLoaded_[i] = false;
        baseDamagedTextures_[i] = Texture2D{};
        baseHealthSnapshots_[i] = BaseHealthSnapshot{};
        baseDamageCacheDisabled_[i] = false;
    }
    if (baseDestroyedTextureLoaded_) {
        UnloadTexture(baseDestroyedTexture_);
        baseDestroyedTextureLoaded_ = false;
        baseDestroyedTexture_ = Texture2D{};
    }
}

void Renderer2D::DrawWorld(
    const GameState& state,
    const AppConfig& config,
    bool reserveHudViewport) {
    profiling::ScopedProfile worldScope(profiling::Scope::RenderWorld, true);
    const int worldWidth =
        reserveHudViewport ? (config.screenWidth - ComputeHudWidth(config)) : config.screenWidth;
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

    if (state.gameplayPhase == GameplayPhase::Starting) {
        EnsureHealthyBaseTexture(baseHealthyTexture_, baseHealthyTextureLoaded_);
        EnsureDestroyedBaseTexture(baseDestroyedTexture_, baseDestroyedTextureLoaded_);
        for (std::size_t i = 0; i < baseDamagedTextures_.size(); ++i) {
            if (baseDamagedTextureLoaded_[i]) {
                UnloadTexture(baseDamagedTextures_[i]);
                baseDamagedTextureLoaded_[i] = false;
                baseDamagedTextures_[i] = Texture2D{};
            }
            baseHealthSnapshots_[i] = BaseHealthSnapshot{};
            baseDamageCacheDisabled_[i] = false;
        }
    }

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

    if (state.world.evacObjectiveActive) {
        const float flickerPhase =
            std::fmod(GetTime(), GameplayConstants::kEvacZoneFlickerCycleSeconds);
        if (flickerPhase < (GameplayConstants::kEvacZoneFlickerCycleSeconds * 0.5F)) {
            const float halfSize = GameplayConstants::kEvacZoneSizeUnits * 0.5F;
            const Vector2 topLeft = GetWorldToScreen2D(
                Vector2{
                    state.world.evacZoneCenter.x - halfSize,
                    state.world.evacZoneCenter.y - halfSize},
                camera);
            const Vector2 bottomRight = GetWorldToScreen2D(
                Vector2{
                    state.world.evacZoneCenter.x + halfSize,
                    state.world.evacZoneCenter.y + halfSize},
                camera);
            const float left = std::min(topLeft.x, bottomRight.x);
            const float right = std::max(topLeft.x, bottomRight.x);
            const float top = std::min(topLeft.y, bottomRight.y);
            const float bottom = std::max(topLeft.y, bottomRight.y);
            DrawRectangleLinesEx(
                Rectangle{
                    .x = left,
                    .y = top,
                    .width = right - left,
                    .height = bottom - top,
                },
                2.0F,
                GREEN);
        }
    }

    const bool showFlowField =
        state.menuSettings.debugInfo && state.world.navigationCache.playerFlowField.HasBuild();
    const bool showBaseDistanceField =
        state.menuSettings.debugInfo && state.world.navigationCache.baseDistanceField.HasBuild();
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
    if (showBaseDistanceField) {
        const auto& baseDistanceField = state.world.navigationCache.baseDistanceField;
        for (int cellY = minCellY; cellY <= maxCellY; ++cellY) {
            for (int cellX = minCellX; cellX <= maxCellX; ++cellX) {
                const int baseDistanceCells = baseDistanceField.DistanceAtCell(cellX, cellY);
                if (baseDistanceCells == std::numeric_limits<int>::max()) {
                    continue;
                }
                DrawBaseDistanceLabel(
                    camera,
                    cellX,
                    cellY,
                    state.world.maze.cellSizeUnits,
                    baseDistanceCells);
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
            for (std::size_t baseIndex = 0; baseIndex < state.world.enemyBases.size(); ++baseIndex) {
                const EnemyBase& base = state.world.enemyBases[baseIndex];
                const Vector2 baseScreenPosition = WorldToSnappedScreen(base.position, camera);
                const int topLeftX = RoundToInt(baseScreenPosition.x) - kBaseHalfPx;
                const int topLeftY = RoundToInt(baseScreenPosition.y) - kBaseHalfPx;
                if (base.destroyed) {
                    if (baseIndex < baseDamagedTextures_.size() && baseDamagedTextureLoaded_[baseIndex]) {
                        UnloadTexture(baseDamagedTextures_[baseIndex]);
                        baseDamagedTextures_[baseIndex] = Texture2D{};
                        baseDamagedTextureLoaded_[baseIndex] = false;
                    }
                    if (EnsureDestroyedBaseTexture(baseDestroyedTexture_, baseDestroyedTextureLoaded_)) {
                        DrawTexture(baseDestroyedTexture_, topLeftX, topLeftY, WHITE);
                    }
                } else if (!base.destroyed) {
                    const bool hasCacheSlot = baseIndex < baseDamagedTextures_.size();
                    const bool fullyHealthy = IsBaseFullyHealthy(base);
                    if (fullyHealthy) {
                        if (hasCacheSlot && baseDamagedTextureLoaded_[baseIndex]) {
                            UnloadTexture(baseDamagedTextures_[baseIndex]);
                            baseDamagedTextures_[baseIndex] = Texture2D{};
                            baseDamagedTextureLoaded_[baseIndex] = false;
                        }
                        if (hasCacheSlot) {
                            baseDamageCacheDisabled_[baseIndex] = false;
                        }
                        if (EnsureHealthyBaseTexture(baseHealthyTexture_, baseHealthyTextureLoaded_)) {
                            DrawTexture(baseHealthyTexture_, topLeftX, topLeftY, WHITE);
                        }
                    } else {
                        if (EnsureDestroyedBaseTexture(baseDestroyedTexture_, baseDestroyedTextureLoaded_)) {
                            DrawTexture(baseDestroyedTexture_, topLeftX, topLeftY, WHITE);
                        }
                        if (hasCacheSlot) {
                            BaseHealthSnapshot& snapshot = baseHealthSnapshots_[baseIndex];
                            const bool hasSnapshot = snapshot.initialized;
                            const bool healed =
                                hasSnapshot &&
                                (base.topSegmentHealth > snapshot.top ||
                                 base.rightSegmentHealth > snapshot.right ||
                                 base.bottomSegmentHealth > snapshot.bottom ||
                                 base.leftSegmentHealth > snapshot.left);
                            const bool tookDamage =
                                hasSnapshot &&
                                (base.topSegmentHealth < snapshot.top ||
                                 base.rightSegmentHealth < snapshot.right ||
                                 base.bottomSegmentHealth < snapshot.bottom ||
                                 base.leftSegmentHealth < snapshot.left);

                            if (healed && baseDamagedTextureLoaded_[baseIndex]) {
                                UnloadTexture(baseDamagedTextures_[baseIndex]);
                                baseDamagedTextures_[baseIndex] = Texture2D{};
                                baseDamagedTextureLoaded_[baseIndex] = false;
                            }

                            // Rebuild the front/overlay texture whenever segment health changes
                            // (damage or repair) so the alive layer stays in sync with the back layer.
                            const bool segmentHealthChanged = healed || tookDamage;
                            const bool shouldTryCache =
                                !baseDamageCacheDisabled_[baseIndex] &&
                                EnsureHealthyBaseTexture(baseHealthyTexture_, baseHealthyTextureLoaded_) &&
                                (!baseDamagedTextureLoaded_[baseIndex] || segmentHealthChanged);
                            if (shouldTryCache) {
                                if (baseDamagedTextureLoaded_[baseIndex]) {
                                    UnloadTexture(baseDamagedTextures_[baseIndex]);
                                    baseDamagedTextures_[baseIndex] = Texture2D{};
                                    baseDamagedTextureLoaded_[baseIndex] = false;
                                }
                                baseDamagedTextureLoaded_[baseIndex] = BuildDamagedBaseTextureFromHealthy(
                                    baseHealthyTexture_,
                                    base,
                                    baseDamagedTextures_[baseIndex]);
                            }

                            if (baseDamagedTextureLoaded_[baseIndex]) {
                                DrawTexture(baseDamagedTextures_[baseIndex], topLeftX, topLeftY, WHITE);
                            }

                            snapshot.top = base.topSegmentHealth;
                            snapshot.right = base.rightSegmentHealth;
                            snapshot.bottom = base.bottomSegmentHealth;
                            snapshot.left = base.leftSegmentHealth;
                            snapshot.initialized = true;
                        } else if (EnsureHealthyBaseTexture(baseHealthyTexture_, baseHealthyTextureLoaded_)) {
                            DrawTexture(baseHealthyTexture_, topLeftX, topLeftY, WHITE);
                        }

                    }
                    const float spawnGrowthProgress01 = base.spawnPreparationActive
                        ? 1.0F -
                              (std::max(0.0F, base.spawnPreparationRemainingSeconds) /
                               GameplayConstants::kBaseSpawnCoreGrowDurationSeconds)
                        : 0.0F;
                    const BaseCoreVisualState coreVisual =
                        ComputeAnimatedBaseCoreState(base, spawnGrowthProgress01);
                    if (coreVisual.animated) {
                        DrawCircleV(baseScreenPosition, kBaseCoreClearRadiusPx, kBackgroundColor);
                        DrawCircleV(baseScreenPosition, coreVisual.radiusPixels, coreVisual.color);
                    }
                }
            }

            const int enemySizePixels = kEnemyRenderSizePx;
            const int enemyHalfPixels = enemySizePixels / 2;
            const Vector2 playerScreenPosition = WorldToSnappedScreen(state.world.player.position, camera);
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
                    const int sheetRow = EnemyTankTextureSheetRow(enemy);
                    const Rectangle sourceRect{
                        .x = static_cast<float>(directionIndex * kEnemyTankFrameSizePx),
                        .y = static_cast<float>(sheetRow * kEnemyTankFrameSizePx),
                        .width = static_cast<float>(kEnemyTankFrameSizePx),
                        .height = static_cast<float>(kEnemyTankFrameSizePx),
                    };
                    DrawTexturePro(enemyTankSheet_, sourceRect, destRect, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
                } else {
                    DrawRectangleRec(destRect, EnemyColorForType(enemy.type));
                }
                if (state.menuSettings.debugInfo) {
                    if (enemy.seesPlayer) {
                        DrawLine(
                            static_cast<int>(enemyScreenPosition.x),
                            static_cast<int>(enemyScreenPosition.y),
                            static_cast<int>(playerScreenPosition.x),
                            static_cast<int>(playerScreenPosition.y),
                            RED);
                    }
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
                    } else if (enemy.type == EnemyType::Torpedo &&
                               enemy.torpedoFlyPathActive &&
                               enemy.torpedoFlySegmentIndex >= 0 &&
                               enemy.torpedoFlySegmentIndex < enemy.torpedoFlySegmentCount) {
                        const Vec2f segmentTarget =
                            enemy.torpedoFlySegmentPoints[static_cast<std::size_t>(enemy.torpedoFlySegmentIndex)];
                        const Vector2 targetScreenPosition = WorldToSnappedScreen(segmentTarget, camera);
                        DrawLine(
                            static_cast<int>(enemyScreenPosition.x),
                            static_cast<int>(enemyScreenPosition.y),
                            static_cast<int>(targetScreenPosition.x),
                            static_cast<int>(targetScreenPosition.y),
                            YELLOW);
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
        const int hullFrameIndex =
            PlayerFrameIndexFromHeading(state.world.player.hullHeadingRadians, playerTankFrameCount_);
        const int turretFrameIndex =
            PlayerFrameIndexFromHeading(state.world.player.turretHeadingRadians, playerTankFrameCount_);
        // Combined sheet is laid out as (hullDir * directionCount + turretDir).
        const int frameIndex = hullFrameIndex * playerTankFrameCount_ + turretFrameIndex;
        const Vector2 sourceOffsetPixels =
            playerTankFrameOffsetsPixels_[static_cast<std::size_t>(hullFrameIndex)];
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

    if (state.world.enemyAlarmActive) {
        constexpr int kAlarmIndicatorFontSize = 20;
        constexpr int kAlarmIndicatorTopInsetPx = 6;
        constexpr float kAlarmIndicatorCircleRadiusPx = 14.0F;
        constexpr float kAlarmIndicatorRingThicknessPx = 2.0F;
        const int centerX = RoundToInt(worldViewport.x + worldViewport.width * 0.5F);
        const int centerY = RoundToInt(worldViewport.y + kAlarmIndicatorCircleRadiusPx + kAlarmIndicatorTopInsetPx);
        const int exclamationWidth = MeasureText("!", kAlarmIndicatorFontSize);
        const int textX = centerX - exclamationWidth / 2;
        const int textY = centerY - kAlarmIndicatorFontSize / 2;
        DrawCircleLines(centerX, centerY, kAlarmIndicatorCircleRadiusPx, kAlarmIndicatorColor);
        DrawCircleLines(centerX, centerY, kAlarmIndicatorCircleRadiusPx - kAlarmIndicatorRingThicknessPx, kAlarmIndicatorColor);
        DrawText("!", textX, textY, kAlarmIndicatorFontSize, kAlarmIndicatorColor);
    }

    EndScissorMode();
}

void Renderer2D::DrawMenuBackground(
    const MazeState& maze,
    const std::vector<EnemyTank>& enemies,
    const Vec2f& cameraTarget,
    const AppConfig& config) {
    GameState state{};
    state.gameplayPhase = GameplayPhase::Active;
    state.world.maze = maze;
    state.world.enemies = enemies;
    state.world.player.alive = false;
    state.world.panModeActive = true;
    state.world.panTarget = cameraTarget;
    state.menuSettings.debugInfo = false;
    DrawWorld(state, config, false);
}
