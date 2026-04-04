#include "game/systems/EnemySystemCombatPhase.h"

#include <cmath>
#include "core/AngleMath.h"
#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"
#include "game/systems/EnemyDrone.h"
#include "game/systems/EnemySystemHelpers.h"
#include "game/systems/EnemyTorpedo.h"
#include "game/systems/ProjectileSystem.h"

namespace {

/// `cos(kTorpedoForwardVisionHalfAngleRadians)` (±45°); keep in sync if that constant changes.
constexpr float kCosTorpedoForwardVisionHalfAngle = 0.70710677F;

float ForwardVisionMinCosDot(EnemyType type) {
    if (type == EnemyType::Torpedo) {
        return kCosTorpedoForwardVisionHalfAngle;
    }
    return 0.0F;  // cos(π/2) for kEnemyForwardVisionHalfAngleRadiansDefault
}

float EnemyVisualDetectRangeUnits(EnemyType type) {
    switch (type) {
    case EnemyType::Drone:
        return GameplayConstants::kDroneDetectRangeUnits;
    case EnemyType::Torpedo:
        // Use Ram-detect distance for visual contact acquisition.
        return GameplayConstants::kTorpedoRamDetectRangeUnits;
    case EnemyType::Hunter:
        return GameplayConstants::kHunterDetectRangeUnits;
    case EnemyType::Assassin:
        return GameplayConstants::kEnemyAggroRangeUnits;
    }
    return GameplayConstants::kDroneDetectRangeUnits;
}

/// Max distance at which an enemy may spawn a projectile toward the player — aligned with
/// per-type detection / debug LOS radii (`GameplayConstants`).
float EnemyProjectileMaxRangeUnits(const EnemyTank& enemy) {
    switch (enemy.type) {
    case EnemyType::Drone:
        return GameplayConstants::kDroneDetectRangeUnits;
    case EnemyType::Torpedo:
        return enemy.aiMode == EnemyAiMode::Ram ? GameplayConstants::kTorpedoRamDetectRangeUnits
                                                 : GameplayConstants::kTorpedoDetectRangeUnits;
    case EnemyType::Hunter:
        return GameplayConstants::kHunterDetectRangeUnits;
    case EnemyType::Assassin:
        return GameplayConstants::kEnemyAggroRangeUnits;
    }
    return GameplayConstants::kDroneDetectRangeUnits;
}

}  // namespace

float EnemyFireInterval(EnemyType type) {
    switch (type) {
    case EnemyType::Drone:
        return GameplayConstants::kEnemyDroneFireInterval;
    case EnemyType::Torpedo:
        return GameplayConstants::kEnemyTorpedoFireInterval;
    case EnemyType::Hunter:
        return GameplayConstants::kEnemyHunterFireInterval;
    case EnemyType::Assassin:
        return GameplayConstants::kEnemyAssassinFireInterval;
    }
    return GameplayConstants::kEnemyAssassinFireInterval;
}

