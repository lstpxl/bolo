#include "core/Profiling.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <new>
#include <vector>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__linux__)
#include <malloc.h>
#endif

namespace profiling {
namespace {
using Clock = std::chrono::steady_clock;

constexpr std::uint64_t kPeriodicReportFrames = 120;

std::uint64_t NowUs() {
    const auto now = Clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

struct ScopeStats {
    std::uint64_t totalUs = 0;
    std::uint64_t totalCalls = 0;
    std::uint64_t lastReportTotalUs = 0;
    std::uint64_t lastReportTotalCalls = 0;
    std::uint32_t maxUs = 0;
    std::uint32_t lastUs = 0;
    std::uint32_t lastCalls = 0;
    std::array<std::uint32_t, kProfilingWindowFrames> historyUs{};
    std::size_t historyCount = 0;
    std::size_t historyIndex = 0;
    AllocationDelta allocLastFrame{};
    AllocationDelta allocCurrentFrame{};
};

struct AllocationTracker {
    std::atomic<std::uint64_t> allocCount{0};
    std::atomic<std::uint64_t> freeCount{0};
    std::atomic<std::uint64_t> bytesAllocated{0};
    std::atomic<std::uint64_t> bytesFreed{0};
    std::atomic<std::uint64_t> liveBytes{0};
    std::atomic<std::uint64_t> peakLiveBytes{0};
};

AllocationTracker gAllocationTracker{};

class ProfilerState {
public:
    void BeginFrame() {
        frameAllocStart_ = CaptureAllocationSnapshot();
        for (std::size_t i = 0; i < kScopeCount; ++i) {
            currentFrameUs_[i] = 0;
            currentFrameCalls_[i] = 0;
            scopes_[i].allocCurrentFrame = AllocationDelta{};
        }
    }

    void EndFrame() {
        frameAllocEnd_ = CaptureAllocationSnapshot();
        lastFrameAllocationDelta_ = MakeAllocationDelta(frameAllocStart_, frameAllocEnd_);
        lastFrameAllocationSnapshot_ = frameAllocEnd_;

        for (std::size_t i = 0; i < kScopeCount; ++i) {
            ScopeStats& scope = scopes_[i];
            const std::uint32_t frameUs = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(currentFrameUs_[i], std::numeric_limits<std::uint32_t>::max()));
            scope.lastUs = frameUs;
            scope.lastCalls = currentFrameCalls_[i];
            scope.totalUs += currentFrameUs_[i];
            scope.totalCalls += currentFrameCalls_[i];
            scope.maxUs = std::max(scope.maxUs, frameUs);
            scope.historyUs[scope.historyIndex] = frameUs;
            scope.historyIndex = (scope.historyIndex + 1) % kProfilingWindowFrames;
            if (scope.historyCount < kProfilingWindowFrames) {
                ++scope.historyCount;
            }
            scope.allocLastFrame = scope.allocCurrentFrame;
        }

        ++frameIndex_;
    }

    void AddScopeDuration(Scope scope, std::uint64_t elapsedUs) {
        const std::size_t idx = static_cast<std::size_t>(scope);
        currentFrameUs_[idx] += elapsedUs;
        ++currentFrameCalls_[idx];
    }

    void AddScopeAllocationDelta(Scope scope, const AllocationDelta& delta) {
        const std::size_t idx = static_cast<std::size_t>(scope);
        scopes_[idx].allocCurrentFrame.allocCount += delta.allocCount;
        scopes_[idx].allocCurrentFrame.freeCount += delta.freeCount;
        scopes_[idx].allocCurrentFrame.bytesAllocated += delta.bytesAllocated;
        scopes_[idx].allocCurrentFrame.bytesFreed += delta.bytesFreed;
    }

    ScopeView ViewFor(Scope scope) const {
        const ScopeStats& stats = scopes_[static_cast<std::size_t>(scope)];
        ScopeView out{};
        out.lastMs = static_cast<float>(stats.lastUs) / 1000.0F;
        out.avgMs = AverageMs(stats);
        const std::uint64_t reportUs = stats.totalUs - stats.lastReportTotalUs;
        const std::uint64_t reportCalls = stats.totalCalls - stats.lastReportTotalCalls;
        out.reportTotalMs = static_cast<float>(reportUs) / 1000.0F;
        out.reportCalls = reportCalls;
        out.reportAvgMs = reportCalls > 0
            ? (static_cast<float>(reportUs) / static_cast<float>(reportCalls) / 1000.0F)
            : 0.0F;
        out.maxMs = static_cast<float>(stats.maxUs) / 1000.0F;
        out.p95Ms = P95Ms(stats);
        out.totalCalls = stats.totalCalls;
        out.callsLastFrame = stats.lastCalls;
        out.allocLastFrame = stats.allocLastFrame;
        return out;
    }

    void CommitReportSnapshot() {
        for (std::size_t i = 0; i < kScopeCount; ++i) {
            ScopeStats& scope = scopes_[i];
            scope.lastReportTotalUs = scope.totalUs;
            scope.lastReportTotalCalls = scope.totalCalls;
        }
    }

    ScopeView CombinedView(Scope a, Scope b, Scope c) const {
        const ScopeView va = ViewFor(a);
        const ScopeView vb = ViewFor(b);
        const ScopeView vc = ViewFor(c);
        ScopeView out{};
        out.lastMs = va.lastMs + vb.lastMs + vc.lastMs;
        out.avgMs = va.avgMs + vb.avgMs + vc.avgMs;
        out.maxMs = va.maxMs + vb.maxMs + vc.maxMs;
        out.p95Ms = va.p95Ms + vb.p95Ms + vc.p95Ms;
        out.totalCalls = va.totalCalls + vb.totalCalls + vc.totalCalls;
        out.callsLastFrame = va.callsLastFrame + vb.callsLastFrame + vc.callsLastFrame;
        out.allocLastFrame.allocCount = va.allocLastFrame.allocCount + vb.allocLastFrame.allocCount + vc.allocLastFrame.allocCount;
        out.allocLastFrame.freeCount = va.allocLastFrame.freeCount + vb.allocLastFrame.freeCount + vc.allocLastFrame.freeCount;
        out.allocLastFrame.bytesAllocated =
            va.allocLastFrame.bytesAllocated + vb.allocLastFrame.bytesAllocated + vc.allocLastFrame.bytesAllocated;
        out.allocLastFrame.bytesFreed =
            va.allocLastFrame.bytesFreed + vb.allocLastFrame.bytesFreed + vc.allocLastFrame.bytesFreed;
        return out;
    }

    AllocationSnapshot LastFrameAllocationSnapshot() const {
        return lastFrameAllocationSnapshot_;
    }

    AllocationDelta LastFrameAllocationDelta() const {
        return lastFrameAllocationDelta_;
    }

    std::uint64_t FrameIndex() const {
        return frameIndex_;
    }

private:
    static float AverageMs(const ScopeStats& stats) {
        if (stats.historyCount == 0) {
            return 0.0F;
        }
        std::uint64_t sumUs = 0;
        for (std::size_t i = 0; i < stats.historyCount; ++i) {
            sumUs += stats.historyUs[i];
        }
        return static_cast<float>(sumUs) / static_cast<float>(stats.historyCount) / 1000.0F;
    }

    static float P95Ms(const ScopeStats& stats) {
        if (stats.historyCount == 0) {
            return 0.0F;
        }
        std::vector<std::uint32_t> samples{};
        samples.reserve(stats.historyCount);
        for (std::size_t i = 0; i < stats.historyCount; ++i) {
            samples.push_back(stats.historyUs[i]);
        }
        const std::size_t p95Index =
            static_cast<std::size_t>(static_cast<double>(samples.size() - 1) * 0.95);
        std::nth_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(p95Index), samples.end());
        return static_cast<float>(samples[p95Index]) / 1000.0F;
    }

