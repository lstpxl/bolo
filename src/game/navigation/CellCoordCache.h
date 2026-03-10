#pragma once

#include <cstdint>
#include "core/Types.h"
#include "game/model/EntityTypes.h"
#include "game/navigation/MazeCellCoord.h"

namespace game::navigation {

class CellCoordCache {
public:
    void ConfigureFromMaze(const MazeState& maze);

    int ClampCellX(float worldX) const;
    int ClampCellY(float worldY) const;
    MazeCellCoord WorldToCell(const Vec2f& worldPos) const;

    int CellIndex(int cellX, int cellY) const;
    int CellHash(int cellX, int cellY) const;
    Vec2f CellCenter(int cellX, int cellY) const;

    bool IsValidCell(int cellX, int cellY) const;
    int WidthCells() const { return widthCells_; }
    int HeightCells() const { return heightCells_; }
    int CellSizeUnits() const { return cellSizeUnits_; }

    bool UpdatePlayerCell(const Vec2f& playerPosition);
    int PlayerCellHash() const { return playerCellHash_; }
    MazeCellCoord PlayerCell() const { return playerCell_; }
    std::uint32_t PlayerCellVersion() const { return playerCellVersion_; }

private:
    int widthCells_ = 0;
    int heightCells_ = 0;
    int cellSizeUnits_ = 1;

    MazeCellCoord playerCell_{};
    int playerCellHash_ = -1;
    std::uint32_t playerCellVersion_ = 0U;
    bool hasPlayerCell_ = false;
};

}  // namespace game::navigation
