#include "platform/Renderer2D.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <array>
#include <filesystem>
#include "platform/PlayerFigure.h"
#include "raylib.h"

namespace {
constexpr int kSourcePlayerFrameCount = 6;
constexpr int kSourcePlayerFrameSize = 20;

float PixelsToWorldUnits(float pixels) {
    return pixels / static_cast<float>(GameplayConstants::kPixelsPerUnit);
}

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
    const float offsetX = std::round(frameCenter - contentCenterX);
    const float offsetY = std::round(frameCenter - contentCenterY);
    return Vector2{
        offsetX,
        offsetY,
    };
}

void CopyFrameFromStripToSheet(
    Image& destination,
    int destinationFrameIndex,
    const Image& stripImage,
    int sourceFrameIndex,
    bool verticalStrip,
    bool reverseOrder,
    int frameCount,
    int frameSizePx) {
    const int effectiveSourceIndex = reverseOrder ? (frameCount - 1 - sourceFrameIndex) : sourceFrameIndex;
    const Rectangle sourceRect{
        .x = verticalStrip ? 0.0F : static_cast<float>(effectiveSourceIndex * frameSizePx),
        .y = verticalStrip ? static_cast<float>(effectiveSourceIndex * frameSizePx) : 0.0F,
        .width = static_cast<float>(frameSizePx),
        .height = static_cast<float>(frameSizePx),
    };
    Image frameImage = ImageFromImage(stripImage, sourceRect);
    const Rectangle frameRect{
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(frameSizePx),
        .height = static_cast<float>(frameSizePx),
    };
    const Rectangle destRect{
        .x = static_cast<float>(destinationFrameIndex * frameSizePx),
        .y = 0.0F,
        .width = static_cast<float>(frameSizePx),
        .height = static_cast<float>(frameSizePx),
    };
    ImageDraw(&destination, frameImage, frameRect, destRect, WHITE);
    UnloadImage(frameImage);
}

bool TryLoadImageAtPath(Image& image, const char* path) {
    if (!FileExists(path)) {
        return false;
    }
    image = LoadImage(path);
    return image.data != nullptr;
}
}  // namespace

bool Renderer2D::LoadResources() {
    UnloadResources();
    for (Vector2& offset : playerTankFrameOffsetsPixels_) {
        offset = Vector2{0.0F, 0.0F};
    }

    std::array<const char*, 5> candidatePaths = {
        "resources/textures/player_tank_sheet.png",
        "../resources/textures/player_tank_sheet.png",
        "../../resources/textures/player_tank_sheet.png",
        "../../../resources/textures/player_tank_sheet.png",
        "../../../../resources/textures/player_tank_sheet.png",
    };

    Image sourceSheet{};
    bool loaded = false;
    for (const char* path : candidatePaths) {
        if (TryLoadImageAtPath(sourceSheet, path)) {
            loaded = true;
            break;
        }
    }
    if (!loaded) {
        const char* applicationDirectory = GetApplicationDirectory();
        if (applicationDirectory != nullptr && applicationDirectory[0] != '\0') {
            std::filesystem::path base(applicationDirectory);
            for (int i = 0; i <= 4 && !loaded; ++i) {
                const std::filesystem::path candidate = base / "resources" / "textures" / "player_tank_sheet.png";
                if (TryLoadImageAtPath(sourceSheet, candidate.string().c_str())) {
                    loaded = true;
                    break;
                }
                if (!base.has_parent_path()) {
                    break;
                }
                base = base.parent_path();
            }
        }
    }
    if (!loaded) {
        TraceLog(LOG_WARNING, "RENDER: player_tank_sheet.png not found");
        return false;
    }

    if (sourceSheet.width != kSourcePlayerFrameCount * kSourcePlayerFrameSize ||
        sourceSheet.height != kSourcePlayerFrameSize) {
        TraceLog(
            LOG_WARNING,
            "RENDER: player_tank_sheet.png has unexpected size (%i x %i), expected %i x %i",
            sourceSheet.width,
            sourceSheet.height,
            kSourcePlayerFrameCount * kSourcePlayerFrameSize,
            kSourcePlayerFrameSize);
    }

    Image rotated90 = ImageCopy(sourceSheet);
    ImageRotateCW(&rotated90);
    Image rotated180 = ImageCopy(rotated90);
    ImageRotateCW(&rotated180);
    Image rotated270 = ImageCopy(rotated180);
    ImageRotateCW(&rotated270);

    Image combinedSheet = GenImageColor(
        kSourcePlayerFrameCount * 4 * kSourcePlayerFrameSize,
        kSourcePlayerFrameSize,
        BLANK);

    for (int i = 0; i < kSourcePlayerFrameCount; ++i) {
        CopyFrameFromStripToSheet(
            combinedSheet,
            i,
            sourceSheet,
            i,
            false,
            false,
            kSourcePlayerFrameCount,
            kSourcePlayerFrameSize);
        CopyFrameFromStripToSheet(
            combinedSheet,
            kSourcePlayerFrameCount + i,
            rotated90,
            i,
            true,
            false,
            kSourcePlayerFrameCount,
            kSourcePlayerFrameSize);
        CopyFrameFromStripToSheet(
            combinedSheet,
            kSourcePlayerFrameCount * 2 + i,
            rotated180,
            i,
            false,
            true,
            kSourcePlayerFrameCount,
            kSourcePlayerFrameSize);
        CopyFrameFromStripToSheet(
            combinedSheet,
            kSourcePlayerFrameCount * 3 + i,
            rotated270,
            i,
            true,
            true,
            kSourcePlayerFrameCount,
            kSourcePlayerFrameSize);
    }

    playerTankSheet_ = LoadTextureFromImage(combinedSheet);
    playerTankSheetLoaded_ = playerTankSheet_.id != 0;
    if (playerTankSheetLoaded_) {
        SetTextureFilter(playerTankSheet_, TEXTURE_FILTER_POINT);
        playerTankFrameSizePx_ = kSourcePlayerFrameSize;
        playerTankFrameCount_ = kSourcePlayerFrameCount * 4;
        const int frameCountToMeasure =
            std::min(playerTankFrameCount_, static_cast<int>(playerTankFrameOffsetsPixels_.size()));
        for (int frameIndex = 0; frameIndex < frameCountToMeasure; ++frameIndex) {
            playerTankFrameOffsetsPixels_[static_cast<std::size_t>(frameIndex)] =
                ComputeFramePivotOffsetPixels(combinedSheet, frameIndex, playerTankFrameSizePx_);
        }
    }

    UnloadImage(combinedSheet);
    UnloadImage(rotated270);
    UnloadImage(rotated180);
    UnloadImage(rotated90);
    UnloadImage(sourceSheet);

    return playerTankSheetLoaded_;
}

