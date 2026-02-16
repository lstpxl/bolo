#include "ui/HudPanel.h"

#include "platform/PlayerFigure.h"
#include "raylib.h"

void HudPanel::Render(const GameState& state, const AppConfig& config) const {
    (void)state;
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
    DrawText("SCORE 0000", contentX + 8, cursorY + 9, 20, RAYWHITE);
    cursorY += 44;

    // 3) 4 lives icons
    const int livesY = cursorY;
    const int iconGap = 6;
    const int iconSize = (contentWidth - (iconGap * 3)) / 4;
    for (int i = 0; i < 4; ++i) {
        const int iconX = contentX + (i * (iconSize + iconGap));
        DrawPlayerFigure(
            Vector2{
                static_cast<float>(iconX + (iconSize / 2)),
                static_cast<float>(livesY + (iconSize / 2)),
            },
            static_cast<float>(iconSize),
            0.0F,
            RAYWHITE);
    }
    cursorY += iconSize + 12;

    // 4) Orange bar
    DrawRectangle(contentX, cursorY, contentWidth, 12, ORANGE);
    DrawRectangleLines(contentX, cursorY, contentWidth, 12, RAYWHITE);
    cursorY += 20;

    // 5) Green quadrant and compass
    const int blockGap = 8;
    const int leftBlockSize = (contentWidth - blockGap) / 2;
    DrawRectangle(contentX, cursorY, leftBlockSize, leftBlockSize, BLACK);
    DrawRectangleLines(contentX, cursorY, leftBlockSize, leftBlockSize, RAYWHITE);
    const int quadrantCell = leftBlockSize / 2;
    DrawRectangle(contentX + 2, cursorY + 2, quadrantCell - 3, quadrantCell - 3, DARKGREEN);
    DrawRectangle(contentX + quadrantCell + 1, cursorY + 2, quadrantCell - 3, quadrantCell - 3, GREEN);
    DrawRectangle(contentX + 2, cursorY + quadrantCell + 1, quadrantCell - 3, quadrantCell - 3, GREEN);
    DrawRectangle(contentX + quadrantCell + 1, cursorY + quadrantCell + 1, quadrantCell - 3, quadrantCell - 3, LIME);

    const int compassX = contentX + leftBlockSize + blockGap;
    DrawRectangle(compassX, cursorY, leftBlockSize, leftBlockSize, PURPLE);
    DrawRectangleLines(compassX, cursorY, leftBlockSize, leftBlockSize, RAYWHITE);
    DrawCircle(
        compassX + (leftBlockSize / 2),
        cursorY + (leftBlockSize / 2),
        static_cast<float>(leftBlockSize / 3),
        BLACK);
    DrawLine(
        compassX + (leftBlockSize / 2),
        cursorY + (leftBlockSize / 2),
        compassX + (leftBlockSize / 2) + (leftBlockSize / 4),
        cursorY + (leftBlockSize / 2) - (leftBlockSize / 5),
        RAYWHITE);
    cursorY += leftBlockSize + 10;

    // 6) Square map
    const int mapSize = contentWidth;
    DrawRectangle(contentX, cursorY, mapSize, mapSize, BLACK);
    DrawRectangleLines(contentX, cursorY, mapSize, mapSize, RAYWHITE);
    DrawCircle(contentX + (mapSize / 2), cursorY + (mapSize / 2), 2.0F, PURPLE);
}
