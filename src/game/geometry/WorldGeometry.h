#pragma once

#include "game/GameState.h"
#include <vector>

namespace game::spatial {
class EnemyCellOccupancy;
}

namespace game::geometry {

bool IsPointInsideMaze(const WorldState& world, const Vec2f& point, float clearanceUnits);
bool IsPointInWall(const WorldState& world, const Vec2f& point, float clearanceUnits);
bool IsPointInUndestroyedBase(const WorldState& world, const Vec2f& point, float clearanceUnits);
bool SegmentIntersectsWall(const WorldState& world, const Vec2f& from, const Vec2f& to, float clearanceUnits);
bool IsSegmentObscuredByWall(const WorldState& world, const Vec2f& from, const Vec2f& to);

// Legacy sampled clearance method (adaptive samples along ray).
float FreeDistanceAheadContinuous(const WorldState& world, const Vec2f& from, float headingRadians,
    float maxDistance, float clearanceUnits, float planningClearanceScale = 1.0F);

// Grid-traversal clearance method (cell-by-cell traversal in maze grid).
float FreeDistanceAheadGrid(const WorldState& world, const Vec2f& from, float headingRadians,
    float maxDistance, float clearanceUnits, float planningClearanceScale = 1.0F);

// Grid traversal against maze walls and boundary only (ignores enemy bases).
float FreeDistanceAheadGridWallsOnly(const WorldState& world, const Vec2f& from, float headingRadians,
    float maxDistance, float clearanceUnits, float planningClearanceScale = 1.0F);

// Default static-obstacle clearance method used by gameplay code.
float FreeDistanceAhead(const WorldState& world, const Vec2f& from, float headingRadians,
    float maxDistance, float clearanceUnits, float planningClearanceScale = 1.0F);

float FreeDistanceAheadWithEnemies(const WorldState& world,
    const std::vector<EnemyTank>& enemies, int selfIndex, const Vec2f& from,
    float headingRadians, float maxDistance, float clearanceUnits,
    float planningClearanceScale = 1.0F,
    const game::spatial::EnemyCellOccupancy* rayQueryOccupancy = nullptr);

}  // namespace game::geometry
