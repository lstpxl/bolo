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

float EnemySubtypeSpeedMultiplier(EnemyType type, EnemySubtype subtype) {
    if (subtype == EnemySubtype::Basic) {
        return 0.75F;
    }
    if (subtype == EnemySubtype::Lord && type == EnemyType::Hunter) {
        return 1.25F;
    }
    return 1.0F;
}

float EnemySpeed(EnemyType type, EnemySubtype subtype, bool assassinSeesPlayer) {
    float baseSpeed = GameplayConstants::kEnemyDroneSpeed;
    if (type == EnemyType::Drone) {
        baseSpeed = GameplayConstants::kEnemyDroneSpeed;
    } else if (type == EnemyType::Torpedo) {
        baseSpeed = GameplayConstants::kEnemyTorpedoSpeed;
    } else if (type == EnemyType::Hunter) {
        baseSpeed = GameplayConstants::kEnemyHunterSpeed;
    } else {
        baseSpeed = assassinSeesPlayer ? 6.0F : 3.0F;
    }
    return baseSpeed * EnemySubtypeSpeedMultiplier(type, subtype);
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

bool IsPointInsideMaze(const WorldState& world, const Vec2f& p, float clearanceUnits) {
    const float mazeWidthUnits = static_cast<float>(world.maze.widthCells * world.maze.cellSizeUnits);
    const float mazeHeightUnits = static_cast<float>(world.maze.heightCells * world.maze.cellSizeUnits);
    return p.x >= clearanceUnits && p.x <= mazeWidthUnits - clearanceUnits &&
        p.y >= clearanceUnits && p.y <= mazeHeightUnits - clearanceUnits;
}

bool IsPointInWall(const WorldState& world, const Vec2f& point, float clearanceUnits) {
    if (!IsPointInsideMaze(world, point, clearanceUnits)) {
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
    const float wallLimit = GameplayConstants::kWallThicknessUnits + clearanceUnits;
    return (cell.northWall && localY <= wallLimit) || (cell.southWall && localY >= cellSize - wallLimit) ||
        (cell.westWall && localX <= wallLimit) || (cell.eastWall && localX >= cellSize - wallLimit);
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
        if (IsPointInWall(world, sample, 0.0F)) {
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

bool SegmentIntersectsWall(const WorldState& world, const Vec2f& from, const Vec2f& to, float clearanceUnits) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= 0.001F) {
        return IsPointInWall(world, to, clearanceUnits);
    }
    const float sampleSpacing = std::max(0.02F, clearanceUnits * 0.5F);
    const int steps = std::max(1, static_cast<int>(std::ceil(distance / sampleSpacing)));
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const Vec2f sample{
            .x = from.x + dx * t,
            .y = from.y + dy * t,
        };
        if (IsPointInWall(world, sample, clearanceUnits)) {
            return true;
        }
    }
    return false;
}
}  // namespace

void UpdateEnemySystem(GameState& state, const AppConfig& config, float deltaSeconds) {
    static Random random(static_cast<std::uint32_t>(GetTime() * 1000.0));
    for (EnemyTank& enemy : state.world.enemies) {
        if (!enemy.alive) {
            continue;
        }
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
        const bool playerObscured = IsSegmentObscuredByWall(state.world, enemy.position, state.world.player.position);
        const bool assassinSeesPlayer =
            enemy.type == EnemyType::Assassin && playerInAggroRange && !playerObscured;
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
        const float speed = EnemySpeed(enemy.type, enemy.subtype, assassinSeesPlayer);
        enemy.velocity.x = snappedDirection.x * speed;
        enemy.velocity.y = snappedDirection.y * speed;
        const Vec2f previousPosition = enemy.position;
        const Vec2f candidatePosition{
            .x = enemy.position.x + enemy.velocity.x * deltaSeconds,
            .y = enemy.position.y + enemy.velocity.y * deltaSeconds,
        };
        if (SegmentIntersectsWall(
                state.world,
                previousPosition,
                candidatePosition,
                GameplayConstants::kEnemyWallAvoidanceRadiusUnits)) {
            enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
            enemy.aiStateTimerSeconds = 0.0F;
        } else {
            enemy.position = candidatePosition;
            enemy.headingRadians = movementHeading;
        }

        enemy.fireCooldownSeconds -= deltaSeconds;
        const bool enemyVisibleInViewport = IsInPlayerViewport(enemy.position, state, config);
        if (enemy.fireCooldownSeconds <= 0.0F &&
            enemyVisibleInViewport &&
            !playerObscured &&
            distanceToPlayerSq < (GameplayConstants::kEnemyFireRangeUnits * GameplayConstants::kEnemyFireRangeUnits)) {
            const float headingToPlayer = std::atan2(toPlayer.x, -toPlayer.y);
            const float quantizedHeadingToPlayer = QuantizeToEightDirections(headingToPlayer);
            SpawnProjectile(
                state,
                ProjectileOwner::Enemy,
                enemy.position,
                quantizedHeadingToPlayer,
                GameplayConstants::kEnemyProjectileSpeed);
            enemy.fireCooldownSeconds = EnemyFireInterval(enemy.type);
        }
    }
}
