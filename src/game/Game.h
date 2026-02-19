#pragma once

#include "app/AppConfig.h"
#include "core/Types.h"
#include "game/GameModeController.h"
#include "game/GameState.h"
#include "platform/IRenderer.h"
#include "platform/Input.h"

class Game {
public:
    GameMode Mode() const;
    const GameState& State() const;

    MenuSettings CurrentMenuSettings() const;
    void SetMenuSettings(const MenuSettings& settings);

    void RequestMenu();
    void StartGame(const AppConfig& config);
    void Update(const FrameInput& input, float deltaSeconds, const AppConfig& config);
    void Render(IRenderer& renderer, const AppConfig& config) const;

private:
    GameModeController modeController_{};
    GameState state_{};
};
