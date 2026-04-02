#pragma once

#include <cstdint>
#include <vector>
#include "core/Random.h"
#include "core/Types.h"
#include "game/GameplayView.h"
#include "game/model/EntityTypes.h"
#include "game/model/WorldState.h"

// Shared math and geometry helpers used by EnemySystem and enemy-type modules.

float DistanceSq(const Vec2f& a, const Vec2f& b);
float Distance(const Vec2f& a, const Vec2f& b);
float DistancePointToSegment(const Vec2f& p, const Vec2f& a, const Vec2f& b);
Vec2f NormalizeOrZero(const Vec2f& v);

float NearestBaseDistance(const WorldState& world, const Vec2f& p);
Vec2f NearestBasePosition(const WorldState& world, const Vec2f& p);

/// Returns true if a segment endpoint is valid (clear of walls and bases).
/// Matches the clearance check used by hunters when building scout segments.
bool IsValidSegmentEndpoint(const WorldState& world, const Vec2f& point);

int RandomRotateDirection(Random& random);

float ChooseBestTurnHeading(
    const WorldState& world,
    const Vec2f& origin,
    float currentHeading,
    const std::array<float, 4>& turnCandidates,
    int candidateCount,
    float requiredDistance);

int EnemyTypeTelemetryIndex(EnemyType type);

/// Speed multiplier by subtype (`Basic` = `0.75`, Hunter `Lord` = `1.25`, else `1.0`).
float EnemySubtypeSpeedMultiplier(EnemyType type, EnemySubtype subtype);

/// Decrements the origin base's active-enemy count and clears `enemy.originBaseIndex`.
/// Safe to call on enemies with no valid origin base (index is clamped/cleared).
void DecrementOriginBaseAliveCount(WorldState& world, EnemyTank& enemy);

/// Returns true if `point` falls within the player's current viewport rectangle.
bool IsInPlayerViewport(const Vec2f& point, const GameState& state, const GameplayView& view);
