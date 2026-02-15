#include "ui/HudPanel.h"

#include "raylib.h"

void HudPanel::Render(const GameState& state, const AppConfig& config) const {
    const Rectangle panel = {
        .x = static_cast<float>(config.screenWidth - config.hudWidth),
        .y = 0.0F,
        .width = static_cast<float>(config.hudWidth),
        .height = static_cast<float>(config.screenHeight),
    };

    DrawRectangleRec(panel, Color{27, 31, 39, 255});
    DrawLine(
        static_cast<int>(panel.x),
        0,
        static_cast<int>(panel.x),
        config.screenHeight,
        Color{58, 66, 80, 255});

    DrawText("HUD", static_cast<int>(panel.x) + 16, 16, 30, RAYWHITE);
    DrawText(
        TextFormat("Difficulty: %d", static_cast<int>(state.menuSettings.difficulty)),
        static_cast<int>(panel.x) + 16,
        58,
        20,
        LIGHTGRAY);
    DrawText(
        TextFormat("Maze Density: %d%%", state.menuSettings.mazeDensityPercent),
        static_cast<int>(panel.x) + 16,
        86,
        20,
        LIGHTGRAY);
    DrawText(
        TextFormat("Player Alive: %s", state.world.player.alive ? "yes" : "no"),
        static_cast<int>(panel.x) + 16,
        114,
        20,
        LIGHTGRAY);
    DrawText(
        TextFormat("Enemies: %d", state.world.enemyBase.activeEnemies),
        static_cast<int>(panel.x) + 16,
        142,
        20,
        LIGHTGRAY);
}
