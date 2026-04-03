#pragma once

#include "game/model/WorldState.h"
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
    const game::spatial::EnemyCellOccupancy* rayQueryOccupancy = nullptr,
    std::vector<int>* candidateIndicesScratch = nullptr,
    std::vector<std::uint32_t>* raySeenMarksScratch = nullptr,
    std::uint32_t* raySeenEpochScratch = nullptr);

/// Returns distance to nearest wall in any of 8 directions (for debug visualization).
float DistanceToNearestWall(const WorldState& world, const Vec2f& point, float maxProbeDistance);

/// 2D forward cone: true if the angle between unit `forwardUnit` and `toTarget` is at most the
/// cone half-angle α (radians), i.e. dot(forwardUnit, normalize(toTarget)) ≥ cos(α). Pass
/// `minCosHalfAngle = std::cos(α)`. Uses `distanceToTarget` as |toTarget| so callers avoid an extra
/// normalize (and stay well-defined when a separate “normalized” vector clamps to zero).
/// Preconditions: `forwardUnit` is unit length; `distanceToTarget` > 0 and equals |toTarget|.
inline bool IsWithinForwardCone2D(
    const Vec2f& forwardUnit,
    const Vec2f& toTarget,
    float distanceToTarget,
    float minCosHalfAngle) {
    const float dotForwardToTarget =
        (forwardUnit.x * toTarget.x + forwardUnit.y * toTarget.y) / distanceToTarget;
    return dotForwardToTarget >= minCosHalfAngle;
}

}  // namespace game::geometry
