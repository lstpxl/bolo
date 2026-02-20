#include "game/systems/CollisionSystem.h"

#include <cstddef>
#include <cmath>

namespace {
float DistanceSq(const Vec2f& a, const Vec2f& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

bool IsInsideMaze(const WorldState& world, const Vec2f& p) {
    const float mazeWidthUnits = static_cast<float>(world.maze.widthCells * world.maze.cellSizeUnits);
    const float mazeHeightUnits = static_cast<float>(world.maze.heightCells * world.maze.cellSizeUnits);
    return p.x >= 0.0F && p.x <= mazeWidthUnits && p.y >= 0.0F && p.y <= mazeHeightUnits;
}

bool HitsWallAtPoint(const WorldState& world, const Vec2f& point) {
    if (!IsInsideMaze(world, point)) {
        return true;
    }
    const float cellSize = static_cast<float>(world.maze.cellSizeUnits);
    const int cellX = static_cast<int>(point.x / cellSize);
    const int cellY = static_cast<int>(point.y / cellSize);
    if (cellX < 0 || cellY < 0 || cellX >= world.maze.widthCells || cellY >= world.maze.heightCells) {
        return true;
    }
    const MazeCell& cell =
        world.maze.cells[static_cast<std::size_t>(cellY * world.maze.widthCells + cellX)];
    const float localX = point.x - static_cast<float>(cellX) * cellSize;
    const float localY = point.y - static_cast<float>(cellY) * cellSize;
    const float t = GameplayConstants::kWallThicknessUnits;
    if (cell.northWall && localY <= t) {
        return true;
    }
    if (cell.southWall && localY >= cellSize - t) {
        return true;
    }
    if (cell.westWall && localX <= t) {
        return true;
    }
    if (cell.eastWall && localX >= cellSize - t) {
        return true;
    }
    return false;
}

bool SegmentHitsWall(const WorldState& world, const Vec2f& a, const Vec2f& b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= 0.001F) {
        return HitsWallAtPoint(world, b);
    }
    const float sampleSpacing = GameplayConstants::kWallThicknessUnits * 0.5F;
    const int steps = std::max(1, static_cast<int>(std::ceil(distance / sampleSpacing)));
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const Vec2f sample{
            .x = a.x + dx * t,
            .y = a.y + dy * t,
        };
        if (HitsWallAtPoint(world, sample)) {
            return true;
        }
    }
    return false;
}
}  // namespace

void UpdateCollisionSystem(GameState& state, float deltaSeconds) {
    (void)deltaSeconds;
    WorldState& world = state.world;

    for (Projectile& projectile : world.projectiles) {
        if (!projectile.alive) {
            continue;
        }
        if (SegmentHitsWall(world, projectile.previousPosition, projectile.position)) {
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

    if (HitsWallAtPoint(world, world.player.position)) {
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
