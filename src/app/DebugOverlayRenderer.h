#pragma once

#include <cstdint>
#include "app/AppConfig.h"
#include "platform/Input.h"
#include "raylib.h"

struct GameState;

class DebugOverlayRenderer {
public:
    void ReleaseResources();
    void Draw(const GameState& state, const AppConfig& config, const FrameInput& input) const;

private:
    static constexpr std::uint64_t kOverlayUpdateEveryFrames = 4;
    void EnsureRenderTarget(int width, int height) const;
    void RebuildRenderTarget() const;
    mutable std::uint64_t lastOverlayUpdateFrame_ = 0;
    mutable bool overlayCacheInitialized_ = false;
    mutable RenderTexture2D renderTarget_{};
    mutable bool renderTargetLoaded_ = false;
    mutable int renderTargetWidth_ = 0;
    mutable int renderTargetHeight_ = 0;
    mutable char axesTextCache_[96]{};
    mutable char perfTextCache_[128]{};
    mutable char profileTextCache_[220]{};
    mutable char enemyStatsTextCache_[220]{};
};
