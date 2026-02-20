#include "platform/RaylibRenderer.h"

bool RaylibRenderer::LoadResources() {
    return renderer2D_.LoadResources();
}

void RaylibRenderer::UnloadResources() {
    renderer2D_.UnloadResources();
}

void RaylibRenderer::RenderGameplay(const GameState& state, const AppConfig& config) {
    renderer2D_.DrawWorld(state, config);
    hudPanel_.Render(state, config);
}
