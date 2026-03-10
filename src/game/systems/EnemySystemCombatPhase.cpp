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
constexpr float kDroneBaseBearingThresholdRadians = 1.3962634F;  // 80 degrees

bool IsInPlayerViewport(const Vec2f& point, const GameState& state, const GameplayView& view) {
    const float halfWidth = view.viewportWidthUnits * 0.5F;
    const float halfHeight = view.viewportHeightUnits * 0.5F;
    const Vec2f center = state.world.player.position;
    return point.x >= center.x - halfWidth && point.x <= center.x + halfWidth &&
        point.y >= center.y - halfHeight && point.y <= center.y + halfHeight;
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
            ? random.NextFloat(6.0F, 12.0F)
            : random.NextFloat(4.0F, 8.0F);
        enemy.selfAwarenessTimerSeconds = enemy.selfAwarenessIntervalSeconds;
    }
    enemy.selfAwarenessTimerSeconds -= deltaSeconds;
    if (enemy.selfAwarenessTimerSeconds <= 0.0F) {
        enemy.selfAwarenessTimerSeconds = enemy.selfAwarenessIntervalSeconds;
        if (enemy.type == EnemyType::Drone) {
            const float nearestBaseDist = NearestBaseDistance(state.world, enemy.position);
            if (nearestBaseDist >= 36.0F) {
                const Vec2f nearestBase = NearestBasePosition(state.world, enemy.position);
                const Vec2f toBase{
                    .x = nearestBase.x - enemy.position.x,
                    .y = nearestBase.y - enemy.position.y,
                };
                const float headingToBase = std::atan2(toBase.x, -toBase.y);
                const float relativeBearing = core::angle::AngleDistance(enemy.headingRadians, headingToBase);
                if (relativeBearing >= kDroneBaseBearingThresholdRadians) {
                    enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
                    EnterDroneWatchMode(state.world, enemy, random);
                }
            }
        }
    }
    enemy.aiModeElapsedSeconds += deltaSeconds;
    perception.toPlayer = Vec2f{
        .x = state.world.player.position.x - enemy.position.x,
        .y = state.world.player.position.y - enemy.position.y,
    };
    perception.toPlayerNormalized = NormalizeOrZero(perception.toPlayer);
    perception.distanceToPlayerSq = DistanceSq(enemy.position, state.world.player.position);
    perception.distanceToPlayer = std::sqrt(perception.distanceToPlayerSq);
    const bool playerInAggroRange =
        !playerInvisible &&
        perception.distanceToPlayerSq <=
            (GameplayConstants::kEnemyAggroRangeUnits * GameplayConstants::kEnemyAggroRangeUnits);
    perception.playerObscured =
        playerInvisible ||
        game::geometry::IsSegmentObscuredByWall(state.world, enemy.position, state.world.player.position);
    perception.assassinHasLineOfSight =
        enemy.type == EnemyType::Assassin && playerInAggroRange && !perception.playerObscured;
    return perception;
}

void RunFiringPhase(
    GameState& state,
    EnemyTank& enemy,
    const EnemyPerception& perception,
    const GameplayView& view,
    float deltaSeconds) {
    enemy.fireCooldownSeconds -= deltaSeconds;
    const bool enemyVisibleInViewport = IsInPlayerViewport(enemy.position, state, view);
    bool canFireTypeSpecific = true;
    if (enemy.type == EnemyType::Torpedo) {
        canFireTypeSpecific = PlayerAheadForTorpedo(enemy, perception.toPlayerNormalized);
    }
    if (state.world.player.alive &&
        enemy.fireCooldownSeconds <= 0.0F &&
        enemyVisibleInViewport &&
        !perception.playerObscured &&
        canFireTypeSpecific &&
        perception.distanceToPlayerSq <
            (GameplayConstants::kEnemyFireRangeUnits * GameplayConstants::kEnemyFireRangeUnits)) {
        const float headingToPlayer = std::atan2(perception.toPlayer.x, -perception.toPlayer.y);
        const float quantizedHeadingToPlayer = core::angle::QuantizeToEightDirections(headingToPlayer);
        SpawnProjectile(
            state,
            ProjectileOwner::Enemy,
            enemy.position,
            quantizedHeadingToPlayer,
            GameplayConstants::kEnemyProjectileSpeed);
        enemy.fireCooldownSeconds = EnemyFireInterval(enemy.type);
    }
}
