#pragma once

#include <vector>
#include "core/Random.h"
#include "core/Types.h"
#include "game/model/EntityTypes.h"
#include "game/model/WorldState.h"
#include "game/navigation/CellCoordCache.h"

namespace game::spatial {
class EnemyCellOccupancy;
}

bool PlayerAheadForTorpedo(const EnemyTank& enemy, const Vec2f& toPlayerNormalized);
bool PlayerAheadForTorpedoRam(const EnemyTank& enemy, const Vec2f& toPlayerNormalized);

float SelectBestLongStraightHeading(const WorldState& world, const EnemyTank& enemy);

void InvalidateTorpedoFlyPath(EnemyTank& enemy);

bool SelectTorpedoFlyMotion(
    const WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    EnemyTank& enemy,
    Random& random,
    float& outHeadingRadians,
    Vec2f& outTargetPoint,
    bool snapHeadingToEightDirections = true);

float SelectTorpedoMoveHeading(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    EnemyTank& enemy,
    Random& random,
    bool& startRetreat,
    bool& decidedStraight,
    const game::spatial::EnemyCellOccupancy* rayQueryOccupancy,
    std::vector<int>* candidateIndicesScratch = nullptr,
    std::vector<std::uint32_t>* raySeenMarksScratch = nullptr,
    std::uint32_t* raySeenEpochScratch = nullptr);

void EnterTorpedoTargetingMode(EnemyTank& enemy);
void EnterTorpedoRotateMode(EnemyTank& enemy);
float UpdateTorpedoRotateHeading(EnemyTank& enemy, float deltaSeconds);
float UpdateTorpedoHeadingToward(
    float currentHeadingRadians,
    float targetHeadingRadians,
    float maxTurnSpeedRadiansPerSecond,
    float deltaSeconds);
