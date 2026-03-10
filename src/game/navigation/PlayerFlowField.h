#pragma once

#include <cstdint>
#include <vector>
#include "core/Types.h"
#include "game/model/EntityTypes.h"
#include "game/navigation/CellCoordCache.h"

namespace game::navigation {

class PlayerFlowField {
public:
    void EnsureCapacity(const MazeState& maze);
    void Rebuild(const MazeState& maze, const CellCoordCache& cellCache,
        const std::vector<EnemyBase>& bases);
    void OverrideNextCellHash(int fromCellHash, int toCellHash);
    void Invalidate();

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
    bool hasBuild_ = false;
};

}  // namespace game::navigation
