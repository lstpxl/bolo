#pragma once

#include <cstdint>
#include "app/AppConfig.h"
#include "platform/Input.h"

struct GameState;

class DebugOverlayRenderer {
public:
    void ReleaseResources();
    void Draw(const GameState& state, const AppConfig& config, const FrameInput& input) const;

private:
    static constexpr std::uint64_t kOverlayUpdateEveryFrames = 4;
    mutable std::uint64_t lastOverlayUpdateFrame_ = 0;
    mutable bool overlayCacheInitialized_ = false;
    mutable char axesTextCache_[96]{};
    mutable char perfTextCache_[128]{};
    mutable char profileTextCache_[220]{};
    mutable char enemyStatsTextCache_[220]{};
};
