#include "app/DebugOverlayRenderer.h"

#include <algorithm>
#include <cstdio>
#include "core/Profiling.h"
#include "game/systems/EnemySystem.h"
#include "raylib.h"

namespace {
constexpr int kOverlayTextLeftPx = 8;
constexpr int kOverlayAxesTopPx = 8;
constexpr int kOverlayPerfTopPx = 20;
constexpr int kOverlayProfileTopPx = 32;
constexpr int kOverlayEnemyStatsTopPx = 44;
}

void DebugOverlayRenderer::ReleaseResources() {
    if (!renderTargetLoaded_) {
        return;
    }

    UnloadRenderTexture(renderTarget_);
    renderTarget_ = RenderTexture2D{};
    renderTargetLoaded_ = false;
    renderTargetWidth_ = 0;
    renderTargetHeight_ = 0;
    overlayCacheInitialized_ = false;
}

void DebugOverlayRenderer::EnsureRenderTarget(int width, int height) const {
    if (width <= 0 || height <= 0) {
        return;
    }

    const bool sizeChanged = renderTargetLoaded_ && (renderTargetWidth_ != width || renderTargetHeight_ != height);
    if (sizeChanged) {
        UnloadRenderTexture(renderTarget_);
        renderTarget_ = RenderTexture2D{};
        renderTargetLoaded_ = false;
        renderTargetWidth_ = 0;
        renderTargetHeight_ = 0;
        overlayCacheInitialized_ = false;
    }

    if (renderTargetLoaded_) {
        return;
    }

    renderTarget_ = LoadRenderTexture(width, height);
    renderTargetLoaded_ = renderTarget_.id != 0;
    if (!renderTargetLoaded_) {
        return;
    }

    SetTextureFilter(renderTarget_.texture, TEXTURE_FILTER_POINT);
    renderTargetWidth_ = width;
    renderTargetHeight_ = height;
    overlayCacheInitialized_ = false;
}

void DebugOverlayRenderer::RebuildRenderTarget() const {
    if (!renderTargetLoaded_) {
        return;
    }

    BeginTextureMode(renderTarget_);
    ClearBackground(BLANK);
    DrawText(axesTextCache_, kOverlayTextLeftPx, kOverlayAxesTopPx, 10, RAYWHITE);
    DrawText(perfTextCache_, kOverlayTextLeftPx, kOverlayPerfTopPx, 10, GREEN);
    DrawText(profileTextCache_, kOverlayTextLeftPx, kOverlayProfileTopPx, 10, LIGHTGRAY);
    DrawText(enemyStatsTextCache_, kOverlayTextLeftPx, kOverlayEnemyStatsTopPx, 10, LIGHTGRAY);
    EndTextureMode();
}

