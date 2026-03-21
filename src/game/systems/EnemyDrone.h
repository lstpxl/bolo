#pragma once

#include <vector>
#include "core/Random.h"
#include "core/Types.h"
#include "game/model/EntityTypes.h"
#include "game/model/WorldState.h"

void EnterDroneWatchMode(WorldState& world, EnemyTank& enemy, Random& random);

/// True when drone is far enough from the nearest alive base to consider return-to-base (uses `BaseDistanceField`
/// graph distance in cells). Builds distance + flow caches if missing.
bool DroneIsFarEnoughForReturnToBase(WorldState& world, const Vec2f& position);

/// Sets `outHeading` to an 8-way heading along `BaseFlowField` toward the nearest base (next cell center).
/// Builds distance + flow caches if missing. Returns false if there is no outbound flow step.
bool DroneTryHeadingTowardBaseAlongFlow(WorldState& world, const Vec2f& position, float& outHeading);

bool SelectDroneReturnToBaseHeading(
    WorldState& world,
    const EnemyTank& enemy,
    Random& random,
    float& selectedHeading);

bool SelectDroneWatchEscapeHeading(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    float deltaSeconds,
    float& selectedHeading);

bool SelectDronePursuitHeading(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    const Vec2f& playerPosition,
    float stepDistance,
    float playerAvoidDistanceUnits,
    float& selectedHeading);
