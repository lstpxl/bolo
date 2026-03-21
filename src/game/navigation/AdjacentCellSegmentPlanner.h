#pragma once

#include <array>
#include "core/Types.h"
#include "game/model/WorldState.h"
#include "game/navigation/CellCoordCache.h"
#include "game/navigation/MazeCellCoord.h"

namespace game::navigation::AdjacentCellSegmentPlanner {

/// Plans 1 or 2 segment endpoints from `fromCell` to `targetCell` when `targetCell` is one of the 8 adjacent cells.
/// See `docs/GAME_DESIGN.md` — **AdjacentCellSegmentPlanner**.
bool Build(
    const WorldState& world,
    const CellCoordCache& cellCache,
    const MazeCellCoord& fromCell,
    const MazeCellCoord& targetCell,
    const Vec2f& startPosition,
    std::array<Vec2f, 2>& outPoints,
    int& outCount);

}  // namespace game::navigation::AdjacentCellSegmentPlanner
