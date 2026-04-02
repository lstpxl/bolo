#include "game/systems/EnemySystemHelpers.h"

#include <cmath>
#include <limits>
#include "core/AngleMath.h"
#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"

namespace {

constexpr float kEnemyPlanningClearanceScale = 1.5F;

}  // namespace

float DistanceSq(const Vec2f& a, const Vec2f& b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    return dx * dx + dy * dy;
}

float Distance(const Vec2f& a, const Vec2f& b) {
    return std::sqrt(DistanceSq(a, b));
}

float DistancePointToSegment(const Vec2f& p, const Vec2f& a, const Vec2f& b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float lenSq = dx * dx + dy * dy;
    if (lenSq <= 0.000001F) {
        return Distance(p, a);
    }
    float t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / lenSq;
    t = std::max(0.0F, std::min(1.0F, t));
    const Vec2f closest{.x = a.x + t * dx, .y = a.y + t * dy};
    return Distance(p, closest);
}

Vec2f NormalizeOrZero(const Vec2f& v) {
    const float lenSq = v.x * v.x + v.y * v.y;
    if (lenSq <= 0.000001F) {
        return Vec2f{.x = 0.0F, .y = 0.0F};
    }
    const float invLen = 1.0F / std::sqrt(lenSq);
    return Vec2f{.x = v.x * invLen, .y = v.y * invLen};
}

float NearestBaseDistance(const WorldState& world, const Vec2f& p) {
    float nearest = std::numeric_limits<float>::infinity();
    for (const EnemyBase& base : world.enemyBases) {
        if (base.destroyed) {
            continue;
        }
        nearest = std::min(nearest, Distance(base.position, p));
    }
    return nearest;
}

bool IsValidSegmentEndpoint(const WorldState& world, const Vec2f& point) {
    if (game::geometry::IsPointInWall(world, point, GameplayConstants::kWallClearanceForAvoidance)) {
        return false;
    }
    return !game::geometry::IsPointInUndestroyedBase(
        world, point, GameplayConstants::kWallClearanceForAvoidance);
}

Vec2f NearestBasePosition(const WorldState& world, const Vec2f& p) {
    Vec2f nearestPos = p;
    float nearest = std::numeric_limits<float>::infinity();
    for (const EnemyBase& base : world.enemyBases) {
        if (base.destroyed) {
            continue;
        }
        const float dist = Distance(base.position, p);
        if (dist < nearest) {
            nearest = dist;
            nearestPos = base.position;
        }
    }
    return nearestPos;
}

int RandomRotateDirection(Random& random) {
    return random.NextInt(0, 1) == 0 ? -1 : 1;
}

float ChooseBestTurnHeading(
    const WorldState& world,
    const Vec2f& origin,
    float currentHeading,
    const std::array<float, 4>& turnCandidates,
    int candidateCount,
    float requiredDistance) {
    float bestHeading = currentHeading;
    float bestDistance = -1.0F;
    for (int i = 0; i < candidateCount; ++i) {
        const float candidate = core::angle::QuantizeToEightDirections(
            currentHeading + turnCandidates[static_cast<std::size_t>(i)]);
        const float freeDist = game::geometry::FreeDistanceAhead(
            world,
            origin,
            candidate,
            requiredDistance + 2.0F,
            GameplayConstants::kWallClearanceForAvoidance,
            kEnemyPlanningClearanceScale);
        if (freeDist > bestDistance) {
            bestDistance = freeDist;
            bestHeading = candidate;
        }
    }
    if (bestDistance >= requiredDistance) {
        return bestHeading;
    }
    return std::numeric_limits<float>::quiet_NaN();
}

int EnemyTypeTelemetryIndex(EnemyType type) {
    switch (type) {
    case EnemyType::Drone:
        return 0;
    case EnemyType::Torpedo:
        return 1;
    case EnemyType::Hunter:
        return 2;
    case EnemyType::Assassin:
        return 3;
    }
    return 0;
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

void DecrementOriginBaseAliveCount(WorldState& world, EnemyTank& enemy) {
    if (enemy.originBaseIndex < 0 ||
        enemy.originBaseIndex >= static_cast<int>(world.enemyBases.size())) {
        enemy.originBaseIndex = -1;
        return;
    }
    EnemyBase& origin = world.enemyBases[static_cast<std::size_t>(enemy.originBaseIndex)];
    origin.activeEnemies = std::max(0, origin.activeEnemies - 1);
    enemy.originBaseIndex = -1;
}