    std::array<ScopeStats, kScopeCount> scopes_{};
    std::array<std::uint64_t, kScopeCount> currentFrameUs_{};
    std::array<std::uint32_t, kScopeCount> currentFrameCalls_{};
    AllocationSnapshot frameAllocStart_{};
    AllocationSnapshot frameAllocEnd_{};
    AllocationSnapshot lastFrameAllocationSnapshot_{};
    AllocationDelta lastFrameAllocationDelta_{};
    std::uint64_t frameIndex_ = 0;
};

ProfilerState& State() {
    static ProfilerState state{};
    return state;
}

const std::array<const char*, kScopeCount> kScopeNames = {
    "frame.total",
    "frame.fixed_step",
    "game.update",
    "system.ai_update",
    "system.player_update",
    "system.projectile_update",
    "system.physics_collision",
    "system.spawner_update",
    "system.maze_update",
    "enemy.update",
    "enemy.type.drone",
    "enemy.type.torpedo",
    "enemy.type.hunter",
    "enemy.type.assassin",
    "enemy.ai.perception",
    "enemy.ai.decision",
    "enemy.ai.movement",
    "enemy.ai.firing",
    "enemy.pathfinding.total",
    "enemy.pathfinding.far_target",
    "enemy.pathfinding.occupancy",
    "enemy.pathfinding.search",
    "enemy.pathfinding.postprocess",
    "enemy.physics.frontal_collisions",
    "enemy.physics.separation",
    "enemy.physics.frontal_grid_build",
    "enemy.physics.frontal_pair_traverse",
    "enemy.physics.frontal_pair_narrow",
    "enemy.physics.separation_grid_build",
    "enemy.physics.separation_pair_traverse",
    "enemy.physics.separation_pair_resolve",
    "enemy.torpedo.select_heading",
    "enemy.cheap.segment_build",
    "frame.input_poll",
    "frame.render",
    "frame.render.world",
    "frame.render.world.maze",
    "frame.render.world.enemies",
    "frame.render.world.effects",
    "frame.render.overlay",
    "frame.present",
};

}  // namespace

