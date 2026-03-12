#pragma once

#include "core/Types.h"
#include "core/Random.h"
#include "game/GameplayView.h"
#include "game/navigation/MazeCellCoord.h"
#include "game/navigation/FlowRebuildWorker.h"

struct GameState;

struct EnemyRuntimeStats {
    int aliveCount = 0;
    int visibleInViewportCount = 0;
    int fullTierCount = 0;
    int cheapTierCount = 0;
    int fullTierInBaseClearanceCount = 0;
    int frontalPairsVisited = 0;
    int frontalPairsDistanceChecks = 0;
    int separationPairsVisited = 0;
    int separationPairsResolved = 0;
};

void UpdateEnemySystem(
    GameState& state,
    const GameplayView& view,
    float deltaSeconds,
    Random& random,
    game::navigation::FlowRebuildWorker& flowWorker);
const EnemyRuntimeStats& GetEnemyRuntimeStats();

void DebugLogEnemiesAtPosition(
    const GameState& state,
    const Vec2f& worldPosition,
    const game::navigation::MazeCellCoord& clickedCell);
