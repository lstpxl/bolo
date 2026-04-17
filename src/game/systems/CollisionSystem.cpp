#include "game/systems/CollisionSystem.h"

#include "core/Log.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "core/Profiling.h"
#include "game/geometry/WorldGeometry.h"
#include "game/systems/EnemySystemHelpers.h"

namespace {

float BaseSideInsetUnitsFromDamage(int segmentHealth) {
    constexpr float kInsetStepUnits = 3.0F / static_cast<float>(GameplayConstants::kPixelsPerUnit);
    const int clampedHealth = std::clamp(segmentHealth, 0, GameplayConstants::kBaseOuterSegmentMaxHealth);
    const int damagePoints = GameplayConstants::kBaseOuterSegmentMaxHealth - clampedHealth;
    return static_cast<float>(damagePoints) * kInsetStepUnits;
}

struct BaseFootprintBounds {
    float minX = 0.0F;
    float maxX = 0.0F;
    float minY = 0.0F;
    float maxY = 0.0F;
};

BaseFootprintBounds ComputeBaseFootprintBounds(const EnemyBase& base, float clearanceUnits) {
    const float halfBase = GameplayConstants::kEnemyBaseSizeUnits * 0.5F;
    const float leftInset = BaseSideInsetUnitsFromDamage(base.leftSegmentHealth);
    const float rightInset = BaseSideInsetUnitsFromDamage(base.rightSegmentHealth);
    const float topInset = BaseSideInsetUnitsFromDamage(base.topSegmentHealth);
    const float bottomInset = BaseSideInsetUnitsFromDamage(base.bottomSegmentHealth);
    return BaseFootprintBounds{
        .minX = base.position.x - halfBase + leftInset - clearanceUnits,
        .maxX = base.position.x + halfBase - rightInset + clearanceUnits,
        .minY = base.position.y - halfBase + topInset - clearanceUnits,
        .maxY = base.position.y + halfBase - bottomInset + clearanceUnits,
    };
}

bool IsPointInsideBaseFootprint(const Vec2f& point, const EnemyBase& base, float clearanceUnits) {
    const BaseFootprintBounds bounds = ComputeBaseFootprintBounds(base, clearanceUnits);
    return point.x >= bounds.minX && point.x <= bounds.maxX &&
        point.y >= bounds.minY && point.y <= bounds.maxY;
}

BaseOuterSegment FootprintSideHit(
    const Vec2f& point,
    const EnemyBase& base,
    const BaseFootprintBounds& bounds) {
    const float distLeft = std::fabs(point.x - bounds.minX);
    const float distRight = std::fabs(point.x - bounds.maxX);
    const float distTop = std::fabs(point.y - bounds.minY);
    const float distBottom = std::fabs(point.y - bounds.maxY);

    float bestDistance = distLeft;
    BaseOuterSegment bestSide = BaseOuterSegment::Left;
    if (distRight < bestDistance) {
        bestDistance = distRight;
        bestSide = BaseOuterSegment::Right;
    }
    if (distTop < bestDistance) {
        bestDistance = distTop;
        bestSide = BaseOuterSegment::Top;
    }
    if (distBottom < bestDistance) {
        bestSide = BaseOuterSegment::Bottom;
    }

    // Tie-breaker for ambiguous points near corners/center: preserve previous dominant-axis behavior.
    const float dx = point.x - base.position.x;
    const float dy = point.y - base.position.y;
    if (std::fabs(dx) == std::fabs(dy)) {
        return dx >= 0.0F ? BaseOuterSegment::Right : BaseOuterSegment::Left;
    }
    return bestSide;
}

bool IsCentralThirdOnHealthySide(const Vec2f& point, const EnemyBase& base, BaseOuterSegment side) {
    const float healthySideLength = GameplayConstants::kEnemyBaseSizeUnits;
    const float halfCentralThird = healthySideLength / 6.0F;
    const float dx = point.x - base.position.x;
    const float dy = point.y - base.position.y;
    if (side == BaseOuterSegment::Top || side == BaseOuterSegment::Bottom) {
        return std::fabs(dx) <= halfCentralThird;
    }
    return std::fabs(dy) <= halfCentralThird;
}

BaseOuterSegment AdjacentSideFromBrokenImpact(
    const Vec2f& point,
    const EnemyBase& base,
    BaseOuterSegment brokenSide) {
    const float dx = point.x - base.position.x;
    const float dy = point.y - base.position.y;
    if (brokenSide == BaseOuterSegment::Top || brokenSide == BaseOuterSegment::Bottom) {
        return dx < 0.0F ? BaseOuterSegment::Left : BaseOuterSegment::Right;
    }
    return dy < 0.0F ? BaseOuterSegment::Top : BaseOuterSegment::Bottom;
}

struct EnemyCollisionDeathDebugWindowStats {
    std::uint64_t projectileKills = 0;
    std::uint64_t projectileKillWallContact = 0;
};

EnemyCollisionDeathDebugWindowStats gEnemyCollisionDeathDebugWindowStats{};
std::uint64_t gLastEnemyCollisionDebugPrintedFrame = 0;

/// Player- and enemy-owned shells use the same full-tier enemy hit rules.
bool TryProjectileHitFullTierEnemy(GameState& state, Projectile& projectile) {
    WorldState& world = state.world;
    for (EnemyTank& enemy : world.enemies) {
        if (!enemy.alive) {
            continue;
        }
        if (enemy.simTier != EnemySimTier::Full) {
            continue;
        }
        if (projectile.owner == ProjectileOwner::Enemy && projectile.shooterEnemySessionId != 0U &&
            enemy.spawnSessionId == projectile.shooterEnemySessionId) {
            continue;
        }
        if (DistanceSq(projectile.position, enemy.position) <=
            GameplayConstants::kProjectileHitRadius * GameplayConstants::kProjectileHitRadius) {
            gEnemyCollisionDeathDebugWindowStats.projectileKills += 1;
            if (game::geometry::IsPointInWall(
                    world,
                    enemy.position,
                    GameplayConstants::kWallClearanceForHard)) {
                gEnemyCollisionDeathDebugWindowStats.projectileKillWallContact += 1;
            }
            state.world.gameplayEvents.Push(GameplayEvent{
                .type = GameplayEventType::EnemyDestroyed,
                .position = enemy.position,
                .enemyType = enemy.type,
                .enemySubtype = enemy.subtype,
            });
            enemy.alive = false;
            DecrementOriginBaseAliveCount(world, enemy);
            projectile.alive = false;
            world.score += state.menuSettings.levelNumber * GameplayConstants::kEnemyScorePerLevelMultiplier;
            return true;
        }
    }
    return false;
}
}  // namespace

