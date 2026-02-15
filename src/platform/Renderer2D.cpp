#include "platform/Renderer2D.h"

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
    BeginMode2D(camera);

    const float mazeWidthUnits =
        static_cast<float>(state.world.maze.widthCells * state.world.maze.cellSizeUnits);
    const float mazeHeightUnits =
        static_cast<float>(state.world.maze.heightCells * state.world.maze.cellSizeUnits);
    const float wallThickness = GameplayConstants::kWallThicknessUnits;

    for (int y = 0; y < state.world.maze.heightCells; ++y) {
        for (int x = 0; x < state.world.maze.widthCells; ++x) {
            const MazeCell& cell =
                state.world.maze.cells[static_cast<std::size_t>(y * state.world.maze.widthCells + x)];
            const float left = static_cast<float>(x * state.world.maze.cellSizeUnits);
            const float top = static_cast<float>(y * state.world.maze.cellSizeUnits);
            const float right = left + static_cast<float>(state.world.maze.cellSizeUnits);
            const float bottom = top + static_cast<float>(state.world.maze.cellSizeUnits);
            if (cell.northWall) {
                DrawRectangleRec(
                    Rectangle{
                        .x = left,
                        .y = top - (wallThickness * 0.5F),
                        .width = static_cast<float>(state.world.maze.cellSizeUnits),
                        .height = wallThickness,
                    },
                    GRAY);
            }
            if (cell.westWall) {
                DrawRectangleRec(
                    Rectangle{
                        .x = left - (wallThickness * 0.5F),
                        .y = top,
                        .width = wallThickness,
                        .height = static_cast<float>(state.world.maze.cellSizeUnits),
                    },
                    GRAY);
            }
            if (x == state.world.maze.widthCells - 1 && cell.eastWall) {
                DrawRectangleRec(
                    Rectangle{
                        .x = right - (wallThickness * 0.5F),
                        .y = top,
                        .width = wallThickness,
                        .height = static_cast<float>(state.world.maze.cellSizeUnits),
                    },
                    GRAY);
            }
            if (y == state.world.maze.heightCells - 1 && cell.southWall) {
                DrawRectangleRec(
                    Rectangle{
                        .x = left,
                        .y = bottom - (wallThickness * 0.5F),
                        .width = static_cast<float>(state.world.maze.cellSizeUnits),
                        .height = wallThickness,
                    },
                    GRAY);
            }
        }
    }

    DrawRectangleLinesEx(
        Rectangle{.x = 0.0F, .y = 0.0F, .width = mazeWidthUnits, .height = mazeHeightUnits},
        wallThickness,
        DARKGRAY);

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
        const float half = GameplayConstants::kEntitySizeUnits * 0.5F;
        DrawRectangleRec(
            Rectangle{
                .x = enemy.position.x - half,
                .y = enemy.position.y - half,
                .width = GameplayConstants::kEntitySizeUnits,
                .height = GameplayConstants::kEntitySizeUnits,
            },
            ORANGE);
    }

    DrawPlayerFigure(
        ToVector2(state.world.player.position),
        GameplayConstants::kEntitySizeUnits,
        state.world.player.hullHeadingRadians,
        GREEN);

    EndMode2D();
    EndScissorMode();
}
