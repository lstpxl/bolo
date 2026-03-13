#include "game/systems/EnemySystemUncouple.h"

#include <algorithm>
#include <cmath>
#include "core/AngleMath.h"
#include "game/model/WorldState.h"
#include "core/Log.h"
#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"
#include "game/systems/EnemyAssassin.h"
#include "game/systems/EnemySystemHelpers.h"
#include "game/systems/EnemySystemInternal.h"

namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kEightDirectionStep = kPi / 4.0F;
constexpr float kEnemyUncoupleDurationSeconds = 1.0F;
constexpr float kUncoupleEnemyForceRangeUnits = GameplayConstants::kEnemyPreferredSeparationUnits * 2.0F;
constexpr float kUncoupleWallProbeRangeUnits = 1.5F;
constexpr float kUncoupleObstacleProbeRangeUnits = 2.0F;
constexpr float kUncoupleObstacleAheadDotThreshold = 0.35F;
constexpr float kUncouplePathFollowForceScale = 0.6F;
constexpr float kUncoupleObstacleForceScale = 0.9F;
constexpr float kUncoupleWallForceScale = 1.2F;
constexpr float kUncoupleRandomForceScale = 0.08F;
constexpr float kUncoupleReentryDistanceUnits = 1.5F;
constexpr float kUncouplePriorityCrowdingRangeUnits = GameplayConstants::kEnemyPreferredSeparationUnits * 1.5F;
constexpr float kUncouplePriorityClearProbeUnits = 3.0F;
constexpr float kUncoupleBlockedHeadingThresholdUnits = 0.10F;
constexpr std::uint64_t kUncoupleEventSampleCap = 16U;

float NormalizeAngle(float angleRadians) {
    return core::angle::NormalizeAngle(angleRadians);
}

float QuantizeToEightDirections(float angleRadians) {
    return core::angle::QuantizeToEightDirections(angleRadians);
}

float SignedAngleDelta(float fromRadians, float toRadians) {
    return core::angle::SignedAngleDelta(fromRadians, toRadians);
}

Vec2f DirectionFromHeading(float headingRadians) {
    return core::angle::DirectionFromHeading(headingRadians);
}

EnemyAiMode DefaultAiModeForType(EnemyType type) {
    switch (type) {
    case EnemyType::Drone:
        return EnemyAiMode::Wander;
    case EnemyType::Torpedo:
        return EnemyAiMode::Wander;
    case EnemyType::Hunter:
        return EnemyAiMode::Scout;
    case EnemyType::Assassin:
        return EnemyAiMode::Pursuit;
    }
    return EnemyAiMode::Wander;
}
}  // namespace

const char* UncoupleReasonLabel(UncoupleReason reason) {
    switch (reason) {
    case UncoupleReason::FrontalCollision:
        return "frontal";
    case UncoupleReason::SeparationProximity:
        return "separation";
    case UncoupleReason::SelfWallContact:
        return "wallContact";
    }
    return "?";
}

void RestoreFromUncoupleMode(EnemyTank& enemy) {
    EnemyAiMode mode = enemy.preUncoupleAiMode;
    if (mode == EnemyAiMode::Uncouple) {
        mode = DefaultAiModeForType(enemy.type);
    }
    enemy.aiMode = mode;
    enemy.aiModeElapsedSeconds = 0.0F;
    enemy.aiStateTimerSeconds = 0.0F;
}

