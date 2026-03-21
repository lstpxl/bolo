#pragma once

#include <vector>
#include "game/navigation/CellCoordCache.h"

namespace game::navigation {

class BaseDistanceField {
public:
    void EnsureCapacity(const MazeState& maze);
    void Rebuild(const MazeState& maze, const CellCoordCache& cellCache,
        const std::vector<EnemyBase>& bases);
    void Invalidate();

    bool HasBuild() const { return hasBuild_; }
    int DistanceAtCell(int cellX, int cellY) const;
    int DistanceAtHash(int cellHash) const;

private:
    bool CanTraverse(const MazeState& maze, int fromX, int fromY, int toX, int toY) const;

    int widthCells_ = 0;
    int heightCells_ = 0;
    std::vector<int> distance_{};
    bool hasBuild_ = false;
};

}  // namespace game::navigation
