#pragma once

struct AppConfig;
struct FrameInput;
struct GameState;

struct IRenderer {
    virtual ~IRenderer() = default;
    virtual void RenderGameplay(const GameState& state, const AppConfig& config, const FrameInput& input) = 0;
};
