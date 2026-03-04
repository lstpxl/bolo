#pragma once

#include "core/Types.h"
#include "game/model/EntityTypes.h"
#include <functional>
#include <vector>

struct WorldState;

namespace game::spatial {

/// 6×6 unit grid over the maze. Assigns enemies to cells for broad-phase spatial queries.
/// Reused each frame; build then query.
class EnemySpatialGrid {
public:
    static constexpr float kCellSizeUnits = 6.0F;

    /// Build grid from current enemy positions (for separation, ray queries).
    /// If positionsOverride is non-null and matches enemy count, use those positions instead.
    void BuildFromPositions(const WorldState& world,
        const std::vector<Vec2f>* positionsOverride = nullptr);

    /// Build grid from frame-start and current positions (for frontal collisions).
    /// Inserts each enemy into cells at both positions to catch crossing segments.
    void BuildFromSegments(const WorldState& world,
        const std::vector<Vec2f>& frameStartPositions);

    /// Call fn(i, j) for each pair of alive enemies in same or adjacent cell.
    /// i < j. Each pair is invoked at most once.
    void ForEachPairInSameOrAdjacentCell(
        const std::vector<EnemyTank>& enemies,
        std::function<void(int i, int j)> fn) const;

    /// Append enemy indices (excluding selfIndex) that are in the cell at point,
    /// or in any of the 8 neighboring cells.
    void GetEnemiesNearPoint(const std::vector<EnemyTank>& enemies,
        int selfIndex, const Vec2f& point,
        std::vector<int>& out) const;

    /// Append enemy indices (excluding selfIndex) in cells the ray passes through.
    /// Ray from `from` along `dir` for up to `maxDist`; dir should be normalized.
    void GetEnemiesAlongRay(const std::vector<EnemyTank>& enemies,
        int selfIndex, const Vec2f& from, const Vec2f& dir, float maxDist,
        std::vector<int>& out) const;

private:
    int WorldToCellX(float x) const;
    int WorldToCellY(float y) const;
    int CellIndex(int cx, int cy) const;
    bool IsValidCell(int cx, int cy) const;
    void InsertEnemyAt(int enemyIndex, float worldX, float worldY);
    void VisitCellAndNeighbors(int cx, int cy,
        std::function<void(int cellIdx)> visitor) const;

    int widthCells_ = 0;
    int heightCells_ = 0;
    std::vector<std::vector<int>> cells_;
};

}  // namespace game::spatial
