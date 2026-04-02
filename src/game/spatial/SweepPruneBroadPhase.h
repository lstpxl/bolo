#pragma once

#include <functional>
#include <vector>
#include "core/Types.h"

namespace game::spatial {

class SweepPruneBroadPhase {
public:
    struct FrameStats {
        std::uint64_t updateCalls = 0;
        std::uint64_t activeItems = 0;
        std::uint64_t candidatePairs = 0;
        std::uint64_t xLocalRepairs = 0;
        std::uint64_t yLocalRepairs = 0;
    };

    struct Item {
        int id = -1;
        float sortField = 0.0F;
    };

    void BeginFrame(int maxId);
    void UpdateEntity(int id, const Vec2f& from, const Vec2f& to, float radius, bool active);

    /// Invoke `fn(a, b)` once for every unique overlapping pair of active entities.
    ///
    /// **Caller contract — scratch buffer sizing:**
    ///   `pairVisitedScratch` MUST be pre-sized to at least `kMaxAliveEnemies * kMaxAliveEnemies`
    ///   elements before gameplay begins (e.g. in InitializeMazeWorld). Growing the buffer during
    ///   fixed-step updates is an O(n²) allocation and will trigger a warning + assert.
    ///
    /// **Caller contract — epoch reuse:**
    ///   `pairVisitedEpoch` must be initialised to 1 and passed back unmodified between calls so
    ///   the epoch-based deduplication correctly identifies which entries belong to the current
    ///   traversal. Resetting the scratch buffer (e.g. after a level transition) requires setting
    ///   `pairVisitedEpoch = 1` and filling `pairVisitedScratch` with 0.
    void ForEachCandidatePair(
        std::function<void(int a, int b)> fn,
        std::vector<std::uint32_t>& pairVisitedScratch,
        std::uint32_t& pairVisitedEpoch);

    bool GetNeighborsX(int id, int& previousId, int& nextId) const;
    bool GetNeighborsY(int id, int& previousId, int& nextId) const;
    const FrameStats& GetFrameStats() const { return frameStats_; }

private:
    struct AxisCache {
        std::vector<Item> items{};
        std::vector<int> idToIndex{};

        void EnsureCapacity(int maxId);
        int UpdateSortField(int id, float newValue);
        bool GetNeighbors(int id, int& previousId, int& nextId) const;
    };

    static bool Overlaps(float minA, float maxA, float minB, float maxB);

    int maxId_ = 0;
    std::vector<float> minX_{};
    std::vector<float> maxX_{};
    std::vector<float> minY_{};
    std::vector<float> maxY_{};
    std::vector<std::uint8_t> active_{};
    AxisCache sortedByMinX_{};
    AxisCache sortedByMinY_{};
    FrameStats frameStats_{};
};

}  // namespace game::spatial
