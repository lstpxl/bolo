#pragma once

#include <vector>
#include "core/Random.h"
#include "game/model/EntityTypes.h"
#include "game/navigation/CellCoordCache.h"
#include "game/navigation/PlayerFlowField.h"

struct WorldState;

enum class UncoupleReason {
    FrontalCollision,
    SeparationProximity,
    SelfWallContact,
};

const char* UncoupleReasonLabel(UncoupleReason reason);
void RestoreFromUncoupleMode(EnemyTank& enemy);

float SelectUncoupleHeading(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    float fallbackHeading,
    Random& random);

void EnterUncoupleMode(
    std::vector<EnemyTank>& enemies,
    int leadingIndex,
    int uncoupleIndex,
    UncoupleReason reason,
    float movedLastFrameUnits = -1.0F);

bool ShouldEnterSeparationUncouple(const EnemyTank& a, const EnemyTank& b, float distSq);

float ComputeUncoupleEscapeScore(
    const WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    const game::navigation::PlayerFlowField& flowField,
    const std::vector<EnemyTank>& enemies,
    int selfIndex);
