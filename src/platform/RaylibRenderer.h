#pragma once

#include "platform/IRenderer.h"
#include "platform/Renderer2D.h"
#include "ui/HudPanel.h"

class RaylibRenderer final : public IRenderer {
public:
    void RenderGameplay(const GameState& state, const AppConfig& config) override;

private:
    Renderer2D renderer2D_{};
    HudPanel hudPanel_{};
};
