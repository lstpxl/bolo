#pragma once

#include "game/model/EntityTypes.h"
#include "game/model/WorldState.h"

struct GameState;

namespace game::navigation {
class CellCoordCache;
class PlayerFlowField;
}

bool TrySelectAssassinFlowNextStep(
    const game::navigation::CellCoordCache& cellCache,
    const game::navigation::PlayerFlowField& flowField,
    EnemyTank& enemy,
    bool& outNeedsInitialFlowBuild,
    float& outHeadingRadians);

bool BuildAssassinCheapFlowSegment(
    WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    const game::navigation::PlayerFlowField& flowField,
    EnemyTank& enemy,
    bool& outNeedsInitialFlowBuild);

bool TryGetAssassinFlowHeading(
    const game::navigation::CellCoordCache& cellCache,
    const game::navigation::PlayerFlowField& flowField,
    const EnemyTank& enemy,
    float& outHeadingRadians);
