#include "platform/RaylibRenderer.h"

#include "core/Profiling.h"

bool RaylibRenderer::LoadResources() {
    return renderer2D_.LoadResources();
}

void RaylibRenderer::UnloadResources() {
    hudPanel_.ReleaseResources();
    renderer2D_.UnloadResources();
}

void RaylibRenderer::RenderGameplay(const GameState& state, const AppConfig& config, const FrameInput& input) {
    renderer2D_.DrawWorld(state, config);
    profiling::ScopedProfile hudScope(profiling::Scope::RenderHud);
    hudPanel_.Render(state, config, input);
}
