#pragma once

#include <cstdint>
#include <vector>
#include "game/systems/EnemySystem.h"
#include "game/spatial/EnemySpatialGrid.h"
#include "game/spatial/SweepPruneBroadPhase.h"

using EnterUncoupleCallback = void (*)(std::vector<EnemyTank>& enemies, int selfIndex, int partnerIndex, int reasonCode);
using ShouldEnterSeparationCallback = bool (*)(const EnemyTank& a, const EnemyTank& b, float distSq);

void ResolveEnemySeparationLegacyGrid(
    WorldState& world,
    EnemyRuntimeStats& frameStats,
    game::spatial::EnemySpatialGrid& grid,
    const std::vector<std::uint8_t>& includeMask,
    const std::vector<std::uint8_t>& reenteredFullTierMask,
    EnterUncoupleCallback enterUncoupleMode,
    ShouldEnterSeparationCallback shouldEnterSeparationUncouple);

void ResolveEnemyFrontalCollisionsLegacyGrid(
    WorldState& world,
    EnemyRuntimeStats& frameStats,
    const std::vector<Vec2f>& frameStartPositions,
    game::spatial::EnemySpatialGrid& grid,
    const std::vector<std::uint8_t>& includeMask,
    const std::vector<std::uint8_t>& reenteredFullTierMask,
    EnterUncoupleCallback enterUncoupleMode);

void ResolveEnemyCollisionsSinglePass(
    WorldState& world,
    EnemyRuntimeStats& frameStats,
    const std::vector<Vec2f>& frameStartPositions,
    game::spatial::SweepPruneBroadPhase& broadPhase,
    const std::vector<std::uint8_t>& includeMask,
    const std::vector<std::uint8_t>& reenteredFullTierMask,
    EnterUncoupleCallback enterUncoupleMode,
    ShouldEnterSeparationCallback shouldEnterSeparationUncouple);
