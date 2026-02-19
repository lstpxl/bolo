#pragma once

struct AppConfig;
struct GameState;

struct IRenderer {
    virtual ~IRenderer() = default;
    virtual void RenderGameplay(const GameState& state, const AppConfig& config) = 0;
};
