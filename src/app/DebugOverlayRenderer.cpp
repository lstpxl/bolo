#include "app/DebugOverlayRenderer.h"

#include <algorithm>
#include <cstdio>
#include "core/Profiling.h"
#include "game/GameQueries.h"
#include "game/systems/EnemySystem.h"
#include "raylib.h"

void DebugOverlayRenderer::Draw(const GameState& state, const AppConfig& config, const FrameInput& input) const {
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

        const int aliveBases = game::queries::CountAliveBases(state);
        const int dronesAlive = game::queries::CountAliveEnemiesByType(state, EnemyType::Drone);
        const int torpedoesAlive = game::queries::CountAliveEnemiesByType(state, EnemyType::Torpedo);
        const int huntersAlive = game::queries::CountAliveEnemiesByType(state, EnemyType::Hunter);
        const int assassinsAlive = game::queries::CountAliveEnemiesByType(state, EnemyType::Assassin);
        std::snprintf(
            countsTextCache_,
            sizeof(countsTextCache_),
            "B:%d D:%d T:%d H:%d A:%d",
            aliveBases,
            dronesAlive,
            torpedoesAlive,
            huntersAlive,
            assassinsAlive);

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
    }

    DrawText(axesTextCache_, 8, 8, 10, RAYWHITE);
    DrawText(perfTextCache_, 8, 20, 10, GREEN);
    DrawText(profileTextCache_, 8, 32, 10, LIGHTGRAY);
    DrawText(enemyStatsTextCache_, 8, 44, 10, LIGHTGRAY);
    DrawText(countsTextCache_, 8, config.screenHeight - 18, 10, RAYWHITE);
}
