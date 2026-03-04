#pragma once

#include "game/GameState.h"

namespace game::geometry {
bool IsPointInsideMaze(const WorldState& world, const Vec2f& point, float clearanceUnits);
bool IsPointInWall(const WorldState& world, const Vec2f& point, float clearanceUnits);
bool IsPointInUndestroyedBase(const WorldState& world, const Vec2f& point, float clearanceUnits);
bool SegmentIntersectsWall(const WorldState& world, const Vec2f& from, const Vec2f& to, float clearanceUnits);
}  // namespace game::geometry
