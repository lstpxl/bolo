#pragma once

#include <cstdint>
#include <vector>
#include "core/Types.h"
#include "game/model/EntityTypes.h"
#include "game/navigation/CellCoordCache.h"

struct NavigationRuntimeCache;
struct WorldState;

namespace game::navigation {

struct FlowRebuildWorker;

struct FlowFieldUpdateStats {
    bool playerCellChanged = false;
    bool rebuildScheduled = false;
};

class PlayerFlowField {
public:
    void EnsureCapacity(const MazeState& maze);
    void Rebuild(const MazeState& maze, const CellCoordCache& cellCache,
        const std::vector<EnemyBase>& bases);
    void OverrideNextCellHash(int fromCellHash, int toCellHash);
    void Invalidate();

    void SetCacheActive(bool active) { cacheActive_ = active; }
    void OnPlayerCellChanged(int newCellX, int newCellY,
        FlowRebuildWorker& flowWorker,
        ::NavigationRuntimeCache& navigationCache,
        const ::WorldState& world,
        FlowFieldUpdateStats* outStats = nullptr);
    void Update(FlowRebuildWorker& flowWorker,
        ::NavigationRuntimeCache& navigationCache,
        const ::WorldState& world,
        int currentCellX, int currentCellY,
        FlowFieldUpdateStats* outStats = nullptr);

    bool HasBuild() const { return hasBuild_; }
    bool IsBuiltFor(std::uint32_t playerCellVersion) const;
    int NextCellHash(int fromCellHash) const;
    Vec2f NextCellCenter(int fromCellHash, const CellCoordCache& cellCache) const;

private:
    bool CanTraverse(const MazeState& maze, int fromX, int fromY, int toX, int toY) const;

    int widthCells_ = 0;
    int heightCells_ = 0;
    std::vector<int> nextCellHash_{};
    std::vector<int> distance_{};
    std::uint32_t builtForPlayerCellVersion_ = 0U;
    int builtForCellX_ = -1;
    int builtForCellY_ = -1;
    bool hasBuild_ = false;

    int lastCellX_ = -1;
    int lastCellY_ = -1;
    bool cacheActive_ = false;
    int age_ = 0;
};

}  // namespace game::navigation
