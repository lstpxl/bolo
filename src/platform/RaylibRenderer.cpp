#include "platform/RaylibRenderer.h"

void RaylibRenderer::RenderGameplay(const GameState& state, const AppConfig& config) {
    renderer2D_.DrawWorld(state, config);
    hudPanel_.Render(state, config);
}