Profiler& Profiler::Instance() {
    static Profiler profiler{};
    return profiler;
}

void Profiler::BeginFrame() {
    State().BeginFrame();
}

void Profiler::EndFrame() {
    State().EndFrame();
}

void Profiler::AddScopeDuration(Scope scope, std::uint64_t elapsedUs) {
    State().AddScopeDuration(scope, elapsedUs);
}

void Profiler::AddScopeAllocationDelta(Scope scope, const AllocationDelta& delta) {
    State().AddScopeAllocationDelta(scope, delta);
}

ScopeView Profiler::GetScopeView(Scope scope) const {
    return State().ViewFor(scope);
}

ScopeView Profiler::GetCombinedScopeView(Scope a, Scope b, Scope c) const {
    return State().CombinedView(a, b, c);
}

AllocationSnapshot Profiler::LastFrameAllocationSnapshot() const {
    return State().LastFrameAllocationSnapshot();
}

std::uint64_t Profiler::FrameIndex() const {
    return State().FrameIndex();
}

bool Profiler::ShouldEmitPeriodicReport() const {
    const std::uint64_t frame = FrameIndex();
    return frame > 0 && (frame % kPeriodicReportFrames) == 0;
}

void Profiler::EmitPeriodicReport(float fixedStepSeconds) const {
    struct ReportRow {
        Scope scope = Scope::FrameTotal;
        ScopeView view{};
    };
    std::array<ReportRow, kScopeCount> rows{};
    for (std::size_t i = 0; i < kScopeCount; ++i) {
        const Scope scope = static_cast<Scope>(i);
        rows[i] = ReportRow{.scope = scope, .view = GetScopeView(scope)};
    }
    std::sort(rows.begin(), rows.end(), [](const ReportRow& a, const ReportRow& b) {
        if (a.view.reportAvgMs != b.view.reportAvgMs) {
            return a.view.reportAvgMs > b.view.reportAvgMs;
        }
        return a.view.avgMs > b.view.avgMs;
    });

    const AllocationDelta frameAllocDelta = State().LastFrameAllocationDelta();
    const AllocationSnapshot allocSnapshot = LastFrameAllocationSnapshot();
    const float fixedStepBudgetMs = fixedStepSeconds * 1000.0F;

    std::printf(
        "\n[PROFILE] frame=%llu budget=%.3fms alloc(+%llu/-%llu, +%llub/-%llub, live=%llub peak=%llub)\n",
        static_cast<unsigned long long>(FrameIndex()),
        fixedStepBudgetMs,
        static_cast<unsigned long long>(frameAllocDelta.allocCount),
        static_cast<unsigned long long>(frameAllocDelta.freeCount),
        static_cast<unsigned long long>(frameAllocDelta.bytesAllocated),
        static_cast<unsigned long long>(frameAllocDelta.bytesFreed),
        static_cast<unsigned long long>(allocSnapshot.liveBytes),
        static_cast<unsigned long long>(allocSnapshot.peakLiveBytes));

    constexpr std::size_t kRowsToPrint = 24;
    std::size_t printedRows = 0;
    for (std::size_t i = 0; i < rows.size() && printedRows < kRowsToPrint; ++i) {
        if (rows[i].view.totalCalls == 0 && rows[i].view.avgMs <= 0.0001F) {
            continue;
        }
        const float budgetPct = fixedStepBudgetMs > 0.001F ? (rows[i].view.avgMs / fixedStepBudgetMs) * 100.0F : 0.0F;
        const float reportBudgetPct =
            fixedStepBudgetMs > 0.001F ? (rows[i].view.reportAvgMs / fixedStepBudgetMs) * 100.0F : 0.0F;
        std::printf(
            "  %-32s win=%7.3fms avg=%7.3fms p95=%7.3fms max=%7.3fms calls(last=%3u,win=%4llu,total=%llu) (win %.1f%% | avg %.1f%% budget)\n",
            ScopeName(rows[i].scope),
            rows[i].view.reportAvgMs,
            rows[i].view.avgMs,
            rows[i].view.p95Ms,
            rows[i].view.maxMs,
            rows[i].view.callsLastFrame,
            static_cast<unsigned long long>(rows[i].view.reportCalls),
            static_cast<unsigned long long>(rows[i].view.totalCalls),
            reportBudgetPct,
            budgetPct);
        ++printedRows;
    }
    State().CommitReportSnapshot();
    std::fflush(stdout);
}

