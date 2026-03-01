#include "game/systems/EnemySystem.h"

#include <cstdint>
#include <cmath>
#include <algorithm>
#include "core/Random.h"
#include "game/systems/ProjectileSystem.h"
#include "raylib.h"

namespace {
float NormalizeAngle(float angleRadians) {
    constexpr float kPi = 3.14159265358979323846F;
    const float twoPi = kPi * 2.0F;
    float normalized = std::fmod(angleRadians, twoPi);
    if (normalized < 0.0F) {
        normalized += twoPi;
    }
    return normalized;
}

float QuantizeToEightDirections(float angleRadians) {
    constexpr float kPi = 3.14159265358979323846F;
    constexpr float kStep = kPi / 4.0F;
    const float normalized = NormalizeAngle(angleRadians);
    const int stepIndex = static_cast<int>(std::round(normalized / kStep));
    return NormalizeAngle(static_cast<float>(stepIndex) * kStep);
}

Vec2f DirectionFromHeading(float headingRadians) {
    return Vec2f{
        .x = std::sin(headingRadians),
        .y = -std::cos(headingRadians),
    };
}

EnemyType EnemyTypeForLevel(int level) {
    if (level <= GameplayConstants::kDroneMaxLevel) {
        return EnemyType::Drone;
    }
    if (level <= GameplayConstants::kTorpedoMaxLevel) {
        return EnemyType::Torpedo;
    }
    if (level <= GameplayConstants::kHunterMaxLevel) {
        return EnemyType::Hunter;
    }
    return EnemyType::Assassin;
}

float EnemySpeed(EnemyType type) {
    if (type == EnemyType::Drone) {
        return GameplayConstants::kEnemyDroneSpeed;
    }
    if (type == EnemyType::Torpedo) {
        return GameplayConstants::kEnemyTorpedoSpeed;
    }
    if (type == EnemyType::Hunter) {
        return GameplayConstants::kEnemyHunterSpeed;
    }
    return GameplayConstants::kEnemyAssassinSpeed;
}

float EnemyFireInterval(EnemyType type) {
    if (type == EnemyType::Drone) {
        return GameplayConstants::kEnemyDroneFireInterval;
    }
    if (type == EnemyType::Torpedo) {
        return GameplayConstants::kEnemyTorpedoFireInterval;
    }
    if (type == EnemyType::Hunter) {
        return GameplayConstants::kEnemyHunterFireInterval;
    }
    return GameplayConstants::kEnemyAssassinFireInterval;
}

float DistanceSq(const Vec2f& a, const Vec2f& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

bool IsPointInsideMaze(const WorldState& world, const Vec2f& p) {
    const float mazeWidthUnits = static_cast<float>(world.maze.widthCells * world.maze.cellSizeUnits);
    const float mazeHeightUnits = static_cast<float>(world.maze.heightCells * world.maze.cellSizeUnits);
    return p.x >= 0.0F && p.x <= mazeWidthUnits && p.y >= 0.0F && p.y <= mazeHeightUnits;
}

bool IsPointInWall(const WorldState& world, const Vec2f& point) {
    if (!IsPointInsideMaze(world, point)) {
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
    return (cell.northWall && localY <= t) || (cell.southWall && localY >= cellSize - t) ||
        (cell.westWall && localX <= t) || (cell.eastWall && localX >= cellSize - t);
}

bool IsSegmentObscuredByWall(const WorldState& world, const Vec2f& from, const Vec2f& to) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= 0.001F) {
        return false;
    }
    const float sampleSpacing = GameplayConstants::kLineOfSightSampleSpacing;
    const int steps = std::max(1, static_cast<int>(std::ceil(distance / sampleSpacing)));
    for (int i = 1; i < steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const Vec2f sample{
            .x = from.x + dx * t,
            .y = from.y + dy * t,
        };
        if (IsPointInWall(world, sample)) {
            return true;
        }
    }
    return false;
}

bool IsInPlayerViewport(const Vec2f& point, const GameState& state, const AppConfig& config) {
    const float viewportWidthUnits = static_cast<float>(config.screenWidth - ComputeHudWidth(config)) /
        static_cast<float>(GameplayConstants::kPixelsPerUnit);
    const float viewportHeightUnits = static_cast<float>(config.screenHeight) /
        static_cast<float>(GameplayConstants::kPixelsPerUnit);
    const float halfWidth = viewportWidthUnits * 0.5F;
    const float halfHeight = viewportHeightUnits * 0.5F;
    const Vec2f center = state.world.player.position;
    return point.x >= center.x - halfWidth && point.x <= center.x + halfWidth &&
        point.y >= center.y - halfHeight && point.y <= center.y + halfHeight;
}

bool SegmentIntersectsWall(const WorldState& world, const Vec2f& from, const Vec2f& to) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= 0.001F) {
        return IsPointInWall(world, to);
    }
    const float sampleSpacing = GameplayConstants::kWallThicknessUnits * 0.5F;
    const int steps = std::max(1, static_cast<int>(std::ceil(distance / sampleSpacing)));
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const Vec2f sample{
            .x = from.x + dx * t,
            .y = from.y + dy * t,
        };
        if (IsPointInWall(world, sample)) {
            return true;
        }
    }
    return false;
}
}  // namespace