float SelectUncoupleHeading(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    float fallbackHeading,
    Random& random) {
    const EnemyTank& self = enemies[static_cast<std::size_t>(selfIndex)];
    auto chooseQuantizedHeadingWithClearance = [&](float desiredHeading) {
        const float quantizedDesired = QuantizeToEightDirections(desiredHeading);
        const float quantizedClear = game::geometry::FreeDistanceAheadWithEnemies(
            world,
            enemies,
            selfIndex,
            self.position,
            quantizedDesired,
            kUncouplePriorityClearProbeUnits,
            GameplayConstants::kWallClearanceForAvoidance,
            1.0F);
        if (quantizedClear > kUncoupleBlockedHeadingThresholdUnits) {
            return quantizedDesired;
        }

        float bestHeading = quantizedDesired;
        float bestScore = -1000000.0F;
        for (int step = 0; step < 8; ++step) {
            const float candidateHeading = NormalizeAngle(static_cast<float>(step) * kEightDirectionStep);
            const float clear = game::geometry::FreeDistanceAheadWithEnemies(
                world,
                enemies,
                selfIndex,
                self.position,
                candidateHeading,
                kUncouplePriorityClearProbeUnits,
                GameplayConstants::kWallClearanceForAvoidance,
                1.0F);
            const float alignDesired = std::cos(SignedAngleDelta(candidateHeading, desiredHeading));
            const float alignFallback = std::cos(SignedAngleDelta(candidateHeading, fallbackHeading));
            const float score = clear * 2.0F + alignDesired * 0.6F + alignFallback * 0.25F;
            if (score > bestScore) {
                bestScore = score;
                bestHeading = candidateHeading;
            }
        }
        return bestHeading;
    };

    Vec2f separationForce{.x = 0.0F, .y = 0.0F};
    Vec2f obstacleAvoidanceForce{.x = 0.0F, .y = 0.0F};
    Vec2f wallAvoidanceForce{.x = 0.0F, .y = 0.0F};
    Vec2f randomNoiseForce{.x = 0.0F, .y = 0.0F};

    const float enemyRangeSq = kUncoupleEnemyForceRangeUnits * kUncoupleEnemyForceRangeUnits;
    const Vec2f desiredDir = DirectionFromHeading(QuantizeToEightDirections(fallbackHeading));
    for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
        if (i == selfIndex) {
            continue;
        }
        const EnemyTank& other = enemies[static_cast<std::size_t>(i)];
        if (!other.alive) {
            continue;
        }
        const float distSq = DistanceSq(self.position, other.position);
        if (distSq > enemyRangeSq) {
            continue;
        }

        const float dist = std::sqrt(std::max(0.0F, distSq));
        Vec2f awayDir = NormalizeOrZero(Vec2f{
            .x = self.position.x - other.position.x,
            .y = self.position.y - other.position.y,
        });
        if (awayDir.x == 0.0F && awayDir.y == 0.0F) {
            awayDir = DirectionFromHeading(static_cast<float>(i) * kEightDirectionStep);
        }
        const float separationWeight = 1.0F - std::min(1.0F, dist / kUncoupleEnemyForceRangeUnits);
        separationForce.x += awayDir.x * separationWeight;
        separationForce.y += awayDir.y * separationWeight;

        if (dist <= kUncoupleObstacleProbeRangeUnits) {
            const Vec2f toOtherDir = NormalizeOrZero(Vec2f{
                .x = other.position.x - self.position.x,
                .y = other.position.y - self.position.y,
            });
            const float aheadDot = toOtherDir.x * desiredDir.x + toOtherDir.y * desiredDir.y;
            if (aheadDot >= kUncoupleObstacleAheadDotThreshold) {
                const float obstacleWeight =
                    (1.0F - std::min(1.0F, dist / kUncoupleObstacleProbeRangeUnits)) *
                    kUncoupleObstacleForceScale;
                obstacleAvoidanceForce.x += awayDir.x * obstacleWeight;
                obstacleAvoidanceForce.y += awayDir.y * obstacleWeight;
            }
        }
    }

    for (int step = 0; step < 8; ++step) {
        const float heading = NormalizeAngle(static_cast<float>(step) * kEightDirectionStep);
        const Vec2f dir = DirectionFromHeading(heading);
        const float clear = game::geometry::FreeDistanceAhead(
            world,
            self.position,
            heading,
            kUncoupleWallProbeRangeUnits,
            GameplayConstants::kWallClearanceForAvoidance,
            1.0F);
        if (clear >= kUncoupleWallProbeRangeUnits) {
            continue;
        }
        const float wallForce =
            (1.0F - std::max(0.0F, clear) / kUncoupleWallProbeRangeUnits) * kUncoupleWallForceScale;
        wallAvoidanceForce.x -= dir.x * wallForce;
        wallAvoidanceForce.y -= dir.y * wallForce;
    }

    const float jitterHeading = random.NextFloat(0.0F, kPi * 2.0F);
    const Vec2f jitterDir = DirectionFromHeading(jitterHeading);
    randomNoiseForce.x += jitterDir.x * kUncoupleRandomForceScale;
    randomNoiseForce.y += jitterDir.y * kUncoupleRandomForceScale;

    const float separationMag =
        std::sqrt(separationForce.x * separationForce.x + separationForce.y * separationForce.y);
    const float pathFollowMag = std::min(kUncouplePathFollowForceScale, separationMag);
    const Vec2f pathFollowingForce{
        .x = desiredDir.x * pathFollowMag,
        .y = desiredDir.y * pathFollowMag,
    };

    const Vec2f summedForce{
        .x = pathFollowingForce.x + separationForce.x + obstacleAvoidanceForce.x + wallAvoidanceForce.x +
            randomNoiseForce.x,
        .y = pathFollowingForce.y + separationForce.y + obstacleAvoidanceForce.y + wallAvoidanceForce.y +
            randomNoiseForce.y,
    };

    const Vec2f awayDir = NormalizeOrZero(summedForce);
    if (awayDir.x == 0.0F && awayDir.y == 0.0F) {
        return chooseQuantizedHeadingWithClearance(fallbackHeading);
    }
    return chooseQuantizedHeadingWithClearance(std::atan2(awayDir.x, -awayDir.y));
}

