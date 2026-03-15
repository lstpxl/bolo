#pragma once

#include <vector>
#include "core/Random.h"
#include "core/Types.h"
#include "game/model/EntityTypes.h"
#include "game/model/WorldState.h"

namespace game::spatial {
class EnemyCellOccupancy;
}

bool PlayerAheadForTorpedo(const EnemyTank& enemy, const Vec2f& toPlayerNormalized);
bool PlayerAheadForTorpedoRam(const EnemyTank& enemy, const Vec2f& toPlayerNormalized);

float SelectBestLongStraightHeading(const WorldState& world, const EnemyTank& enemy);

float SelectTorpedoMoveHeading(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    EnemyTank& enemy,
    Random& random,
    bool& startRetreat,
    bool& decidedStraight,
    const game::spatial::EnemyCellOccupancy* rayQueryOccupancy);

void EnterTorpedoTargetingMode(EnemyTank& enemy);
void EnterTorpedoRotateMode(EnemyTank& enemy);
float UpdateTorpedoRotateHeading(EnemyTank& enemy, float deltaSeconds);
float UpdateTorpedoHeadingToward(
    float currentHeadingRadians,
    float targetHeadingRadians,
    float maxTurnSpeedRadiansPerSecond,
    float deltaSeconds);
