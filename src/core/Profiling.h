#pragma once

#include <cstddef>
#include <cstdint>

namespace profiling {

constexpr std::size_t kProfilingWindowFrames = 120;

enum class Scope : std::uint8_t {
    FrameTotal = 0,
    FixedStepUpdate,
    GameUpdate,
    AiUpdate,
    PlayerUpdate,
    ProjectileUpdate,
    PhysicsCollisionUpdate,
    SpawnerUpdate,
    MazeUpdate,
    EnemyUpdate,
    EnemyTypeDroneUpdate,
    EnemyTypeTorpedoUpdate,
    EnemyTypeHunterUpdate,
    EnemyTypeAssassinUpdate,
    EnemyAiPerception,
    EnemyAiDecision,
    EnemyAiMovement,
    EnemyMovementSeparationProbe,
    EnemyMovementOverlapCheck,
    EnemyMovementOverlapIsBlocked,
    EnemyMovementOverlapSeparationTurn,
    EnemyMovementOverlapTurnValid,
    EnemyMovementWallCheck,
    EnemyAiFiring,
    PathfindingTotal,
    PathfindingFarTarget,
    PathfindingOccupancy,
    PathfindingSearch,
    PathfindingPostprocess,
    FlowFieldRebuild,
    EnemyFrontalCollisions,
    EnemySeparation,
    EnemyFrontalGridBuild,
    EnemyFrontalPairTraverse,
    EnemyFrontalPairNarrowphase,
    EnemySeparationGridBuild,
    EnemySeparationPairTraverse,
    EnemySeparationPairResolve,
    EnemyTorpedoSelectHeading,
    EnemyCheapSegmentBuild,
    FrameInputPoll,
    FrameRender,
    RenderWorld,
    RenderWorldMaze,
    RenderWorldEnemies,
    RenderWorldEnemiesCull,
    RenderWorldEnemiesDraw,
    RenderWorldEffects,
    RenderWorldEffectsProjectiles,
    RenderWorldEffectsExplosion,
    RenderWorldEffectsPlayerFallback,
    RenderHud,
    RenderHudStatic,
    RenderHudText,
    RenderHudLives,
    RenderHudBars,
    RenderHudMinimap,
    RenderHudCompass,
    RenderOverlay,
    FramePresent,
    MenuMusicUpdate,
    Count
};

constexpr std::size_t kScopeCount = static_cast<std::size_t>(Scope::Count);

struct AllocationSnapshot {
    std::uint64_t allocCount = 0;
    std::uint64_t freeCount = 0;
    std::uint64_t bytesAllocated = 0;
    std::uint64_t bytesFreed = 0;
    std::uint64_t liveBytes = 0;
    std::uint64_t peakLiveBytes = 0;
};

struct AllocationDelta {
    std::uint64_t allocCount = 0;
    std::uint64_t freeCount = 0;
    std::uint64_t bytesAllocated = 0;
    std::uint64_t bytesFreed = 0;
};

struct ScopeView {
    float lastMs = 0.0F;
    float avgMs = 0.0F;
    float reportAvgMs = 0.0F;
    float reportTotalMs = 0.0F;
    float maxMs = 0.0F;
    float p95Ms = 0.0F;
    std::uint64_t totalCalls = 0;
    std::uint64_t reportCalls = 0;
    std::uint32_t callsLastFrame = 0;
    AllocationDelta allocLastFrame{};
};

class Profiler {
public:
    static Profiler& Instance();

    void BeginFrame();
    void EndFrame();

    void AddScopeDuration(Scope scope, std::uint64_t elapsedUs);
    void AddScopeAllocationDelta(Scope scope, const AllocationDelta& delta);

    ScopeView GetScopeView(Scope scope) const;
    ScopeView GetCombinedScopeView(Scope a, Scope b, Scope c) const;
    AllocationSnapshot LastFrameAllocationSnapshot() const;
    std::uint64_t FrameIndex() const;
    bool ShouldEmitPeriodicReport() const;
    void EmitPeriodicReport(float fixedStepSeconds) const;

private:
    Profiler() = default;
};

const char* ScopeName(Scope scope);

AllocationSnapshot CaptureAllocationSnapshot();
AllocationDelta MakeAllocationDelta(const AllocationSnapshot& start, const AllocationSnapshot& end);

void RecordAllocation(std::size_t bytes);
void RecordFree(std::size_t bytes);

class ScopedProfile {
public:
    explicit ScopedProfile(Scope scope, bool trackAllocations = false);
    ~ScopedProfile();

    ScopedProfile(const ScopedProfile&) = delete;
    ScopedProfile& operator=(const ScopedProfile&) = delete;

private:
    Scope scope_;
    std::uint64_t startUs_ = 0;
    bool trackAllocations_ = false;
    AllocationSnapshot startAlloc_{};
};

}  // namespace profiling
