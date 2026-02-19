#include "ui/HudPanel.h"

#include <algorithm>
#include <cmath>
#include "platform/PlayerFigure.h"
#include "raylib.h"

void HudPanel::Render(const GameState& state, const AppConfig& config) const {
    const int hudWidth = ComputeHudWidth(config);
    const Rectangle panel = {
        .x = static_cast<float>(config.screenWidth - hudWidth),
        .y = 0.0F,
        .width = static_cast<float>(hudWidth),
        .height = static_cast<float>(config.screenHeight),
    };

    DrawRectangleRec(panel, Color{27, 31, 39, 255});
    DrawLine(
        static_cast<int>(panel.x),
        0,
        static_cast<int>(panel.x),
        config.screenHeight,
        Color{58, 66, 80, 255});

    const int panelX = static_cast<int>(panel.x);
    const int contentPadding = 10;
    const int contentX = panelX + contentPadding;
    const int contentWidth = hudWidth - (contentPadding * 2);
    int cursorY = 8;

    // 1) BOLO text
    DrawText("BOLO", contentX, cursorY, 40, PURPLE);
    cursorY += 46;

    // 2) SCORE 0000 display
    DrawRectangle(contentX, cursorY, contentWidth, 36, BLACK);
    DrawRectangleLines(contentX, cursorY, contentWidth, 36, RAYWHITE);
    DrawText(TextFormat("SCORE %04d", state.world.score), contentX + 8, cursorY + 9, 20, RAYWHITE);
    cursorY += 44;

    // 3) lives icons
    const int livesY = cursorY;
    const int iconGap = 6;
    const int iconSize = (contentWidth - (iconGap * 3)) / 4;
    const int livesToRender = std::max(0, std::min(4, state.world.player.lives));
    for (int i = 0; i < livesToRender; ++i) {
        const int iconX = contentX + (i * (iconSize + iconGap));
        DrawPlayerFigure(
            Vector2{
                static_cast<float>(iconX) + static_cast<float>(iconSize) * 0.5F,
                static_cast<float>(livesY) + static_cast<float>(iconSize) * 0.5F,
            },
            static_cast<float>(iconSize),
            0.0F,
            RAYWHITE);
    }
    cursorY += iconSize + 12;

    // 4) Fuel bar
    const float fuelClamped = std::max(0.0F, std::min(100.0F, state.world.player.fuel));
    const int fuelWidth = static_cast<int>((fuelClamped / 100.0F) * static_cast<float>(contentWidth));
    DrawRectangle(contentX, cursorY, contentWidth, 12, DARKGRAY);
    DrawRectangle(contentX, cursorY, fuelWidth, 12, ORANGE);
    DrawRectangleLines(contentX, cursorY, contentWidth, 12, RAYWHITE);
    cursorY += 20;

    // 5) Velocity bar
    constexpr float kPlayerFullVelocityUnitsPerSecond = 20.0F;
    const float speed = std::sqrt(
        state.world.player.velocity.x * state.world.player.velocity.x +
        state.world.player.velocity.y * state.world.player.velocity.y);
    const float speedNormalized = std::max(0.0F, std::min(1.0F, speed / kPlayerFullVelocityUnitsPerSecond));
    const int speedWidth = static_cast<int>(speedNormalized * static_cast<float>(contentWidth));
    DrawRectangle(contentX, cursorY, contentWidth, 12, DARKGRAY);
    DrawRectangle(contentX, cursorY, speedWidth, 12, SKYBLUE);
    DrawRectangleLines(contentX, cursorY, contentWidth, 12, RAYWHITE);
    cursorY += 20;

    // 6) Base direction quadrants + player heading indicator
    const int blockGap = 8;
    const int leftBlockSize = (contentWidth - blockGap) / 2;
    DrawRectangle(contentX, cursorY, leftBlockSize, leftBlockSize, BLACK);
    DrawRectangleLines(contentX, cursorY, leftBlockSize, leftBlockSize, RAYWHITE);
    const int quadrantCell = leftBlockSize / 2;
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
    const Color dimQuadrant = Color{18, 60, 26, 255};
    const Color brightQuadrant = Color{160, 255, 120, 255};
    DrawRectangle(
        contentX + 2,
        cursorY + 2,
        quadrantCell - 3,
        quadrantCell - 3,
        highlightedQuadrant == 0 ? brightQuadrant : dimQuadrant);
    DrawRectangle(
        contentX + quadrantCell + 1,
        cursorY + 2,
        quadrantCell - 3,
        quadrantCell - 3,
        highlightedQuadrant == 1 ? brightQuadrant : dimQuadrant);
    DrawRectangle(
        contentX + 2,
        cursorY + quadrantCell + 1,
        quadrantCell - 3,
        quadrantCell - 3,
        highlightedQuadrant == 2 ? brightQuadrant : dimQuadrant);
    DrawRectangle(
        contentX + quadrantCell + 1,
        cursorY + quadrantCell + 1,
        quadrantCell - 3,
        quadrantCell - 3,
        highlightedQuadrant == 3 ? brightQuadrant : dimQuadrant);

    const int compassX = contentX + leftBlockSize + blockGap;
    DrawRectangle(compassX, cursorY, leftBlockSize, leftBlockSize, Color{237, 126, 188, 255});
    DrawRectangleLines(compassX, cursorY, leftBlockSize, leftBlockSize, RAYWHITE);
    const int compassPadding = 4;
    DrawRectangle(
        compassX + compassPadding,
        cursorY + compassPadding,
        leftBlockSize - compassPadding * 2,
        leftBlockSize - compassPadding * 2,
        Color{27, 31, 39, 255});
    DrawCircle(
        compassX + (leftBlockSize / 2),
        cursorY + (leftBlockSize / 2),
        static_cast<float>(leftBlockSize) / 3.0F,
        BLACK);
    const float headingX = std::sin(state.world.player.hullHeadingRadians);
    const float headingY = -std::cos(state.world.player.hullHeadingRadians);
    const int centerX = compassX + leftBlockSize / 2;
    const int centerY = cursorY + leftBlockSize / 2;
    const float armLength = static_cast<float>(leftBlockSize) * 0.28F;
    DrawLine(
        centerX,
        centerY,
        static_cast<int>(static_cast<float>(centerX) + headingX * armLength),
        static_cast<int>(static_cast<float>(centerY) + headingY * armLength),
        RAYWHITE);
    cursorY += leftBlockSize + 10;

    // 7) Live square map (player dot)
    const int mapSize = contentWidth;
    DrawRectangle(contentX, cursorY, mapSize, mapSize, BLACK);
    DrawRectangleLines(contentX, cursorY, mapSize, mapSize, RAYWHITE);
    const float mazeWidth = static_cast<float>(state.world.maze.widthCells * state.world.maze.cellSizeUnits);
    const float mazeHeight = static_cast<float>(state.world.maze.heightCells * state.world.maze.cellSizeUnits);
    const float normalizedX = mazeWidth > 0.0F ? state.world.player.position.x / mazeWidth : 0.5F;
    const float normalizedY = mazeHeight > 0.0F ? state.world.player.position.y / mazeHeight : 0.5F;
    const int dotX = contentX + static_cast<int>(
        std::max(0.0F, std::min(1.0F, normalizedX)) * static_cast<float>(mapSize - 1));
    const int dotY = cursorY + static_cast<int>(
        std::max(0.0F, std::min(1.0F, normalizedY)) * static_cast<float>(mapSize - 1));
    DrawCircle(dotX, dotY, 3.0F, PURPLE);

    if (state.world.levelCleared || state.world.levelClearMessageSeconds > 0.0F) {
        DrawText("LEVEL CLEARED", contentX + 6, cursorY + mapSize + 8, 20, LIME);
    }
}
