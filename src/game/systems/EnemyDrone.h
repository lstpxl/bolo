#pragma once

#include <vector>
#include "core/Random.h"
#include "core/Types.h"
#include "game/model/EntityTypes.h"
#include "game/model/WorldState.h"

void EnterDroneWatchMode(WorldState& world, EnemyTank& enemy, Random& random);

bool SelectDroneReturnToBaseHeading(
    const WorldState& world,
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
