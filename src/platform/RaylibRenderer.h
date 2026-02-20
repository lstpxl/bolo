#pragma once

#include "platform/IRenderer.h"
#include "platform/Renderer2D.h"
#include "ui/HudPanel.h"

class RaylibRenderer final : public IRenderer {
public:
    bool LoadResources();
    void UnloadResources();
    void RenderGameplay(const GameState& state, const AppConfig& config, const FrameInput& input) override;

private:
    Renderer2D renderer2D_{};
    HudPanel hudPanel_{};
};
