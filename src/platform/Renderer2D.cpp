#include "platform/Renderer2D.h"

#include "platform/PlayerFigure.h"
#include "raylib.h"

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
    camera.target = Vector2{state.world.player.position.x, state.world.player.position.y};
    camera.offset = Vector2{worldViewport.width * 0.5F, worldViewport.height * 0.5F};
    camera.rotation = 0.0F;
    camera.zoom = 1.0F;

    BeginMode2D(camera);
    DrawRectangleLines(-500, -500, 1000, 1000, DARKGRAY);
    DrawPlayerFigure(
        Vector2{state.world.player.position.x, state.world.player.position.y},
        36.0F,
        state.world.player.headingRadians,
        GREEN);
    DrawCircleV(Vector2{state.world.enemyBase.position.x, state.world.enemyBase.position.y}, 16.0F, RED);
    EndMode2D();
}
