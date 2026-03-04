#include "game/systems/CollisionSystem.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
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
}  // namespace

void UpdateCollisionSystem(GameState& state, float deltaSeconds) {
    (void)deltaSeconds;
    WorldState& world = state.world;

    for (Projectile& projectile : world.projectiles) {
        if (!projectile.alive) {
            continue;
        }
        if (game::geometry::SegmentIntersectsWall(world, projectile.previousPosition, projectile.position, 0.0F)) {
            projectile.alive = false;
            continue;
        }

        if (projectile.owner == ProjectileOwner::Player) {
            for (EnemyTank& enemy : world.enemies) {
                if (!enemy.alive) {
                    continue;
                }
                if (DistanceSq(projectile.position, enemy.position) <=
                    GameplayConstants::kProjectileHitRadius * GameplayConstants::kProjectileHitRadius) {
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

    if (!world.player.alive) {
        return;
    }

    if (game::geometry::IsPointInWall(world, world.player.position, GameplayConstants::kTankCollisionRadiusUnits)) {
        world.player.alive = false;
        world.playerTurnLostPending = true;
        return;
    }

    for (const EnemyTank& enemy : world.enemies) {
        if (!enemy.alive) {
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
        const float halfBase = GameplayConstants::kEnemyBaseSizeUnits * 0.5F;
        if (dx <= halfBase && dy <= halfBase) {
            world.player.alive = false;
            world.playerTurnLostPending = true;
            return;
        }
    }
}