void EnterUncoupleMode(
    std::vector<EnemyTank>& enemies,
    int leadingIndex,
    int uncoupleIndex,
    UncoupleReason reason,
    float movedLastFrameUnits) {
    if (uncoupleIndex < 0 || uncoupleIndex >= static_cast<int>(enemies.size())) {
        return;
    }
    EnemyTank& uncoupled = enemies[static_cast<std::size_t>(uncoupleIndex)];
    if (!uncoupled.alive) {
        return;
    }
    EnemyRuntimeWindowStats& stats = gEnemyRuntimeWindowStats;
    stats.uncoupleEntries += 1;
    switch (reason) {
    case UncoupleReason::FrontalCollision:
        stats.uncoupleEntriesFrontal += 1;
        break;
    case UncoupleReason::SeparationProximity:
        stats.uncoupleEntriesSeparation += 1;
        break;
    case UncoupleReason::SelfWallContact:
        stats.uncoupleEntriesWallContact += 1;
        break;
    }

    const bool alreadyUncouple = uncoupled.aiMode == EnemyAiMode::Uncouple;
    const float timerBeforeReset = uncoupled.aiStateTimerSeconds;
    if (alreadyUncouple) {
        stats.uncoupleReentryResets += 1;
    }
    if (uncoupled.aiMode != EnemyAiMode::Uncouple) {
        if (uncoupled.type == EnemyType::Assassin && uncoupled.offscreenSegmentActive) {
            bolt::log::Profile(
                "[ENEMY_ASSASSIN_SEGMENT_DROP] id=%d reason=enter_uncouple "
                "uncoupleReason=%s pos=(%.3f,%.3f) cell=(%d,%d)\n",
                uncoupleIndex,
                UncoupleReasonLabel(reason),
                uncoupled.position.x,
                uncoupled.position.y,
                uncoupled.cellCoord.x,
                uncoupled.cellCoord.y);
        }
        uncoupled.preUncoupleAiMode = uncoupled.aiMode;
        uncoupled.aiMode = EnemyAiMode::Uncouple;
        uncoupled.aiModeElapsedSeconds = 0.0F;
        uncoupled.aiStateTimerSeconds = kEnemyUncoupleDurationSeconds;
        uncoupled.offscreenSegmentActive = false;
        uncoupled.cheapSegmentBuildFailCount = 0;
        uncoupled.cheapSegmentLastFailCellHash = -1;
        uncoupled.cheapSegmentLastFailReason = CheapSegmentFailReason::None;
        uncoupled.cheapSegmentBuildMethodStage = 0;
        uncoupled.cheapSegmentInsideWallAvoidLastFrame = false;
    } else {
        uncoupled.aiMode = EnemyAiMode::Uncouple;
    }

    // Preserve heading on uncouple re-entry for pair collisions to prevent
    // oscillation from repeated same-frame re-triggers in crowded chokepoints.
    const bool shouldRefreshHeading =
        !alreadyUncouple || reason == UncoupleReason::SelfWallContact;
    if (shouldRefreshHeading) {
        float uncoupleHeading = QuantizeToEightDirections(uncoupled.headingRadians);
        if (leadingIndex >= 0 && leadingIndex < static_cast<int>(enemies.size())) {
            const EnemyTank& leader = enemies[static_cast<std::size_t>(leadingIndex)];
            if (leader.alive) {
                const Vec2f awayDir = NormalizeOrZero(Vec2f{
                    .x = uncoupled.position.x - leader.position.x,
                    .y = uncoupled.position.y - leader.position.y,
                });
                if (awayDir.x != 0.0F || awayDir.y != 0.0F) {
                    uncoupleHeading = QuantizeToEightDirections(std::atan2(awayDir.x, -awayDir.y));
                }
            }
        }
        uncoupled.desiredHeadingRadians = uncoupleHeading;
    }

    const bool shouldSample = alreadyUncouple || reason == UncoupleReason::SelfWallContact;
    if (shouldSample && stats.uncoupleSamplesPrinted < kUncoupleEventSampleCap) {
        bolt::log::Profile(
            "[ENEMY_UNCOUPLE_EVENT] id=%d reason=%s alreadyUncouple=%d timerBeforeReset=%.3f "
            "movedLastFrame=%.3f pos=(%.2f,%.2f) headingBefore=%.3f desiredHeading=%.3f "
            "leader=%d preMode=%d\n",
            uncoupleIndex,
            UncoupleReasonLabel(reason),
            alreadyUncouple ? 1 : 0,
            timerBeforeReset,
            movedLastFrameUnits,
            uncoupled.position.x,
            uncoupled.position.y,
            uncoupled.headingRadians,
            uncoupled.desiredHeadingRadians,
            leadingIndex,
            static_cast<int>(uncoupled.preUncoupleAiMode));
        stats.uncoupleSamplesPrinted += 1;
    }
}