void DebugOverlayRenderer::Draw(const GameState& state, const AppConfig& config, const FrameInput& input) const {
    (void)state;
    EnsureRenderTarget(config.screenWidth, config.screenHeight);

    const std::uint64_t frameIndex = profiling::Profiler::Instance().FrameIndex();
    const bool shouldRefreshCache =
        !overlayCacheInitialized_ ||
        frameIndex >= (lastOverlayUpdateFrame_ + kOverlayUpdateEveryFrames);
    if (shouldRefreshCache) {
        std::snprintf(
            axesTextCache_,
            sizeof(axesTextCache_),
            "Axes:  0:%6d  1:%6d  2:%6d  3:%6d",
            input.gamepadAxis0Raw,
            input.gamepadAxis1Raw,
            input.gamepadAxis2Raw,
            input.gamepadAxis3Raw);

        const profiling::ScopeView frameView =
            profiling::Profiler::Instance().GetScopeView(profiling::Scope::FrameTotal);
        const profiling::ScopeView fixedStepView =
            profiling::Profiler::Instance().GetScopeView(profiling::Scope::FixedStepUpdate);
        const int fps = GetFPS();
        const float overheadMs = std::max(0.0F, frameView.avgMs - fixedStepView.avgMs);
        std::snprintf(
            perfTextCache_,
            sizeof(perfTextCache_),
            "PERF FPS %3d FT %.2fms FS %.2fms OH %.2fms",
            fps,
            frameView.avgMs,
            fixedStepView.avgMs,
            overheadMs);

        const profiling::ScopeView aiView = profiling::Profiler::Instance().GetScopeView(profiling::Scope::AiUpdate);
        const profiling::ScopeView pathView =
            profiling::Profiler::Instance().GetScopeView(profiling::Scope::PathfindingTotal);
        const profiling::ScopeView physicsView = profiling::Profiler::Instance().GetCombinedScopeView(
            profiling::Scope::PhysicsCollisionUpdate,
            profiling::Scope::EnemyFrontalCollisions,
            profiling::Scope::EnemySeparation);
        const profiling::AllocationSnapshot allocationView =
            profiling::Profiler::Instance().LastFrameAllocationSnapshot();
        std::snprintf(
            profileTextCache_,
            sizeof(profileTextCache_),
            "AI %.2f PF %.2f PH %.2f | A +%llu/-%llu B +%lluk/-%lluk L %lluk P %lluk",
            aiView.avgMs,
            pathView.avgMs,
            physicsView.avgMs,
            static_cast<unsigned long long>(
                aiView.allocLastFrame.allocCount +
                pathView.allocLastFrame.allocCount +
                physicsView.allocLastFrame.allocCount),
            static_cast<unsigned long long>(
                aiView.allocLastFrame.freeCount +
                pathView.allocLastFrame.freeCount +
                physicsView.allocLastFrame.freeCount),
            static_cast<unsigned long long>(
                (aiView.allocLastFrame.bytesAllocated +
                    pathView.allocLastFrame.bytesAllocated +
                    physicsView.allocLastFrame.bytesAllocated) /
                1024ULL),
            static_cast<unsigned long long>(
                (aiView.allocLastFrame.bytesFreed +
                    pathView.allocLastFrame.bytesFreed +
                    physicsView.allocLastFrame.bytesFreed) /
                1024ULL),
            static_cast<unsigned long long>(allocationView.liveBytes / 1024ULL),
            static_cast<unsigned long long>(allocationView.peakLiveBytes / 1024ULL));

        const EnemyRuntimeStats& enemyStats = GetEnemyRuntimeStats();
        std::snprintf(
            enemyStatsTextCache_,
            sizeof(enemyStatsTextCache_),
            "A:%d V:%d F:%d C:%d FB:%d | Pairs F:%d/%d S:%d/%d",
            enemyStats.aliveCount,
            enemyStats.visibleInViewportCount,
            enemyStats.fullTierCount,
            enemyStats.cheapTierCount,
            enemyStats.fullTierInBaseClearanceCount,
            enemyStats.frontalPairsDistanceChecks,
            enemyStats.frontalPairsVisited,
            enemyStats.separationPairsResolved,
            enemyStats.separationPairsVisited);

        lastOverlayUpdateFrame_ = frameIndex;
        overlayCacheInitialized_ = true;
        RebuildRenderTarget();
    }

    if (renderTargetLoaded_) {
        const Rectangle source{
            .x = 0.0F,
            .y = 0.0F,
            .width = static_cast<float>(renderTargetWidth_),
            .height = -static_cast<float>(renderTargetHeight_),
        };
        const Rectangle destination{
            .x = 0.0F,
            .y = 0.0F,
            .width = static_cast<float>(renderTargetWidth_),
            .height = static_cast<float>(renderTargetHeight_),
        };
        DrawTexturePro(renderTarget_.texture, source, destination, Vector2{0.0F, 0.0F}, 0.0F, WHITE);
        return;
    }

    DrawText(axesTextCache_, kOverlayTextLeftPx, kOverlayAxesTopPx, 10, RAYWHITE);
    DrawText(perfTextCache_, kOverlayTextLeftPx, kOverlayPerfTopPx, 10, GREEN);
    DrawText(profileTextCache_, kOverlayTextLeftPx, kOverlayProfileTopPx, 10, LIGHTGRAY);
    DrawText(enemyStatsTextCache_, kOverlayTextLeftPx, kOverlayEnemyStatsTopPx, 10, LIGHTGRAY);
}