const char* ScopeName(Scope scope) {
    return kScopeNames[static_cast<std::size_t>(scope)];
}

AllocationSnapshot CaptureAllocationSnapshot() {
    AllocationSnapshot out{};
    out.allocCount = gAllocationTracker.allocCount.load(std::memory_order_relaxed);
    out.freeCount = gAllocationTracker.freeCount.load(std::memory_order_relaxed);
    out.bytesAllocated = gAllocationTracker.bytesAllocated.load(std::memory_order_relaxed);
    out.bytesFreed = gAllocationTracker.bytesFreed.load(std::memory_order_relaxed);
    out.liveBytes = gAllocationTracker.liveBytes.load(std::memory_order_relaxed);
    out.peakLiveBytes = gAllocationTracker.peakLiveBytes.load(std::memory_order_relaxed);
    return out;
}

AllocationDelta MakeAllocationDelta(const AllocationSnapshot& start, const AllocationSnapshot& end) {
    AllocationDelta delta{};
    delta.allocCount = end.allocCount >= start.allocCount ? end.allocCount - start.allocCount : 0;
    delta.freeCount = end.freeCount >= start.freeCount ? end.freeCount - start.freeCount : 0;
    delta.bytesAllocated = end.bytesAllocated >= start.bytesAllocated ? end.bytesAllocated - start.bytesAllocated : 0;
    delta.bytesFreed = end.bytesFreed >= start.bytesFreed ? end.bytesFreed - start.bytesFreed : 0;
    return delta;
}