bool ShouldEnterSeparationUncouple(const EnemyTank& a, const EnemyTank& b, float distSq) {
    if (a.aiMode != EnemyAiMode::Uncouple || b.aiMode != EnemyAiMode::Uncouple) {
        return true;
    }
    const float reentryDistSq = kUncoupleReentryDistanceUnits * kUncoupleReentryDistanceUnits;
    return distSq <= reentryDistSq;
}

float ComputeUncoupleEscapeScore(
    const WorldState& world,
    const game::navigation::CellCoordCache& cellCache,
    const game::navigation::PlayerFlowField& flowField,
    const std::vector<EnemyTank>& enemies,
    int selfIndex) {
    if (selfIndex < 0 || selfIndex >= static_cast<int>(enemies.size())) {
        return -1000.0F;
    }
    const EnemyTank& self = enemies[static_cast<std::size_t>(selfIndex)];
    if (!self.alive || self.aiMode != EnemyAiMode::Uncouple) {
        return -1000.0F;
    }

    float strategicHeading = self.desiredHeadingRadians;
    if (self.type == EnemyType::Assassin) {
        float flowHeading = strategicHeading;
        if (TryGetAssassinFlowHeading(cellCache, flowField, self, flowHeading)) {
            strategicHeading = flowHeading;
        }
    }

    const float clearAhead = game::geometry::FreeDistanceAheadWithEnemies(
        world,
        enemies,
        selfIndex,
        self.position,
        strategicHeading,
        kUncouplePriorityClearProbeUnits,
        GameplayConstants::kWallClearanceForAvoidance,
        1.0F);
    const float alignment = std::cos(SignedAngleDelta(self.headingRadians, strategicHeading));

    const float crowdingRangeSq = kUncouplePriorityCrowdingRangeUnits * kUncouplePriorityCrowdingRangeUnits;
    int crowdingCount = 0;
    for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
        if (i == selfIndex) {
            continue;
        }
        const EnemyTank& other = enemies[static_cast<std::size_t>(i)];
        if (!other.alive || other.aiMode != EnemyAiMode::Uncouple) {
            continue;
        }
        if (DistanceSq(self.position, other.position) <= crowdingRangeSq) {
            crowdingCount += 1;
        }
    }

    return clearAhead * 2.0F + alignment * 0.75F - static_cast<float>(crowdingCount) * 0.5F;
}
