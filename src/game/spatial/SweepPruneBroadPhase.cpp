#include "game/spatial/SweepPruneBroadPhase.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include "core/Log.h"

namespace game::spatial {

void SweepPruneBroadPhase::AxisCache::EnsureCapacity(int maxId) {
    if (maxId <= 0) {
        items.clear();
        idToIndex.clear();
        return;
    }
    if (static_cast<int>(idToIndex.size()) != maxId) {
        // Topology changed (enemy count changed). Reset axis cache to avoid stale
        // duplicate ids from previous frames producing self-pairs.
        items.clear();
        idToIndex.assign(static_cast<std::size_t>(maxId), -1);
    }
}

int SweepPruneBroadPhase::AxisCache::UpdateSortField(int id, float newValue) {
    if (id < 0 || id >= static_cast<int>(idToIndex.size())) {
        return 0;
    }
    int swapCount = 0;

    int i = idToIndex[static_cast<std::size_t>(id)];
    if (i < 0) {
        i = static_cast<int>(items.size());
        items.push_back(Item{.id = id, .sortField = newValue});
        idToIndex[static_cast<std::size_t>(id)] = i;
    } else {
        items[static_cast<std::size_t>(i)].sortField = newValue;
    }

    while (i > 0 &&
           items[static_cast<std::size_t>(i)].sortField <
               items[static_cast<std::size_t>(i - 1)].sortField) {
        std::swap(items[static_cast<std::size_t>(i)], items[static_cast<std::size_t>(i - 1)]);
        idToIndex[static_cast<std::size_t>(items[static_cast<std::size_t>(i)].id)] = i;
        idToIndex[static_cast<std::size_t>(items[static_cast<std::size_t>(i - 1)].id)] = i - 1;
        --i;
        swapCount += 1;
    }

    while (i + 1 < static_cast<int>(items.size()) &&
           items[static_cast<std::size_t>(i)].sortField >
               items[static_cast<std::size_t>(i + 1)].sortField) {
        std::swap(items[static_cast<std::size_t>(i)], items[static_cast<std::size_t>(i + 1)]);
        idToIndex[static_cast<std::size_t>(items[static_cast<std::size_t>(i)].id)] = i;
        idToIndex[static_cast<std::size_t>(items[static_cast<std::size_t>(i + 1)].id)] = i + 1;
        ++i;
        swapCount += 1;
    }
    return swapCount;
}

bool SweepPruneBroadPhase::AxisCache::GetNeighbors(int id, int& previousId, int& nextId) const {
    previousId = -1;
    nextId = -1;
    if (id < 0 || id >= static_cast<int>(idToIndex.size())) {
        return false;
    }
    const int i = idToIndex[static_cast<std::size_t>(id)];
    if (i < 0) {
        return false;
    }
    if (i > 0) {
        previousId = items[static_cast<std::size_t>(i - 1)].id;
    }
    if (i + 1 < static_cast<int>(items.size())) {
        nextId = items[static_cast<std::size_t>(i + 1)].id;
    }
    return true;
}

void SweepPruneBroadPhase::BeginFrame(int maxId) {
    maxId_ = std::max(0, maxId);
    if (maxId_ <= 0) {
        minX_.clear();
        maxX_.clear();
        minY_.clear();
        maxY_.clear();
        active_.clear();
        sortedByMinX_.EnsureCapacity(0);
        sortedByMinY_.EnsureCapacity(0);
        return;
    }

    const std::size_t n = static_cast<std::size_t>(maxId_);
    if (minX_.size() != n) {
        minX_.assign(n, 0.0F);
        maxX_.assign(n, 0.0F);
        minY_.assign(n, 0.0F);
        maxY_.assign(n, 0.0F);
        active_.assign(n, 0U);
    } else {
        std::fill(active_.begin(), active_.end(), 0U);
    }

    sortedByMinX_.EnsureCapacity(maxId_);
    sortedByMinY_.EnsureCapacity(maxId_);
    frameStats_ = FrameStats{};
}

void SweepPruneBroadPhase::UpdateEntity(int id, const Vec2f& from, const Vec2f& to, float radius, bool active) {
    if (id < 0 || id >= maxId_) {
        return;
    }
    const float minX = std::min(from.x, to.x) - radius;
    const float maxX = std::max(from.x, to.x) + radius;
    const float minY = std::min(from.y, to.y) - radius;
    const float maxY = std::max(from.y, to.y) + radius;

    minX_[static_cast<std::size_t>(id)] = minX;
    maxX_[static_cast<std::size_t>(id)] = maxX;
    minY_[static_cast<std::size_t>(id)] = minY;
    maxY_[static_cast<std::size_t>(id)] = maxY;
    active_[static_cast<std::size_t>(id)] = active ? 1U : 0U;
    frameStats_.updateCalls += 1;
    if (active) {
        frameStats_.activeItems += 1;
    }

    const float xSortField = active ? minX : std::numeric_limits<float>::infinity();
    const float ySortField = active ? minY : std::numeric_limits<float>::infinity();
    frameStats_.xLocalRepairs += static_cast<std::uint64_t>(sortedByMinX_.UpdateSortField(id, xSortField));
    frameStats_.yLocalRepairs += static_cast<std::uint64_t>(sortedByMinY_.UpdateSortField(id, ySortField));
}

bool SweepPruneBroadPhase::Overlaps(float minA, float maxA, float minB, float maxB) {
    return !(maxA < minB || maxB < minA);
}

void SweepPruneBroadPhase::ForEachCandidatePair(
    std::function<void(int a, int b)> fn,
    std::vector<std::uint32_t>& pairVisitedScratch,
    std::uint32_t& pairVisitedEpoch) {
    if (maxId_ <= 1) {
        return;
    }
    const std::size_t pairCount = static_cast<std::size_t>(maxId_) * static_cast<std::size_t>(maxId_);
    // Callers must pre-size pairVisitedScratch to at least kMaxAliveEnemies^2 before fixed-step
    // play begins (see InitializeMazeWorld). Any growth here is unexpected and expensive.
    assert(pairVisitedScratch.size() >= pairCount &&
           "pairVisitedScratch under-sized; caller must pre-size to kMaxAliveEnemies^2");
    if (pairVisitedScratch.size() < pairCount) {
        bolt::log::Warning(
            "[SWEEP_PRUNE] pairVisitedScratch too small (%zu < %zu); growing in hot path. "
            "Caller must pre-size to kMaxAliveEnemies^2 before gameplay begins.",
            pairVisitedScratch.size(), pairCount);
        pairVisitedScratch.resize(pairCount, 0U);
    }
    std::uint32_t epoch = pairVisitedEpoch + 1U;
    if (epoch == 0U) {
        std::fill(pairVisitedScratch.begin(), pairVisitedScratch.end(), 0U);
        epoch = 1U;
    }
    pairVisitedEpoch = epoch;
    const auto& items = sortedByMinX_.items;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const int idA = items[static_cast<std::size_t>(i)].id;
        if (idA < 0 || idA >= maxId_ || active_[static_cast<std::size_t>(idA)] == 0U) {
            continue;
        }
        const float aMaxX = maxX_[static_cast<std::size_t>(idA)];
        for (int j = i + 1; j < static_cast<int>(items.size()); ++j) {
            const int idB = items[static_cast<std::size_t>(j)].id;
            if (idB < 0 || idB >= maxId_ || active_[static_cast<std::size_t>(idB)] == 0U) {
                continue;
            }
            if (idA == idB) {
                continue;
            }
            const float bMinX = minX_[static_cast<std::size_t>(idB)];
            if (bMinX > aMaxX) {
                break;
            }
            if (!Overlaps(
                    minY_[static_cast<std::size_t>(idA)],
                    maxY_[static_cast<std::size_t>(idA)],
                    minY_[static_cast<std::size_t>(idB)],
                    maxY_[static_cast<std::size_t>(idB)])) {
                continue;
            }
            const int lo = std::min(idA, idB);
            const int hi = std::max(idA, idB);
            if (lo == hi) {
                continue;
            }
            const std::size_t key = static_cast<std::size_t>(lo * maxId_ + hi);
            if (pairVisitedScratch[key] == epoch) {
                continue;
            }
            pairVisitedScratch[key] = epoch;
            frameStats_.candidatePairs += 1;
            fn(lo, hi);
        }
    }
}

bool SweepPruneBroadPhase::GetNeighborsX(int id, int& previousId, int& nextId) const {
    return sortedByMinX_.GetNeighbors(id, previousId, nextId);
}

bool SweepPruneBroadPhase::GetNeighborsY(int id, int& previousId, int& nextId) const {
    return sortedByMinY_.GetNeighbors(id, previousId, nextId);
}

}  // namespace game::spatial