EnemyPerception RunPerceptionPhase(
    GameState& state,
    EnemyTank& enemy,
    float deltaSeconds,
    bool playerInvisible,
    Random& random) {
    EnemyPerception perception{};
    if (enemy.selfAwarenessIntervalSeconds <= 0.0F) {
        enemy.selfAwarenessIntervalSeconds = (enemy.type == EnemyType::Drone)
            ? random.NextFloat(
                  GameplayConstants::kDroneSelfAwarenessIntervalMinSeconds,
                  GameplayConstants::kDroneSelfAwarenessIntervalMaxSeconds)
            : random.NextFloat(4.0F, 8.0F);
        enemy.selfAwarenessTimerSeconds = enemy.selfAwarenessIntervalSeconds;
    }
    enemy.selfAwarenessTimerSeconds -= deltaSeconds;
    if (enemy.selfAwarenessTimerSeconds <= 0.0F) {
        TryDroneSelfAwarenessReset(state.world, enemy, random);
        if (enemy.type == EnemyType::Drone) {
            enemy.selfAwarenessIntervalSeconds = random.NextFloat(
                GameplayConstants::kDroneSelfAwarenessIntervalMinSeconds,
                GameplayConstants::kDroneSelfAwarenessIntervalMaxSeconds);
        }
        enemy.selfAwarenessTimerSeconds = enemy.selfAwarenessIntervalSeconds;
    }
    enemy.aiModeElapsedSeconds += deltaSeconds;
    perception.toPlayer = Vec2f{
        .x = state.world.player.position.x - enemy.position.x,
        .y = state.world.player.position.y - enemy.position.y,
    };
    perception.toPlayerNormalized = NormalizeOrZero(perception.toPlayer);
    perception.distanceToPlayerSq = DistanceSq(enemy.position, state.world.player.position);
    perception.distanceToPlayer = std::sqrt(perception.distanceToPlayerSq);
    perception.playerObscured =
        playerInvisible ||
        game::geometry::IsSegmentObscuredByWall(state.world, enemy.position, state.world.player.position);
    const float detectRange = EnemyVisualDetectRangeUnits(enemy.type);
    const float detectRangeSq = detectRange * detectRange;
    enemy.seesPlayer = state.world.player.alive && !perception.playerObscured &&
        perception.distanceToPlayerSq <= detectRangeSq;
    if (enemy.seesPlayer && perception.distanceToPlayerSq > 1.0e-8F) {
        const Vec2f forward = core::angle::DirectionFromHeading(enemy.headingRadians);
        if (!game::geometry::IsWithinForwardCone2D(
                forward,
                perception.toPlayer,
                perception.distanceToPlayer,
                ForwardVisionMinCosDot(enemy.type))) {
            enemy.seesPlayer = false;
        }
    }
    {
        const float aggroRangeSq =
            GameplayConstants::kEnemyAggroRangeUnits * GameplayConstants::kEnemyAggroRangeUnits;
        perception.assassinInAggroMode = enemy.type == EnemyType::Assassin && enemy.seesPlayer &&
            perception.distanceToPlayerSq <= aggroRangeSq;
    }
    return perception;
}

void RunFiringPhase(
    GameState& state,
    EnemyTank& enemy,
    const EnemyPerception& perception,
    float deltaSeconds) {
    enemy.fireCooldownSeconds -= deltaSeconds;
    bool canFireTypeSpecific = true;
    if (enemy.type == EnemyType::Drone) {
        canFireTypeSpecific = enemy.aiMode == EnemyAiMode::Defend;
    } else if (enemy.type == EnemyType::Torpedo) {
        canFireTypeSpecific = (enemy.aiMode == EnemyAiMode::Ram)
            ? PlayerAheadForTorpedoRam(enemy, perception.toPlayerNormalized)
            : PlayerAheadForTorpedo(enemy, perception.toPlayerNormalized);
    }
    const float fireRange = EnemyProjectileMaxRangeUnits(enemy);
    const float fireRangeSq = fireRange * fireRange;
    if (state.world.player.alive &&
        enemy.simTier == EnemySimTier::Full &&
        enemy.seesPlayer &&
        enemy.fireCooldownSeconds <= 0.0F &&
        !perception.playerObscured &&
        canFireTypeSpecific &&
        perception.distanceToPlayerSq <= fireRangeSq) {
        const float headingToPlayer = std::atan2(perception.toPlayer.x, -perception.toPlayer.y);
        const float quantizedHeadingToPlayer = core::angle::QuantizeToEightDirections(headingToPlayer);
        SpawnProjectile(
            state,
            ProjectileOwner::Enemy,
            enemy.position,
            quantizedHeadingToPlayer,
            GameplayConstants::kEnemyProjectileSpeed,
            enemy.spawnSessionId);
        enemy.fireCooldownSeconds = EnemyFireInterval(enemy.type);
    }
}
