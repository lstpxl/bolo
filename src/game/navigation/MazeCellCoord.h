#pragma once

namespace game::navigation {

struct MazeCellCoord {
    int x = 0;
    int y = 0;
};

constexpr int CellDistance(const MazeCellCoord& a, const MazeCellCoord& b) {
    const int dx = a.x - b.x;
    const int dy = a.y - b.y;
    const int absDx = (dx >= 0) ? dx : -dx;
    const int absDy = (dy >= 0) ? dy : -dy;
    return absDx + absDy;
}

}  // namespace game::navigation
