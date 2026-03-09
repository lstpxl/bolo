#pragma once

#include <cstddef>
#include <functional>
#include <vector>

namespace game::spatial {

/// Incremental spatial index: maps integer cell coordinates to enemy indices.
/// Enemies update their cell on move; lookups are O(k) for k enemies in the cell.
/// All storage is preallocated; no per-frame allocations.
///
/// Not wired. Caller computes cell coords (e.g. via CellCoordCache::WorldToCell).
class EnemyCellOccupancy {
public:
    static constexpr int kInvalid = -1;

    /// Preallocate for given dimensions. Idempotent; may shrink if called with smaller values.
    void Reserve(int widthCells, int heightCells, int maxEnemies);

    /// Insert or move enemy to cell. Unlinks from previous cell if any.
    /// Clamps cell coords to valid range.
    void SetCell(int enemyIndex, int cellX, int cellY);

    /// Remove enemy from index. No-op if not present.
    void Remove(int enemyIndex);

    /// Invoke fn(enemyIndex) for each enemy in cell (excluding selfIndex if selfIndex >= 0).
    void ForEachInCell(int cellX, int cellY, int selfIndex,
        std::function<void(int enemyIndex)> fn) const;

    /// Return true if any other enemy (index != selfIndex) is in the cell.
    bool HasOtherInCell(int cellX, int cellY, int selfIndex) const;

    /// Number of enemies currently in the cell.
    int CountInCell(int cellX, int cellY) const;

    bool IsConfigured() const { return widthCells_ > 0 && heightCells_ > 0 && maxEnemies_ > 0; }

private:
    int CellIndex(int cx, int cy) const;
    bool IsValidCell(int cx, int cy) const;
    void Unlink(int enemyIndex);
    void Link(int enemyIndex, int cellIdx);

    int widthCells_ = 0;
    int heightCells_ = 0;
    int maxEnemies_ = 0;
    std::vector<int> cellFirst_{};   // head per cell, kInvalid = empty
    std::vector<int> enemyNext_{};  // next in same cell
    std::vector<int> enemyPrev_{};  // prev in same cell
    std::vector<int> enemyCell_{};  // cell index for this enemy, kInvalid = not inserted
};

}  // namespace game::spatial
