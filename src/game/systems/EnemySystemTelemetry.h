#pragma once

#include <cstdint>
#include "game/systems/EnemySystem.h"

const char* EnemyTypeTelemetryLabel(int idx);
void AccumulateEnemyWindowStats(int aliveCount, int visibleCount, int fullTierCount);
void AccumulateEnemyWindowTime(float deltaSeconds);
void AccumulateEnemyWindowPairStats(const EnemyRuntimeStats& frameStats);
void ResetEnemyWindowStats();
void EmitEnemyPeriodicWindowLogs(const EnemyRuntimeStats& frameStats, std::uint64_t frameIndex);