void UpdateCollisionSystem(GameState& state, float deltaSeconds) {
    (void)deltaSeconds;
    WorldState& world = state.world;
    bool anyBaseDestroyed = false;

    for (Projectile& projectile : world.projectiles) {
        if (!projectile.alive) {
            continue;
        }
        if (game::geometry::IsSegmentObscuredByWall(world, projectile.previousPosition, projectile.position)) {
            const Vec2f hitPosition{
                (projectile.previousPosition.x + projectile.position.x) * 0.5F,
                (projectile.previousPosition.y + projectile.position.y) * 0.5F,
            };
            world.gameplayEvents.Push(GameplayEvent{
                .type = GameplayEventType::ProjectileHitWall,
                .position = hitPosition,
            });
            projectile.alive = false;
            continue;
        }

        if (projectile.owner == ProjectileOwner::Player) {
            if (TryProjectileHitFullTierEnemy(state, projectile)) {
                continue;
            }

            for (int baseIndex = 0; baseIndex < static_cast<int>(world.enemyBases.size()); ++baseIndex) {
                EnemyBase& base = world.enemyBases[static_cast<std::size_t>(baseIndex)];
                if (base.destroyed) {
                    continue;
                }
                if (!IsPointInsideBaseFootprint(projectile.position, base, 0.0F)) {
                    continue;
                }

                const BaseFootprintBounds footprintBounds = ComputeBaseFootprintBounds(base, 0.0F);
                const BaseOuterSegment impactSide =
                    FootprintSideHit(projectile.position, base, footprintBounds);
                int& impactSideHealth = base.SegmentHealthRef(impactSide);
                if (impactSideHealth > 0) {
                    impactSideHealth -= 1;
                    base.repairCountdownSeconds = GameplayConstants::kBaseRepairDelaySeconds;
                    projectile.alive = false;
                    break;
                }
                if (!IsCentralThirdOnHealthySide(projectile.position, base, impactSide)) {
                    const BaseOuterSegment adjacentSide =
                        AdjacentSideFromBrokenImpact(projectile.position, base, impactSide);
                    int& adjacentHealth = base.SegmentHealthRef(adjacentSide);
                    if (adjacentHealth > 0) {
                        adjacentHealth -= 1;
                        base.repairCountdownSeconds = GameplayConstants::kBaseRepairDelaySeconds;
                    }
                    projectile.alive = false;
                    break;
                }

                base.destroyed = true;
                anyBaseDestroyed = true;
                projectile.alive = false;
                world.score += state.menuSettings.levelNumber * GameplayConstants::kBaseScorePerLevelMultiplier;
                if (world.player.fuel < GameplayConstants::kFuelMax) {
                    world.startModeFuelRampStart = world.player.fuel;
                    world.startModeRemainingSeconds = GameplayConstants::kStartModeDurationSeconds;
                    world.startModeReason = StartModeReason::BaseRefuel;
                }
                state.world.gameplayEvents.Push(GameplayEvent{
                    .type = GameplayEventType::BaseDestroyed,
                    .position = base.position,
                    .baseIndex = baseIndex,
                });
                break;
            }
        } else {
            if (world.player.alive &&
                DistanceSq(projectile.position, world.player.position) <=
                    GameplayConstants::kProjectileHitRadius * GameplayConstants::kProjectileHitRadius) {
                projectile.alive = false;
                world.player.alive = false;
                world.playerTurnLostPending = true;
                continue;
            }
            if (TryProjectileHitFullTierEnemy(state, projectile)) {
                continue;
            }
        }
    }

    if (anyBaseDestroyed) {
        world.navigationCache.baseDistanceField.Invalidate();
        world.navigationCache.baseDistanceField.Rebuild(
            world.maze,
            world.navigationCache.cellCoords,
            world.enemyBases);
        world.navigationCache.baseFlowField.Invalidate();
        world.navigationCache.baseFlowField.Rebuild(
            world.maze,
            world.navigationCache.cellCoords,
            world.navigationCache.baseDistanceField);
    }

    const auto& profiler = profiling::Profiler::Instance();
    const std::uint64_t frameIndex = profiler.FrameIndex();
    if (profiler.ShouldEmitPeriodicReport() && frameIndex != gLastEnemyCollisionDebugPrintedFrame) {
        gLastEnemyCollisionDebugPrintedFrame = frameIndex;
        bolt::log::Profile(
            "[ENEMY_KILL_DEBUG_COLLISION] projectile{kills=%llu wallContact=%llu}\n",
            static_cast<unsigned long long>(gEnemyCollisionDeathDebugWindowStats.projectileKills),
            static_cast<unsigned long long>(gEnemyCollisionDeathDebugWindowStats.projectileKillWallContact));
        gEnemyCollisionDeathDebugWindowStats = EnemyCollisionDeathDebugWindowStats{};
    }

    if (!world.player.alive) {
        return;
    }

    if (game::geometry::IsPointInWall(world, world.player.position, GameplayConstants::kWallClearanceForHard)) {
        world.player.alive = false;
        world.playerTurnLostPending = true;
        return;
    }

    for (EnemyTank& enemy : world.enemies) {
        if (!enemy.alive) {
            continue;
        }
        if (enemy.simTier != EnemySimTier::Full) {
            continue;
        }
        if (DistanceSq(world.player.position, enemy.position) <=
            GameplayConstants::kPlayerEnemyCollisionRadius * GameplayConstants::kPlayerEnemyCollisionRadius) {
            world.player.alive = false;
            world.playerTurnLostPending = true;
            if (enemy.type == EnemyType::Torpedo && enemy.aiMode == EnemyAiMode::Ram) {
                state.world.gameplayEvents.Push(GameplayEvent{
                    .type = GameplayEventType::EnemyDestroyed,
                    .position = enemy.position,
                    .enemyType = enemy.type,
                    .enemySubtype = enemy.subtype,
                });
                enemy.alive = false;
                DecrementOriginBaseAliveCount(world, enemy);
            }
            return;
        }
    }

    for (const EnemyBase& base : world.enemyBases) {
        if (base.destroyed) {
            continue;
        }
        if (IsPointInsideBaseFootprint(
                world.player.position,
                base,
                GameplayConstants::kEntityRadiusUnits)) {
            world.player.alive = false;
            world.playerTurnLostPending = true;
            return;
        }
    }
}
