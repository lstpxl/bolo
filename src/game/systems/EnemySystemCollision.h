#pragma once

#include <cstdint>
#include <vector>
#include "game/systems/EnemySystem.h"
#include "game/spatial/SweepPruneBroadPhase.h"

struct WorldState;

using EnterUncoupleCallback = void (*)(std::vector<EnemyTank>& enemies, int selfIndex, int partnerIndex, int reasonCode);
using ShouldEnterSeparationCallback = bool (*)(const EnemyTank& a, const EnemyTank& b, float distSq);

void ResolveEnemyCollisionsSinglePass(
    WorldState& world,
    EnemyRuntimeStats& frameStats,
    const std::vector<Vec2f>& frameStartPositions,
    game::spatial::SweepPruneBroadPhase& broadPhase,
    const std::vector<std::uint8_t>& includeMask,
    const std::vector<std::uint8_t>& reenteredFullTierMask,
    EnterUncoupleCallback enterUncoupleMode,
    ShouldEnterSeparationCallback shouldEnterSeparationUncouple);