void Renderer2D::UnloadResources() {
    if (playerTankSheetLoaded_) {
        UnloadTexture(playerTankSheet_);
        playerTankSheetLoaded_ = false;
        playerTankSheet_ = Texture2D{};
    }
}

void Renderer2D::DrawWorld(const GameState& state, const AppConfig& config) {
    const int worldWidth = config.screenWidth - ComputeHudWidth(config);
    const Rectangle worldViewport = {
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(worldWidth),
        .height = static_cast<float>(config.screenHeight),
    };

    DrawRectangleRec(worldViewport, Color{20, 24, 30, 255});

    Camera2D camera{};
    camera.target = SnapWorldToPixelGrid(state.world.player.position);
    camera.offset = Vector2{
        std::round(worldViewport.width * 0.5F),
        std::round(worldViewport.height * 0.5F),
    };
    camera.rotation = 0.0F;
    camera.zoom = static_cast<float>(GameplayConstants::kPixelsPerUnit);

    BeginScissorMode(0, 0, worldWidth, config.screenHeight);
    const float mazeWidthUnits =
        static_cast<float>(state.world.maze.widthCells * state.world.maze.cellSizeUnits);
    const float mazeHeightUnits =
        static_cast<float>(state.world.maze.heightCells * state.world.maze.cellSizeUnits);
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
                    GRAY);
            }
            if (cell.westWall) {
                DrawVerticalWallPixels(
                    GetWorldToScreen2D(Vector2{left, top}, camera),
                    GetWorldToScreen2D(Vector2{left, bottom}, camera),
                    wallThicknessPixels,
                    GRAY);
            }
            if (x == state.world.maze.widthCells - 1 && cell.eastWall) {
                DrawVerticalWallPixels(
                    GetWorldToScreen2D(Vector2{right, top}, camera),
                    GetWorldToScreen2D(Vector2{right, bottom}, camera),
                    wallThicknessPixels,
                    GRAY);
            }
            if (y == state.world.maze.heightCells - 1 && cell.southWall) {
                DrawHorizontalWallPixels(
                    GetWorldToScreen2D(Vector2{left, bottom}, camera),
                    GetWorldToScreen2D(Vector2{right, bottom}, camera),
                    wallThicknessPixels,
                    GRAY);
            }
        }
    }

    const Vector2 borderTopLeft = GetWorldToScreen2D(Vector2{0.0F, 0.0F}, camera);
    const Vector2 borderTopRight = GetWorldToScreen2D(Vector2{mazeWidthUnits, 0.0F}, camera);
    const Vector2 borderBottomLeft = GetWorldToScreen2D(Vector2{0.0F, mazeHeightUnits}, camera);
    const Vector2 borderBottomRight = GetWorldToScreen2D(Vector2{mazeWidthUnits, mazeHeightUnits}, camera);
    DrawHorizontalWallPixels(borderTopLeft, borderTopRight, wallThicknessPixels, DARKGRAY);
    DrawHorizontalWallPixels(borderBottomLeft, borderBottomRight, wallThicknessPixels, DARKGRAY);
    DrawVerticalWallPixels(borderTopLeft, borderBottomLeft, wallThicknessPixels, DARKGRAY);
    DrawVerticalWallPixels(borderTopRight, borderBottomRight, wallThicknessPixels, DARKGRAY);

    const Vector2 playerRenderPosition = SnapWorldToPixelGrid(state.world.player.position);

    BeginMode2D(camera);

    for (const EnemyBase& base : state.world.enemyBases) {
        const float half = GameplayConstants::kEnemyBaseSizeUnits * 0.5F;
        DrawRectangleRec(
            Rectangle{
                .x = base.position.x - half,
                .y = base.position.y - half,
                .width = GameplayConstants::kEnemyBaseSizeUnits,
                .height = GameplayConstants::kEnemyBaseSizeUnits,
            },
            base.destroyed ? DARKGRAY : RED);
    }

    for (const EnemyTank& enemy : state.world.enemies) {
        if (!enemy.alive) {
            continue;
        }
        Color enemyColor = ORANGE;
        if (enemy.type == EnemyType::Drone) {
            enemyColor = ORANGE;
        } else if (enemy.type == EnemyType::Torpedo) {
            enemyColor = GOLD;
        } else if (enemy.type == EnemyType::Hunter) {
            enemyColor = RED;
        } else if (enemy.type == EnemyType::Assassin) {
            enemyColor = MAGENTA;
        }
        const float half = GameplayConstants::kEntitySizeUnits * 0.5F;
        DrawRectangleRec(
            Rectangle{
                .x = enemy.position.x - half,
                .y = enemy.position.y - half,
                .width = GameplayConstants::kEntitySizeUnits,
                .height = GameplayConstants::kEntitySizeUnits,
            },
            enemyColor);
    }

    for (const Projectile& projectile : state.world.projectiles) {
        if (!projectile.alive) {
            continue;
        }
        const Color color = projectile.owner == ProjectileOwner::Player ? SKYBLUE : YELLOW;
        DrawCircleV(Vector2{projectile.position.x, projectile.position.y}, 0.18F, color);
    }

    if (playerTankSheetLoaded_) {
        // Draw in screen space at fixed 20x20 to avoid platform-dependent camera scaling artifacts.
    } else {
        DrawPlayerFigure(
            playerRenderPosition,
            PixelsToWorldUnits(static_cast<float>(kSourcePlayerFrameSize)),
            state.world.player.hullHeadingRadians,
            GREEN);
    }

    EndMode2D();
    if (playerTankSheetLoaded_) {
        const int frameIndex = PlayerFrameIndexFromHeading(state.world.player.hullHeadingRadians, playerTankFrameCount_);
        const Vector2 frameOffsetPixels =
            playerTankFrameOffsetsPixels_[static_cast<std::size_t>(frameIndex)];
        const Rectangle sourceRect{
            .x = static_cast<float>(frameIndex * playerTankFrameSizePx_),
            .y = 0.0F,
            .width = static_cast<float>(playerTankFrameSizePx_),
            .height = static_cast<float>(playerTankFrameSizePx_),
        };
        const Vector2 playerScreenPosition = GetWorldToScreen2D(playerRenderPosition, camera);
        const Rectangle destRect{
            .x = static_cast<float>(RoundToInt(playerScreenPosition.x + frameOffsetPixels.x)),
            .y = static_cast<float>(RoundToInt(playerScreenPosition.y + frameOffsetPixels.y)),
            .width = static_cast<float>(playerTankFrameSizePx_),
            .height = static_cast<float>(playerTankFrameSizePx_),
        };
        const float halfFrame = static_cast<float>(playerTankFrameSizePx_) * 0.5F;
        DrawTexturePro(playerTankSheet_, sourceRect, destRect, Vector2{halfFrame, halfFrame}, 0.0F, WHITE);
    }
    EndScissorMode();
}
