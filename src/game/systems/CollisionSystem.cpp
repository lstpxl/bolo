#include "game/systems/CollisionSystem.h"

#include "core/Log.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "core/Profiling.h"
#include "game/geometry/WorldGeometry.h"

namespace {
float DistanceSq(const Vec2f& a, const Vec2f& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

BaseOuterSegment SegmentForImpactPoint(const Vec2f& point, const EnemyBase& base) {
    const float dx = point.x - base.position.x;
    const float dy = point.y - base.position.y;
    if (std::fabs(dx) >= std::fabs(dy)) {
        return dx >= 0.0F ? BaseOuterSegment::Right : BaseOuterSegment::Left;
    }
    return dy >= 0.0F ? BaseOuterSegment::Bottom : BaseOuterSegment::Top;
}

float BaseSideInsetUnitsFromDamage(int segmentHealth) {
    constexpr float kInsetStepUnits = 3.0F / static_cast<float>(GameplayConstants::kPixelsPerUnit);
    const int clampedHealth = std::clamp(segmentHealth, 0, GameplayConstants::kBaseOuterSegmentMaxHealth);
    const int damagePoints = GameplayConstants::kBaseOuterSegmentMaxHealth - clampedHealth;
    return static_cast<float>(damagePoints) * kInsetStepUnits;
}

bool IsPointInsideBaseFootprint(const Vec2f& point, const EnemyBase& base, float clearanceUnits) {
    const float halfBase = GameplayConstants::kEnemyBaseSizeUnits * 0.5F;
    const float leftInset = BaseSideInsetUnitsFromDamage(base.leftSegmentHealth);
    const float rightInset = BaseSideInsetUnitsFromDamage(base.rightSegmentHealth);
    const float topInset = BaseSideInsetUnitsFromDamage(base.topSegmentHealth);
    const float bottomInset = BaseSideInsetUnitsFromDamage(base.bottomSegmentHealth);
    const float minX = base.position.x - halfBase + leftInset - clearanceUnits;
    const float maxX = base.position.x + halfBase - rightInset + clearanceUnits;
    const float minY = base.position.y - halfBase + topInset - clearanceUnits;
    const float maxY = base.position.y + halfBase - bottomInset + clearanceUnits;
    return point.x >= minX && point.x <= maxX && point.y >= minY && point.y <= maxY;
}

bool IsPointInsideBaseCore(const Vec2f& point, const EnemyBase& base) {
    const float dx = point.x - base.position.x;
    const float dy = point.y - base.position.y;
    const float coreRadius = GameplayConstants::kEnemyBaseCoreRadiusUnits;
    return dx * dx + dy * dy <= coreRadius * coreRadius;
}

void DecrementOriginBaseAliveCount(WorldState& world, EnemyTank& enemy) {
    if (enemy.originBaseIndex < 0 || enemy.originBaseIndex >= static_cast<int>(world.enemyBases.size())) {
        enemy.originBaseIndex = -1;
        return;
    }
    EnemyBase& origin = world.enemyBases[static_cast<std::size_t>(enemy.originBaseIndex)];
    origin.activeEnemies = std::max(0, origin.activeEnemies - 1);
    enemy.originBaseIndex = -1;
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

                const BaseOuterSegment impactSegment = SegmentForImpactPoint(projectile.position, base);
                int& impactSegmentHealth = base.SegmentHealthRef(impactSegment);
                if (impactSegmentHealth > 0) {
                    impactSegmentHealth -= 1;
                    base.repairCountdownSeconds = GameplayConstants::kBaseRepairDelaySeconds;
                    projectile.alive = false;
                    break;
                }

                if (!IsPointInsideBaseCore(projectile.position, base)) {
                    break;
                }

                base.destroyed = true;
                anyBaseDestroyed = true;
                projectile.alive = false;
                world.score += state.menuSettings.levelNumber * GameplayConstants::kBaseScorePerLevelMultiplier;
                world.player.fuel = GameplayConstants::kFuelMax;
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
