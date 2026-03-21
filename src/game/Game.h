#pragma once

#include "app/AppConfig.h"
#include "core/Random.h"
#include "core/Types.h"
#include "game/GameplayView.h"
#include "game/GameModeController.h"
#include "game/RuntimeContext.h"
#include "game/navigation/FlowRebuildWorker.h"
#include "platform/IRenderer.h"
#include "platform/Input.h"

class Game {
public:
    Game();

    GameMode Mode() const;
    const GameState& State() const;
    GameState& MutableState();

    MenuSettings CurrentMenuSettings() const;
    void SetMenuSettings(const MenuSettings& settings);

    void RequestMenu();
    void StartGame(const AppConfig& config, const GameplayView& view);
    void Update(const FrameInput& input, float deltaSeconds, const GameplayView& view);
    void Render(IRenderer& renderer, const AppConfig& config, const FrameInput& input) const;

private:
    GameModeController modeController_{};
    GameState state_{};
    RuntimeContext runtimeContext_{};
    Random random_{0};
    game::navigation::FlowRebuildWorker flowWorker_{};
};
