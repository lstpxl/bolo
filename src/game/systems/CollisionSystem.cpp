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
}  // namespace

void UpdateCollisionSystem(GameState& state, float deltaSeconds) {
    (void)deltaSeconds;
    WorldState& world = state.world;

    for (Projectile& projectile : world.projectiles) {
        if (!projectile.alive) {
            continue;
        }
        if (game::geometry::IsSegmentObscuredByWall(world, projectile.previousPosition, projectile.position)) {
            projectile.alive = false;
            continue;
        }

        if (projectile.owner == ProjectileOwner::Player) {
            for (EnemyTank& enemy : world.enemies) {
                if (!enemy.alive) {
                    continue;
                }
                if (enemy.simTier != EnemySimTier::Full) {
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
                    enemy.alive = false;
                    DecrementOriginBaseAliveCount(world, enemy);
                    projectile.alive = false;
                    world.score += state.menuSettings.levelNumber * GameplayConstants::kEnemyScorePerLevelMultiplier;
                    break;
                }
            }
            if (!projectile.alive) {
                continue;
            }

            for (EnemyBase& base : world.enemyBases) {
                if (base.destroyed) {
                    continue;
                }
                const float dx = std::fabs(projectile.position.x - base.position.x);
                const float dy = std::fabs(projectile.position.y - base.position.y);
                const float halfBase = GameplayConstants::kEnemyBaseSizeUnits * 0.5F;
                if (dx <= halfBase && dy <= halfBase) {
                    base.destroyed = true;
                    projectile.alive = false;
                    world.score += state.menuSettings.levelNumber * GameplayConstants::kBaseScorePerLevelMultiplier;
                    world.player.fuel = GameplayConstants::kFuelMax;
                    break;
                }
            }
        } else {
            if (world.player.alive &&
                DistanceSq(projectile.position, world.player.position) <=
                    GameplayConstants::kProjectileHitRadius * GameplayConstants::kProjectileHitRadius) {
                projectile.alive = false;
                world.player.alive = false;
                world.playerTurnLostPending = true;
            }
        }
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

    for (const EnemyTank& enemy : world.enemies) {
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
            return;
        }
    }

    for (const EnemyBase& base : world.enemyBases) {
        if (base.destroyed) {
            continue;
        }
        const float dx = std::fabs(world.player.position.x - base.position.x);
        const float dy = std::fabs(world.player.position.y - base.position.y);
        const float baseThreshold = GameplayConstants::kPlayerBaseHardCollisionUnits;
        if (dx <= baseThreshold && dy <= baseThreshold) {
            world.player.alive = false;
            world.playerTurnLostPending = true;
            return;
        }
    }
}
