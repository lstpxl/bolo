#pragma once

#include <vector>
#include "game/navigation/BaseDistanceField.h"
#include "game/navigation/CellCoordCache.h"

namespace game::navigation {

class BaseFlowField {
public:
    void EnsureCapacity(const MazeState& maze);
    void Rebuild(
        const MazeState& maze,
        const CellCoordCache& cellCache,
        const BaseDistanceField& baseDistanceField);
    void Invalidate();

    bool HasBuild() const { return hasBuild_; }
    int NextCellHash(int fromCellHash) const;
    Vec2f NextCellCenter(int fromCellHash, const CellCoordCache& cellCache) const;

private:
    bool CanTraverse(const MazeState& maze, int fromX, int fromY, int toX, int toY) const;

    int widthCells_ = 0;
    int heightCells_ = 0;
    std::vector<int> nextCellHash_{};
    bool hasBuild_ = false;
};

}  // namespace game::navigation
