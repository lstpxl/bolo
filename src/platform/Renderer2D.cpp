#include "platform/Renderer2D.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include "platform/PlayerFigure.h"
#include "raylib.h"

namespace {
Vector2 ToVector2(const Vec2f& value) {
    return Vector2{value.x, value.y};
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
}  // namespace

void Renderer2D::DrawWorld(const GameState& state, const AppConfig& config) const {
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

    DrawPlayerFigure(
        ToVector2(state.world.player.position),
        GameplayConstants::kEntitySizeUnits,
        state.world.player.hullHeadingRadians,
        GREEN);

    EndMode2D();
    EndScissorMode();
}