void UpdateEnemySystem(GameState& state, const AppConfig& config, float deltaSeconds) {
    static Random random(static_cast<std::uint32_t>(GetTime() * 1000.0));
    const EnemyType levelType = EnemyTypeForLevel(state.menuSettings.levelNumber);
    for (EnemyTank& enemy : state.world.enemies) {
        if (!enemy.alive) {
            continue;
        }
        enemy.type = levelType;
        enemy.aiStateTimerSeconds -= deltaSeconds;
        if (enemy.aiStateTimerSeconds <= 0.0F) {
            enemy.aiStateTimerSeconds =
                GameplayConstants::kEnemyAiRetargetMinSeconds +
                random.NextFloat(0.0F, GameplayConstants::kEnemyAiRetargetRandomSeconds);
            const float angle = random.NextFloat(0.0F, PI * 2.0F);
            enemy.wanderDirection = Vec2f{.x = std::sin(angle), .y = -std::cos(angle)};
        }

        Vec2f desiredDirection = enemy.wanderDirection;
        const Vec2f toPlayer{
            .x = state.world.player.position.x - enemy.position.x,
            .y = state.world.player.position.y - enemy.position.y,
        };
        const float distanceToPlayerSq = DistanceSq(enemy.position, state.world.player.position);
        const bool playerInAggroRange =
            distanceToPlayerSq < (GameplayConstants::kEnemyAggroRangeUnits * GameplayConstants::kEnemyAggroRangeUnits);
        if (enemy.type == EnemyType::Torpedo && playerInAggroRange) {
            desiredDirection = toPlayer;
        } else if (enemy.type == EnemyType::Hunter) {
            desiredDirection = toPlayer;
        } else if (enemy.type == EnemyType::Assassin) {
            const Vec2f predicted{
                .x = state.world.player.position.x +
                    state.world.player.velocity.x * GameplayConstants::kEnemyAssassinPredictionSeconds,
                .y = state.world.player.position.y +
                    state.world.player.velocity.y * GameplayConstants::kEnemyAssassinPredictionSeconds,
            };
            desiredDirection = Vec2f{
                .x = predicted.x - enemy.position.x,
                .y = predicted.y - enemy.position.y,
            };
        }

        const float desiredLength =
            std::sqrt(desiredDirection.x * desiredDirection.x + desiredDirection.y * desiredDirection.y);
        if (desiredLength > 0.001F) {
            desiredDirection.x /= desiredLength;
            desiredDirection.y /= desiredLength;
        } else {
            desiredDirection = Vec2f{.x = 0.0F, .y = 0.0F};
        }

        float movementHeading = enemy.headingRadians;
        if (desiredLength > 0.001F) {
            movementHeading = std::atan2(desiredDirection.x, -desiredDirection.y);
        }
        movementHeading = QuantizeToEightDirections(movementHeading);
        const Vec2f snappedDirection = DirectionFromHeading(movementHeading);
        const float speed = EnemySpeed(enemy.type);
        enemy.velocity.x = snappedDirection.x * speed;
        enemy.velocity.y = snappedDirection.y * speed;
        const Vec2f previousPosition = enemy.position;
        const Vec2f candidatePosition{
            .x = enemy.position.x + enemy.velocity.x * deltaSeconds,
            .y = enemy.position.y + enemy.velocity.y * deltaSeconds,
        };
        if (SegmentIntersectsWall(state.world, previousPosition, candidatePosition)) {
            enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
            enemy.aiStateTimerSeconds = 0.0F;
        } else {
            enemy.position = candidatePosition;
            enemy.headingRadians = movementHeading;
        }

        enemy.fireCooldownSeconds -= deltaSeconds;
        const bool enemyVisibleInViewport = IsInPlayerViewport(enemy.position, state, config);
        const bool playerObscured = IsSegmentObscuredByWall(state.world, enemy.position, state.world.player.position);
        if (enemy.fireCooldownSeconds <= 0.0F &&
            enemyVisibleInViewport &&
            !playerObscured &&
            distanceToPlayerSq < (GameplayConstants::kEnemyFireRangeUnits * GameplayConstants::kEnemyFireRangeUnits)) {
            const float headingToPlayer = std::atan2(toPlayer.x, -toPlayer.y);
            SpawnProjectile(
                state,
                ProjectileOwner::Enemy,
                enemy.position,
                headingToPlayer,
                GameplayConstants::kEnemyProjectileSpeed);
            enemy.fireCooldownSeconds = EnemyFireInterval(enemy.type);
        }
    }
}
