#pragma once

#include "game/GameState.h"
#include <vector>

namespace game::spatial {
class EnemySpatialGrid;
}

namespace game::geometry {

bool IsPointInsideMaze(const WorldState& world, const Vec2f& point, float clearanceUnits);
bool IsPointInWall(const WorldState& world, const Vec2f& point, float clearanceUnits);
bool IsPointInUndestroyedBase(const WorldState& world, const Vec2f& point, float clearanceUnits);
bool SegmentIntersectsWall(const WorldState& world, const Vec2f& from, const Vec2f& to, float clearanceUnits);
bool IsSegmentObscuredByWall(const WorldState& world, const Vec2f& from, const Vec2f& to);

float FreeDistanceAhead(const WorldState& world, const Vec2f& from, float headingRadians,
    float maxDistance, float clearanceUnits, float planningClearanceScale = 1.0F);

float FreeDistanceAheadWithEnemies(const WorldState& world,
    const std::vector<EnemyTank>& enemies, int selfIndex, const Vec2f& from,
    float headingRadians, float maxDistance, float clearanceUnits,
    float planningClearanceScale = 1.0F,
    const game::spatial::EnemySpatialGrid* spatialGrid = nullptr);

}  // namespace game::geometry