void RecordAllocation(std::size_t bytes) {
    gAllocationTracker.allocCount.fetch_add(1, std::memory_order_relaxed);
    gAllocationTracker.bytesAllocated.fetch_add(static_cast<std::uint64_t>(bytes), std::memory_order_relaxed);
    const std::uint64_t liveAfter = gAllocationTracker.liveBytes.fetch_add(
        static_cast<std::uint64_t>(bytes),
        std::memory_order_relaxed) + static_cast<std::uint64_t>(bytes);

    std::uint64_t currentPeak = gAllocationTracker.peakLiveBytes.load(std::memory_order_relaxed);
    while (liveAfter > currentPeak &&
           !gAllocationTracker.peakLiveBytes.compare_exchange_weak(
               currentPeak,
               liveAfter,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

void RecordFree(std::size_t bytes) {
    gAllocationTracker.freeCount.fetch_add(1, std::memory_order_relaxed);
    gAllocationTracker.bytesFreed.fetch_add(static_cast<std::uint64_t>(bytes), std::memory_order_relaxed);
    const std::uint64_t freed = static_cast<std::uint64_t>(bytes);
    std::uint64_t currentLive = gAllocationTracker.liveBytes.load(std::memory_order_relaxed);
    while (true) {
        const std::uint64_t nextLive = (freed > currentLive) ? 0ULL : (currentLive - freed);
        if (gAllocationTracker.liveBytes.compare_exchange_weak(
                currentLive,
                nextLive,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            break;
        }
    }
}

ScopedProfile::ScopedProfile(Scope scope, bool trackAllocations)
    : scope_(scope),
      startUs_(NowUs()),
      trackAllocations_(trackAllocations) {
    if (trackAllocations_) {
        startAlloc_ = CaptureAllocationSnapshot();
    }
}

ScopedProfile::~ScopedProfile() {
    const std::uint64_t endUs = NowUs();
    Profiler::Instance().AddScopeDuration(scope_, endUs - startUs_);
    if (trackAllocations_) {
        const AllocationSnapshot endAlloc = CaptureAllocationSnapshot();
        Profiler::Instance().AddScopeAllocationDelta(scope_, MakeAllocationDelta(startAlloc_, endAlloc));
    }
}

}  // namespace profiling

namespace {
std::size_t AlignSizeForAllocation(std::size_t size, std::size_t alignment) {
    const std::size_t rem = size % alignment;
    if (rem == 0U) {
        return size;
    }
    return size + (alignment - rem);
}

std::size_t AllocationSizeForPointer(void* ptr) {
    if (ptr == nullptr) {
        return 0U;
    }
#if defined(__APPLE__)
    return malloc_size(ptr);
#elif defined(__linux__)
    return malloc_usable_size(ptr);
#else
    return 0U;
#endif
}

void FreeTrackedPointer(void* ptr) {
    if (ptr == nullptr) {
        return;
    }
    profiling::RecordFree(AllocationSizeForPointer(ptr));
    std::free(ptr);
}
}  // namespace

void* operator new(std::size_t size) {
    if (size == 0U) {
        size = 1U;
    }
    void* ptr = std::malloc(size);
    if (ptr == nullptr) {
        throw std::bad_alloc{};
    }
    std::size_t trackedBytes = AllocationSizeForPointer(ptr);
    if (trackedBytes == 0U) {
        trackedBytes = size;
    }
    profiling::RecordAllocation(trackedBytes);
    return ptr;
}

void operator delete(void* ptr) noexcept {
    FreeTrackedPointer(ptr);
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete[](void* ptr) noexcept {
    ::operator delete(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    ::operator delete(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    ::operator delete[](ptr);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    ::operator delete(ptr);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return ::operator new[](size);
    } catch (...) {
        return nullptr;
    }
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    ::operator delete[](ptr);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    if (size == 0U) {
        size = 1U;
    }
    const std::size_t align = static_cast<std::size_t>(alignment);
    const std::size_t alignedSize = AlignSizeForAllocation(size, align);
    void* ptr = std::aligned_alloc(align, alignedSize);
    if (ptr == nullptr) {
        throw std::bad_alloc{};
    }
    std::size_t trackedBytes = AllocationSizeForPointer(ptr);
    if (trackedBytes == 0U) {
        trackedBytes = alignedSize;
    }
    profiling::RecordAllocation(trackedBytes);
    return ptr;
}

void operator delete(void* ptr, std::align_val_t) noexcept {
    FreeTrackedPointer(ptr);
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}

void operator delete[](void* ptr, std::align_val_t alignment) noexcept {
    ::operator delete(ptr, alignment);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
    FreeTrackedPointer(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
    FreeTrackedPointer(ptr);
}
