#pragma once

#include "platform/IRenderer.h"
#include "platform/Renderer2D.h"
#include "ui/HudPanel.h"

class MenuBackgroundSimulation;

class RaylibRenderer final : public IRenderer {
public:
    bool LoadResources();
    void UnloadResources();
    void ResetTransientState();
    void PrepareGameplayRender(const GameState& state, const AppConfig& config, const FrameInput& input);
    void RenderMenuBackground(const MenuBackgroundSimulation& simulation, const AppConfig& config);
    void RenderGameplay(const GameState& state, const AppConfig& config, const FrameInput& input) override;

private:
    Renderer2D renderer2D_{};
    HudPanel hudPanel_{};
};
