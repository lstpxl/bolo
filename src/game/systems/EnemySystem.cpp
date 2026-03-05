#include "game/systems/EnemySystem.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
#include "core/AngleMath.h"
#include "core/Profiling.h"
#include "core/Random.h"
#include "game/geometry/WorldGeometry.h"
#include "game/spatial/EnemySpatialGrid.h"
#include "game/systems/ProjectileSystem.h"

namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kEightDirectionStep = kPi / 4.0F;
constexpr float kCosThirtyDegrees = 0.8660254F;
constexpr float kDroneBaseBearingThresholdRadians = 1.3962634F;  // 80 degrees
constexpr float kDroneReturnRequiredClearRunUnits = 6.0F;
constexpr float kEnemyPlanningClearanceScale = 1.5F;
constexpr float kTorpedoNearCollisionCheckDistanceUnits = 3.0F;
constexpr float kTorpedoMoveDecisionHoldDistanceUnits = 1.0F;
constexpr float kTorpedoRetreatExitClearanceUnits = 2.0F;
constexpr float kTorpedoRetreatSpeedFactor = 0.1F;
constexpr float kTorpedoImmediateObstacleDistanceUnits = 1.0F;
constexpr float kTorpedoLongPathProbeUnits = 24.0F;
constexpr float kTorpedoPlayerDetectIntervalSeconds = 0.25F;
constexpr float kSegmentBuildProbeMaxUnits = 15.0F;
constexpr float kSegmentBuildSafetyReduceUnits = 4.0F;
constexpr float kSegmentBuildMinLengthUnits = 2.0F;
constexpr float kOffscreenSegmentLengthUnits = 8.0F;
constexpr float kOffscreenTorpedoSegmentLengthUnits = 12.0F;
constexpr float kOffscreenTorpedoDetectIntervalSeconds = 0.6F;

EnemyRuntimeStats gEnemyRuntimeStats{};
std::uint64_t gLastEnemyStatsPrintedFrame = 0;
struct EnemyRuntimeWindowStats {
    int minAliveCount = std::numeric_limits<int>::max();
    int maxAliveCount = 0;
    int minVisibleCount = std::numeric_limits<int>::max();
    int maxVisibleCount = 0;
    int minFullTierCount = std::numeric_limits<int>::max();
    int maxFullTierCount = 0;
    std::uint64_t fixedSteps = 0;
    std::uint64_t collisionPassRuns = 0;
    std::uint64_t collisionPassSkips = 0;
};
EnemyRuntimeWindowStats gEnemyRuntimeWindowStats{};

void AccumulateEnemyWindowStats(int aliveCount, int visibleCount, int fullTierCount) {
    EnemyRuntimeWindowStats& stats = gEnemyRuntimeWindowStats;
    stats.fixedSteps += 1;
    stats.minAliveCount = std::min(stats.minAliveCount, aliveCount);
    stats.maxAliveCount = std::max(stats.maxAliveCount, aliveCount);
    stats.minVisibleCount = std::min(stats.minVisibleCount, visibleCount);
    stats.maxVisibleCount = std::max(stats.maxVisibleCount, visibleCount);
    stats.minFullTierCount = std::min(stats.minFullTierCount, fullTierCount);
    stats.maxFullTierCount = std::max(stats.maxFullTierCount, fullTierCount);
}

void ResetEnemyWindowStats() {
    gEnemyRuntimeWindowStats = EnemyRuntimeWindowStats{};
}

float NormalizeAngle(float angleRadians) {
    return core::angle::NormalizeAngle(angleRadians);
}

float AngleDistance(float aRadians, float bRadians) {
    return core::angle::AngleDistance(aRadians, bRadians);
}

float SignedAngleDelta(float fromRadians, float toRadians) {
    return core::angle::SignedAngleDelta(fromRadians, toRadians);
}

float QuantizeToEightDirections(float angleRadians) {
    return core::angle::QuantizeToEightDirections(angleRadians);
}

Vec2f DirectionFromHeading(float headingRadians) {
    return core::angle::DirectionFromHeading(headingRadians);
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

float EnemySpeed(EnemyType type, EnemySubtype subtype, bool assassinHasLineOfSight) {
    float baseSpeed = GameplayConstants::kEnemyDroneSpeed;
    if (type == EnemyType::Drone) {
        baseSpeed = GameplayConstants::kEnemyDroneSpeed;
    } else if (type == EnemyType::Torpedo) {
        baseSpeed = GameplayConstants::kEnemyTorpedoSpeed;
    } else if (type == EnemyType::Hunter) {
        baseSpeed = GameplayConstants::kEnemyHunterSpeed;
    } else {
        baseSpeed = assassinHasLineOfSight ? 6.0F : 3.0F;
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

float Distance(const Vec2f& a, const Vec2f& b) {
    return std::sqrt(DistanceSq(a, b));
}

float DistancePointToSegment(const Vec2f& p, const Vec2f& a, const Vec2f& b) {
    const float abX = b.x - a.x;
    const float abY = b.y - a.y;
    const float abLenSq = abX * abX + abY * abY;
    if (abLenSq <= 0.000001F) {
        return Distance(p, a);
    }
    const float apX = p.x - a.x;
    const float apY = p.y - a.y;
    const float t = std::max(0.0F, std::min(1.0F, (apX * abX + apY * abY) / abLenSq));
    const Vec2f closest{
        .x = a.x + abX * t,
        .y = a.y + abY * t,
    };
    return Distance(p, closest);
}

float Cross2D(const Vec2f& a, const Vec2f& b, const Vec2f& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool IsPointOnSegment(const Vec2f& p, const Vec2f& a, const Vec2f& b) {
    constexpr float kEps = 0.0001F;
    return p.x >= std::min(a.x, b.x) - kEps && p.x <= std::max(a.x, b.x) + kEps &&
        p.y >= std::min(a.y, b.y) - kEps && p.y <= std::max(a.y, b.y) + kEps;
}

bool SegmentsIntersect(const Vec2f& a1, const Vec2f& a2, const Vec2f& b1, const Vec2f& b2) {
    const float d1 = Cross2D(a1, a2, b1);
    const float d2 = Cross2D(a1, a2, b2);
    const float d3 = Cross2D(b1, b2, a1);
    const float d4 = Cross2D(b1, b2, a2);
    constexpr float kEps = 0.0001F;

    if (((d1 > kEps && d2 < -kEps) || (d1 < -kEps && d2 > kEps)) &&
        ((d3 > kEps && d4 < -kEps) || (d3 < -kEps && d4 > kEps))) {
        return true;
    }
    if (std::fabs(d1) <= kEps && IsPointOnSegment(b1, a1, a2)) {
        return true;
    }
    if (std::fabs(d2) <= kEps && IsPointOnSegment(b2, a1, a2)) {
        return true;
    }
    if (std::fabs(d3) <= kEps && IsPointOnSegment(a1, b1, b2)) {
        return true;
    }
    if (std::fabs(d4) <= kEps && IsPointOnSegment(a2, b1, b2)) {
        return true;
    }
    return false;
}

float SegmentToSegmentDistance(const Vec2f& a1, const Vec2f& a2, const Vec2f& b1, const Vec2f& b2) {
    if (SegmentsIntersect(a1, a2, b1, b2)) {
        return 0.0F;
    }
    return std::min(
        std::min(DistancePointToSegment(a1, b1, b2), DistancePointToSegment(a2, b1, b2)),
        std::min(DistancePointToSegment(b1, a1, a2), DistancePointToSegment(b2, a1, a2)));
}

int RandomRotateDirection(Random& random) {
    return random.NextInt(0, 1) == 0 ? -1 : 1;
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

void EnterDroneWatchMode(WorldState& world, EnemyTank& enemy, Random& random) {
    enemy.aiMode = EnemyAiMode::Watch;
    enemy.aiModeElapsedSeconds = 0.0F;
    enemy.watchRotateDirection = RandomRotateDirection(random);
    const float nearestBaseDist = NearestBaseDistance(world, enemy.position);
    enemy.returnToBase = nearestBaseDist >= 36.0F;
}

Vec2f NormalizeOrZero(const Vec2f& v) {
    const float lenSq = v.x * v.x + v.y * v.y;
    if (lenSq <= 0.000001F) {
        return Vec2f{.x = 0.0F, .y = 0.0F};
    }
    const float invLen = 1.0F / std::sqrt(lenSq);
    return Vec2f{.x = v.x * invLen, .y = v.y * invLen};
}

bool IsPointInUndestroyedBase(const WorldState& world, const Vec2f& point, float clearanceUnits) {
    return game::geometry::IsPointInUndestroyedBase(world, point, clearanceUnits);
}

bool IsInPlayerViewport(const Vec2f& point, const GameState& state, const GameplayView& view) {
    const float halfWidth = view.viewportWidthUnits * 0.5F;
    const float halfHeight = view.viewportHeightUnits * 0.5F;
    const Vec2f center = state.world.player.position;
    return point.x >= center.x - halfWidth && point.x <= center.x + halfWidth &&
        point.y >= center.y - halfHeight && point.y <= center.y + halfHeight;
}

EnemySimTier DetermineEnemySimTier(const EnemyTank& enemy, const GameState& state, const GameplayView& view) {
    const float cellWidthUnits = static_cast<float>(state.world.maze.cellSizeUnits);
    const float fullTierRadiusUnits =
        (view.viewportWidthUnits * 0.5F + cellWidthUnits) * 1.5F;
    const float fullTierRadiusSq = fullTierRadiusUnits * fullTierRadiusUnits;
    const bool nearPlayer = DistanceSq(enemy.position, state.world.player.position) <= fullTierRadiusSq;
    const bool forceFullForTorpedoState =
        enemy.type == EnemyType::Torpedo &&
        enemy.torpedoMoveMode != TorpedoMoveMode::Move;
    return (nearPlayer || forceFullForTorpedoState)
        ? EnemySimTier::Full
        : EnemySimTier::Cheap;
}

bool SegmentIntersectsWall(const WorldState& world, const Vec2f& from, const Vec2f& to, float clearanceUnits) {
    return game::geometry::SegmentIntersectsWall(world, from, to, clearanceUnits);
}

float FreeDistanceAheadWallsOnly(
    const WorldState& world,
    const Vec2f& from,
    float headingRadians,
    float maxDistance,
    float clearanceUnits) {
    const Vec2f dir = DirectionFromHeading(headingRadians);
    const float sampleStep = std::max(0.02F, GameplayConstants::kLineOfSightSampleSpacing);
    float dist = sampleStep;
    while (dist <= maxDistance) {
        const float clampedDist = std::min(dist, maxDistance);
        const Vec2f sample{
            .x = from.x + dir.x * clampedDist,
            .y = from.y + dir.y * clampedDist,
        };
        if (game::geometry::IsPointInWall(world, sample, clearanceUnits)) {
            return clampedDist;
        }
        if (clampedDist >= maxDistance) {
            break;
        }
        dist += sampleStep;
    }
    return maxDistance;
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
        const float candidate = QuantizeToEightDirections(currentHeading + turnCandidates[static_cast<std::size_t>(i)]);
        const float freeDist = game::geometry::FreeDistanceAhead(
            world,
            origin,
            candidate,
            requiredDistance + 2.0F,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
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

int ClampCellX(const WorldState& world, float x) {
    const int maxX = world.maze.widthCells - 1;
    const int cell = static_cast<int>(x / static_cast<float>(world.maze.cellSizeUnits));
    return std::max(0, std::min(maxX, cell));
}

int ClampCellY(const WorldState& world, float y) {
    const int maxY = world.maze.heightCells - 1;
    const int cell = static_cast<int>(y / static_cast<float>(world.maze.cellSizeUnits));
    return std::max(0, std::min(maxY, cell));
}

int CellIndex(const WorldState& world, int x, int y) {
    return y * world.maze.widthCells + x;
}

Vec2f CellCenter(const WorldState& world, int x, int y) {
    const float cellSize = static_cast<float>(world.maze.cellSizeUnits);
    return Vec2f{
        .x = (static_cast<float>(x) + 0.5F) * cellSize,
        .y = (static_cast<float>(y) + 0.5F) * cellSize,
    };
}

bool CanStepToNeighbor(const WorldState& world, int x, int y, int nx, int ny) {
    if (nx < 0 || ny < 0 || nx >= world.maze.widthCells || ny >= world.maze.heightCells) {
        return false;
    }
    const bool startInsideBase =
        IsPointInUndestroyedBase(world, CellCenter(world, x, y), GameplayConstants::kTankCollisionRadiusUnits);
    if (!startInsideBase &&
        IsPointInUndestroyedBase(world, CellCenter(world, nx, ny), GameplayConstants::kTankCollisionRadiusUnits)) {
        return false;
    }
    const MazeCell& cell = world.maze.cells[static_cast<std::size_t>(CellIndex(world, x, y))];
    if (nx == x + 1) {
        return !cell.eastWall;
    }
    if (nx == x - 1) {
        return !cell.westWall;
    }
    if (ny == y + 1) {
        return !cell.southWall;
    }
    if (ny == y - 1) {
        return !cell.northWall;
    }
    return false;
}

struct AStarNode {
    int parent = -1;
    float g = std::numeric_limits<float>::infinity();
    float f = std::numeric_limits<float>::infinity();
    bool open = false;
    bool closed = false;
};

struct OpenNode {
    int index = 0;
    float f = 0.0F;
};

struct OpenNodeCompare {
    bool operator()(const OpenNode& a, const OpenNode& b) const {
        return a.f > b.f;
    }
};

struct PathfindingPool {
    std::vector<bool> occupied{};
    std::vector<AStarNode> nodes{};
    std::vector<OpenNode> openHeap{};
    std::vector<int> pathCells{};

    void EnsureCapacity(int totalCells) {
        const auto n = static_cast<std::size_t>(totalCells);
        if (occupied.size() < n) {
            occupied.resize(n);
        }
        if (nodes.size() < n) {
            nodes.resize(n);
        }
        if (openHeap.capacity() < static_cast<std::size_t>(std::min(1024, totalCells))) {
            openHeap.reserve(static_cast<std::size_t>(std::min(1024, totalCells)));
        }
        if (pathCells.capacity() < static_cast<std::size_t>(std::min(512, totalCells))) {
            pathCells.reserve(static_cast<std::size_t>(std::min(512, totalCells)));
        }
    }
};

PathfindingPool& GetPathfindingPool() {
    static PathfindingPool pool;
    return pool;
}

float HeuristicManhattan(int x, int y, int tx, int ty) {
    return static_cast<float>(std::abs(tx - x) + std::abs(ty - y));
}

void BuildEnemyOccupancy(const GameState& state, int ignoreEnemyIndex, std::vector<bool>& occupied) {
    const int totalCells = state.world.maze.widthCells * state.world.maze.heightCells;
    occupied.assign(static_cast<std::size_t>(totalCells), false);
    for (int i = 0; i < static_cast<int>(state.world.enemies.size()); ++i) {
        if (i == ignoreEnemyIndex) {
            continue;
        }
        const EnemyTank& enemy = state.world.enemies[static_cast<std::size_t>(i)];
        if (!enemy.alive) {
            continue;
        }
        const int cx = ClampCellX(state.world, enemy.position.x);
        const int cy = ClampCellY(state.world, enemy.position.y);
        occupied[static_cast<std::size_t>(CellIndex(state.world, cx, cy))] = true;
    }
}

bool BuildAssassinPath(GameState& state, EnemyTank& enemy, int enemyIndex) {
    profiling::ScopedProfile totalScope(profiling::Scope::PathfindingTotal, true);
    const int width = state.world.maze.widthCells;
    const int height = state.world.maze.heightCells;
    const int totalCells = width * height;
    if (totalCells <= 0) {
        return false;
    }

    const int startX = ClampCellX(state.world, enemy.position.x);
    const int startY = ClampCellY(state.world, enemy.position.y);
    const int goalX = ClampCellX(state.world, state.world.player.position.x);
    const int goalY = ClampCellY(state.world, state.world.player.position.y);
    const int startIndex = CellIndex(state.world, startX, startY);
    const int goalIndex = CellIndex(state.world, goalX, goalY);
    if (startIndex == goalIndex) {
        enemy.pathWaypointCount = 0;
        enemy.pathWaypointIndex = 0;
        return false;
    }

    PathfindingPool& pool = GetPathfindingPool();
    pool.EnsureCapacity(totalCells);

    std::vector<bool>& occupied = pool.occupied;
    std::vector<AStarNode>& nodes = pool.nodes;
    std::vector<OpenNode>& openHeap = pool.openHeap;
    OpenNodeCompare cmp{};

    {
        profiling::ScopedProfile occupancyScope(profiling::Scope::PathfindingOccupancy, true);
        BuildEnemyOccupancy(state, enemyIndex, occupied);
    }
    occupied[static_cast<std::size_t>(startIndex)] = false;
    occupied[static_cast<std::size_t>(goalIndex)] = false;

    const std::size_t n = static_cast<std::size_t>(totalCells);
    for (std::size_t i = 0; i < n; ++i) {
        nodes[i] = AStarNode{};
    }
    nodes[static_cast<std::size_t>(startIndex)].g = 0.0F;
    nodes[static_cast<std::size_t>(startIndex)].f = HeuristicManhattan(startX, startY, goalX, goalY);
    nodes[static_cast<std::size_t>(startIndex)].open = true;
    openHeap.clear();
    openHeap.push_back(OpenNode{.index = startIndex, .f = nodes[static_cast<std::size_t>(startIndex)].f});
    std::push_heap(openHeap.begin(), openHeap.end(), cmp);

    const std::array<int, 4> dx{1, -1, 0, 0};
    const std::array<int, 4> dy{0, 0, 1, -1};
    bool found = false;
    {
        profiling::ScopedProfile searchScope(profiling::Scope::PathfindingSearch, true);
        while (!openHeap.empty()) {
            std::pop_heap(openHeap.begin(), openHeap.end(), cmp);
            const OpenNode top = openHeap.back();
            openHeap.pop_back();
            AStarNode& current = nodes[static_cast<std::size_t>(top.index)];
            if (current.closed) {
                continue;
            }
            current.closed = true;
            if (top.index == goalIndex) {
                found = true;
                break;
            }

            const int x = top.index % width;
            const int y = top.index / width;
            for (int i = 0; i < 4; ++i) {
                const int nx = x + dx[static_cast<std::size_t>(i)];
                const int ny = y + dy[static_cast<std::size_t>(i)];
                if (!CanStepToNeighbor(state.world, x, y, nx, ny)) {
                    continue;
                }
                const int ni = CellIndex(state.world, nx, ny);
                if (occupied[static_cast<std::size_t>(ni)] && ni != goalIndex) {
                    continue;
                }

                AStarNode& neighbor = nodes[static_cast<std::size_t>(ni)];
                if (neighbor.closed) {
                    continue;
                }
                const float tentativeG = current.g + 1.0F;
                if (tentativeG >= neighbor.g) {
                    continue;
                }
                neighbor.parent = top.index;
                neighbor.g = tentativeG;
                neighbor.f = tentativeG + HeuristicManhattan(nx, ny, goalX, goalY);
                neighbor.open = true;
                openHeap.push_back(OpenNode{.index = ni, .f = neighbor.f});
                std::push_heap(openHeap.begin(), openHeap.end(), cmp);
            }
        }
    }

    if (!found) {
        enemy.pathWaypointCount = 0;
        enemy.pathWaypointIndex = 0;
        return false;
    }

    {
        profiling::ScopedProfile postprocessScope(profiling::Scope::PathfindingPostprocess, true);
        std::vector<int>& pathCells = pool.pathCells;
        pathCells.clear();
        int trace = goalIndex;
        while (trace != -1) {
            pathCells.push_back(trace);
            if (trace == startIndex) {
                break;
            }
            trace = nodes[static_cast<std::size_t>(trace)].parent;
        }
        if (pathCells.empty() || pathCells.back() != startIndex) {
            enemy.pathWaypointCount = 0;
            enemy.pathWaypointIndex = 0;
            return false;
        }
        std::reverse(pathCells.begin(), pathCells.end());

        enemy.pathWaypointCount = 0;
        enemy.pathWaypointIndex = 0;
        int lastStepX = 0;
        int lastStepY = 0;
        for (int i = 1; i < static_cast<int>(pathCells.size()); ++i) {
            const int prev = pathCells[static_cast<std::size_t>(i - 1)];
            const int curr = pathCells[static_cast<std::size_t>(i)];
            const int px = prev % width;
            const int py = prev / width;
            const int cx = curr % width;
            const int cy = curr / width;
            const int stepX = cx - px;
            const int stepY = cy - py;
            const bool turnPoint =
                (i == 1) ||
                (stepX != lastStepX) ||
                (stepY != lastStepY) ||
                (i == static_cast<int>(pathCells.size()) - 1);
            lastStepX = stepX;
            lastStepY = stepY;
            if (!turnPoint) {
                continue;
            }
            if (enemy.pathWaypointCount >= EnemyTank::kMaxPathWaypoints) {
                break;
            }
            enemy.pathWaypoints[static_cast<std::size_t>(enemy.pathWaypointCount)] = CellCenter(state.world, cx, cy);
            ++enemy.pathWaypointCount;
        }
    }
    return enemy.pathWaypointCount > 0;
}

bool BuildAssassinPathToTarget(GameState& state, EnemyTank& enemy, int enemyIndex, const Vec2f& target) {
    const Vec2f previousPlayerPosition = state.world.player.position;
    state.world.player.position = target;
    const bool built = BuildAssassinPath(state, enemy, enemyIndex);
    state.world.player.position = previousPlayerPosition;
    return built;
}

Vec2f RandomMazePoint(const WorldState& world, Random& random) {
    const int cellX = random.NextInt(0, world.maze.widthCells - 1);
    const int cellY = random.NextInt(0, world.maze.heightCells - 1);
    return CellCenter(world, cellX, cellY);
}

bool BuildAssassinPathToFarRandomTarget(
    GameState& state,
    EnemyTank& enemy,
    int enemyIndex,
    Random& random) {
    profiling::ScopedProfile scope(profiling::Scope::PathfindingFarTarget, true);
    constexpr float kMinRandomTargetDistanceUnits = 24.0F;
    constexpr float kMinRandomTargetDistanceSq = kMinRandomTargetDistanceUnits * kMinRandomTargetDistanceUnits;
    constexpr int kMaxTargetAttempts = 24;
    for (int attempt = 0; attempt < kMaxTargetAttempts; ++attempt) {
        const Vec2f randomTarget = RandomMazePoint(state.world, random);
        if (DistanceSq(randomTarget, enemy.position) < kMinRandomTargetDistanceSq) {
            continue;
        }
        if (BuildAssassinPathToTarget(state, enemy, enemyIndex, randomTarget)) {
            return true;
        }
    }

    // Fallback: still pick some random destination if no far target succeeded.
    const Vec2f fallbackTarget = RandomMazePoint(state.world, random);
    return BuildAssassinPathToTarget(state, enemy, enemyIndex, fallbackTarget);
}

bool SelectDroneReturnToBaseHeading(
    const WorldState& world,
    const EnemyTank& enemy,
    Random& random,
    float& selectedHeading) {
    const Vec2f nearestBase = NearestBasePosition(world, enemy.position);
    const Vec2f toBase{
        .x = nearestBase.x - enemy.position.x,
        .y = nearestBase.y - enemy.position.y,
    };
    if (std::fabs(toBase.x) <= 0.001F && std::fabs(toBase.y) <= 0.001F) {
        return false;
    }

    const float desiredHeading = QuantizeToEightDirections(std::atan2(toBase.x, -toBase.y));
    const std::array<float, 8> offsets{
        0.0F,
        kEightDirectionStep,
        -kEightDirectionStep,
        kEightDirectionStep * 2.0F,
        -kEightDirectionStep * 2.0F,
        kEightDirectionStep * 3.0F,
        -kEightDirectionStep * 3.0F,
        kEightDirectionStep * 4.0F};

    std::array<float, 8> candidateHeadings{};
    int candidateCount = 0;
    int bestCandidateIndex = -1;
    float bestAlignment = std::numeric_limits<float>::infinity();
    for (float offset : offsets) {
        const float candidate = QuantizeToEightDirections(desiredHeading + offset);
        const float clearDistance = game::geometry::FreeDistanceAhead(
            world,
            enemy.position,
            candidate,
            kDroneReturnRequiredClearRunUnits,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
            kEnemyPlanningClearanceScale);
        if (clearDistance < kDroneReturnRequiredClearRunUnits) {
            continue;
        }
        const float alignment = AngleDistance(candidate, desiredHeading);
        if (candidateCount < static_cast<int>(candidateHeadings.size())) {
            candidateHeadings[static_cast<std::size_t>(candidateCount)] = candidate;
            if (alignment < bestAlignment) {
                bestAlignment = alignment;
                bestCandidateIndex = candidateCount;
            }
            ++candidateCount;
        }
    }

    if (candidateCount <= 0 || bestCandidateIndex < 0) {
        return false;
    }
    if (candidateCount == 1) {
        selectedHeading = candidateHeadings[0];
        return true;
    }

    constexpr float kBestHeadingWeight = 0.6F;
    constexpr float kOtherHeadingsTotalWeight = 0.4F;
    const float otherWeightEach = kOtherHeadingsTotalWeight / static_cast<float>(candidateCount - 1);
    const float pick = random.NextFloat(0.0F, 1.0F);
    float cumulative = 0.0F;
    for (int i = 0; i < candidateCount; ++i) {
        const float weight = (i == bestCandidateIndex) ? kBestHeadingWeight : otherWeightEach;
        cumulative += weight;
        if (pick <= cumulative || i == candidateCount - 1) {
            selectedHeading = candidateHeadings[static_cast<std::size_t>(i)];
            return true;
        }
    }

    selectedHeading = candidateHeadings[static_cast<std::size_t>(bestCandidateIndex)];
    return true;
}

bool SelectDroneWatchEscapeHeading(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    float deltaSeconds,
    float& selectedHeading) {
    const EnemyTank& self = enemies[static_cast<std::size_t>(selfIndex)];

    float currentNearestDistance = std::numeric_limits<float>::infinity();
    int nearestEnemyIndex = -1;
    for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
        if (i == selfIndex) {
            continue;
        }
        const EnemyTank& other = enemies[static_cast<std::size_t>(i)];
        if (!other.alive) {
            continue;
        }
        const float dist = Distance(self.position, other.position);
        if (dist < currentNearestDistance) {
            currentNearestDistance = dist;
            nearestEnemyIndex = i;
        }
    }

    float awayHeading = self.headingRadians;
    if (nearestEnemyIndex >= 0) {
        const EnemyTank& nearestEnemy = enemies[static_cast<std::size_t>(nearestEnemyIndex)];
        awayHeading = std::atan2(
            self.position.x - nearestEnemy.position.x,
            -(self.position.y - nearestEnemy.position.y));
    }

    const std::array<float, 8> offsets{
        0.0F,
        kEightDirectionStep,
        -kEightDirectionStep,
        kEightDirectionStep * 2.0F,
        -kEightDirectionStep * 2.0F,
        kEightDirectionStep * 3.0F,
        -kEightDirectionStep * 3.0F,
        kEightDirectionStep * 4.0F};

    const float stepDistance = GameplayConstants::kEnemyDroneSpeed * deltaSeconds;
    bool found = false;
    float bestNearestDistance = -1.0F;
    float bestAwayAlignment = std::numeric_limits<float>::infinity();
    float bestHeading = self.headingRadians;
    for (float offset : offsets) {
        const float candidateHeading = QuantizeToEightDirections(self.headingRadians + offset);
        const float clearDistance = game::geometry::FreeDistanceAhead(
            world,
            self.position,
            candidateHeading,
            GameplayConstants::kEnemyRequiredClearRunUnits + 0.5F,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
            kEnemyPlanningClearanceScale);
        if (clearDistance <= GameplayConstants::kEnemyRequiredClearRunUnits) {
            continue;
        }

        const Vec2f dir = DirectionFromHeading(candidateHeading);
        const Vec2f candidatePosition{
            .x = self.position.x + dir.x * stepDistance,
            .y = self.position.y + dir.y * stepDistance,
        };

        float nearestDistance = std::numeric_limits<float>::infinity();
        for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
            if (i == selfIndex) {
                continue;
            }
            const EnemyTank& other = enemies[static_cast<std::size_t>(i)];
            if (!other.alive) {
                continue;
            }
            nearestDistance = std::min(nearestDistance, Distance(candidatePosition, other.position));
        }
        const float awayAlignment = AngleDistance(candidateHeading, awayHeading);

        if (!found ||
            nearestDistance > bestNearestDistance + 0.001F ||
            (std::fabs(nearestDistance - bestNearestDistance) <= 0.001F &&
             awayAlignment < bestAwayAlignment)) {
            found = true;
            bestNearestDistance = nearestDistance;
            bestAwayAlignment = awayAlignment;
            bestHeading = candidateHeading;
        }
    }

    if (!found) {
        return false;
    }

    if (currentNearestDistance < GameplayConstants::kEnemyPreferredSeparationUnits &&
        bestNearestDistance <= currentNearestDistance + 0.001F) {
        return false;
    }

    selectedHeading = bestHeading;
    return true;
}

bool PlayerAheadForTorpedo(const EnemyTank& enemy, const Vec2f& toPlayerNormalized) {
    const Vec2f forward = DirectionFromHeading(enemy.headingRadians);
    const float dot = forward.x * toPlayerNormalized.x + forward.y * toPlayerNormalized.y;
    return dot >= kCosThirtyDegrees;
}

float SelectBestLongStraightHeading(const WorldState& world, const EnemyTank& enemy) {
    float bestHeading = QuantizeToEightDirections(enemy.headingRadians);
    float bestClear = -1.0F;
    for (int step = 0; step < 8; ++step) {
        const float candidate = NormalizeAngle(static_cast<float>(step) * kEightDirectionStep);
        const float clearDist = game::geometry::FreeDistanceAhead(
            world,
            enemy.position,
            candidate,
            kTorpedoLongPathProbeUnits,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
            kEnemyPlanningClearanceScale);
        if (clearDist > bestClear) {
            bestClear = clearDist;
            bestHeading = candidate;
        }
    }
    return QuantizeToEightDirections(bestHeading);
}

float SelectTorpedoMoveHeading(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    EnemyTank& enemy,
    Random& random,
    bool& startRetreat,
    bool& decidedStraight,
    const game::spatial::EnemySpatialGrid* spatialGrid) {
    profiling::ScopedProfile selectScope(profiling::Scope::EnemyTorpedoSelectHeading, true);
    startRetreat = false;
    decidedStraight = true;
    const float straightHeading = QuantizeToEightDirections(enemy.headingRadians);
    const float straightClearWithEnemies = game::geometry::FreeDistanceAheadWithEnemies(
        world,
        enemies,
        selfIndex,
        enemy.position,
        straightHeading,
        kSegmentBuildProbeMaxUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
        kEnemyPlanningClearanceScale,
        spatialGrid);
    const float leftHeading = QuantizeToEightDirections(straightHeading - kEightDirectionStep);
    const float rightHeading = QuantizeToEightDirections(straightHeading + kEightDirectionStep);
    const float leftClear = game::geometry::FreeDistanceAheadWithEnemies(
        world,
        enemies,
        selfIndex,
        enemy.position,
        leftHeading,
        kSegmentBuildProbeMaxUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
        kEnemyPlanningClearanceScale,
        spatialGrid);
    const float rightClear = game::geometry::FreeDistanceAheadWithEnemies(
        world,
        enemies,
        selfIndex,
        enemy.position,
        rightHeading,
        kSegmentBuildProbeMaxUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
        kEnemyPlanningClearanceScale,
        spatialGrid);

    if (straightClearWithEnemies < kTorpedoImmediateObstacleDistanceUnits &&
        leftClear < kTorpedoImmediateObstacleDistanceUnits &&
        rightClear < kTorpedoImmediateObstacleDistanceUnits) {
        startRetreat = true;
        return straightHeading;
    }

    struct Candidate {
        float heading;
        float clearDistance;
    };
    const std::array<Candidate, 3> candidates{{
        {.heading = straightHeading, .clearDistance = straightClearWithEnemies},
        {.heading = leftHeading, .clearDistance = leftClear},
        {.heading = rightHeading, .clearDistance = rightClear},
    }};
    float bestClear = -1.0F;
    for (const Candidate& candidate : candidates) {
        bestClear = std::max(bestClear, candidate.clearDistance);
    }
    std::array<int, 3> bestIndices{};
    int bestCount = 0;
    constexpr float kTieEpsilon = 0.001F;
    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
        if (std::fabs(candidates[static_cast<std::size_t>(i)].clearDistance - bestClear) <= kTieEpsilon) {
            bestIndices[static_cast<std::size_t>(bestCount)] = i;
            ++bestCount;
        }
    }
    const int chosenIndex =
        bestIndices[static_cast<std::size_t>(random.NextInt(0, std::max(0, bestCount - 1)))];
    const Candidate& chosen = candidates[static_cast<std::size_t>(chosenIndex)];
    const float maxSegmentLength = chosen.clearDistance - kSegmentBuildSafetyReduceUnits;
    if (maxSegmentLength < kSegmentBuildMinLengthUnits) {
        startRetreat = true;
        return straightHeading;
    }
    enemy.torpedoMoveDecisionHoldRemainingUnits =
        random.NextFloat(kSegmentBuildMinLengthUnits, maxSegmentLength);
    enemy.torpedoStraightDistanceSinceTurnUnits = 0.0F;
    decidedStraight = chosen.heading == straightHeading;

    return chosen.heading;
}

void EnterTorpedoTargetingMode(EnemyTank& enemy) {
    enemy.torpedoMoveMode = TorpedoMoveMode::Targeting;
}

void EnterTorpedoRotateMode(EnemyTank& enemy) {
    enemy.torpedoMoveMode = TorpedoMoveMode::Rotate;
    enemy.torpedoRotateTargetHeadingRadians = enemy.torpedoChosenHeadingRadians;
}

float UpdateTorpedoRotateHeading(EnemyTank& enemy, float deltaSeconds) {
    const float rotateStep =
        (kPi * 2.0F / GameplayConstants::kSlowRotateFullTurnSeconds) * deltaSeconds;
    const float signedDelta = SignedAngleDelta(enemy.headingRadians, enemy.torpedoRotateTargetHeadingRadians);
    if (std::fabs(signedDelta) <= rotateStep + 0.0001F) {
        const float heading = QuantizeToEightDirections(enemy.torpedoRotateTargetHeadingRadians);
        enemy.torpedoMoveMode = TorpedoMoveMode::Move;
        enemy.torpedoStraightDistanceSinceTurnUnits = 0.0F;
        enemy.torpedoMoveDecisionHoldRemainingUnits = 0.0F;
        return heading;
    }
    const float direction = signedDelta > 0.0F ? 1.0F : -1.0F;
    return NormalizeAngle(enemy.headingRadians + direction * rotateStep);
}

float SelectScoutHeadingWithFallback(
    const WorldState& world,
    const EnemyTank& enemy,
    bool allowNinetyTurns,
    bool& shouldRotate) {
    const float lookahead = game::geometry::FreeDistanceAhead(
        world,
        enemy.position,
        enemy.headingRadians,
        GameplayConstants::kEnemyLookaheadObstacleUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
        kEnemyPlanningClearanceScale);
    if (lookahead >= GameplayConstants::kEnemyLookaheadObstacleUnits) {
        shouldRotate = false;
        return enemy.headingRadians;
    }

    const std::array<float, 4> turns45{
        -kEightDirectionStep, kEightDirectionStep, 0.0F, 0.0F};
    const float turned45 = ChooseBestTurnHeading(
        world,
        enemy.position,
        enemy.headingRadians,
        turns45,
        2,
        GameplayConstants::kEnemyRequiredClearRunUnits);
    if (!std::isnan(turned45)) {
        shouldRotate = false;
        return turned45;
    }

    if (allowNinetyTurns) {
        const std::array<float, 4> turns90{
            -kEightDirectionStep * 2.0F, kEightDirectionStep * 2.0F, 0.0F, 0.0F};
        const float turned90 = ChooseBestTurnHeading(
            world,
            enemy.position,
            enemy.headingRadians,
            turns90,
            2,
            GameplayConstants::kEnemyRequiredClearRunUnits);
        if (!std::isnan(turned90)) {
            shouldRotate = false;
            return turned90;
        }
    }

    shouldRotate = true;
    return enemy.headingRadians;
}

bool TrySeparationTurn(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    float speed,
    float deltaSeconds,
    float& chosenHeading,
    Vec2f& candidatePosition) {
    const EnemyTank& self = enemies[static_cast<std::size_t>(selfIndex)];
    const std::array<float, 2> turnOffsets{-kEightDirectionStep, kEightDirectionStep};
    float bestDistanceSq = -1.0F;
    const float sepSq = GameplayConstants::kEnemyPreferredSeparationUnits * GameplayConstants::kEnemyPreferredSeparationUnits;
    bool found = false;
    for (float offset : turnOffsets) {
        const float candidateHeading = QuantizeToEightDirections(self.headingRadians + offset);
        const Vec2f dir = DirectionFromHeading(candidateHeading);
        const Vec2f candidate{
            .x = self.position.x + dir.x * speed * deltaSeconds,
            .y = self.position.y + dir.y * speed * deltaSeconds,
        };
        if (SegmentIntersectsWall(
                world,
                self.position,
                candidate,
                GameplayConstants::kEnemyWallAvoidanceRadiusUnits)) {
            continue;
        }
        float nearestSq = std::numeric_limits<float>::infinity();
        for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
            if (i == selfIndex) {
                continue;
            }
            const EnemyTank& other = enemies[static_cast<std::size_t>(i)];
            if (!other.alive) {
                continue;
            }
            nearestSq = std::min(nearestSq, DistanceSq(candidate, other.position));
        }
        if (nearestSq > bestDistanceSq && nearestSq >= sepSq) {
            bestDistanceSq = nearestSq;
            chosenHeading = candidateHeading;
            candidatePosition = candidate;
            found = true;
        }
    }
    return found && bestDistanceSq >= sepSq;
}

bool IsMovementBlockedByEnemies(
    const std::vector<EnemyTank>& enemies,
    const std::vector<Vec2f>& frameStartPositions,
    int selfIndex,
    const Vec2f& from,
    const Vec2f& to,
    float minSeparation) {
    constexpr float kSeparationProgressEpsilon = 0.001F;
    for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
        if (i == selfIndex) {
            continue;
        }
        const EnemyTank& other = enemies[static_cast<std::size_t>(i)];
        if (!other.alive) {
            continue;
        }

        // Use updated position for already-processed enemies and frame-start position for others.
        const Vec2f otherObstacle = (i < selfIndex)
            ? other.position
            : frameStartPositions[static_cast<std::size_t>(i)];

        const float fromDistance = Distance(from, otherObstacle);
        const float toDistance = Distance(to, otherObstacle);
        const bool separatingFromOverlap =
            fromDistance < minSeparation &&
            toDistance > fromDistance + kSeparationProgressEpsilon;

        if (toDistance < minSeparation && !separatingFromOverlap) {
            return true;
        }
        if (!separatingFromOverlap &&
            DistancePointToSegment(otherObstacle, from, to) < minSeparation) {
            return true;
        }
    }
    return false;
}

void ResolveEnemySeparation(
    WorldState& world,
    game::spatial::EnemySpatialGrid& grid,
    const std::vector<std::uint8_t>& includeMask) {
    profiling::ScopedProfile scope(profiling::Scope::EnemySeparation, true);
    {
        profiling::ScopedProfile buildScope(profiling::Scope::EnemySeparationGridBuild, true);
        grid.BuildFromPositions(world, nullptr, &includeMask);
    }
    {
        profiling::ScopedProfile pairTraverseScope(profiling::Scope::EnemySeparationPairTraverse, true);
        grid.ForEachPairInSameOrAdjacentCell(world.enemies, [&](int i, int j) {
        gEnemyRuntimeStats.separationPairsVisited += 1;
        EnemyTank& a = world.enemies[static_cast<std::size_t>(i)];
        EnemyTank& b = world.enemies[static_cast<std::size_t>(j)];
        if (game::geometry::IsPointInUndestroyedBase(world, a.position, 1.0F) ||
            game::geometry::IsPointInUndestroyedBase(world, b.position, 1.0F)) {
            return;
        }
        profiling::ScopedProfile pairScope(profiling::Scope::EnemySeparationPairResolve, true);

        const float distSq = DistanceSq(a.position, b.position);
        const float killDistSq =
            GameplayConstants::kEnemyMutualKillDistanceUnits * GameplayConstants::kEnemyMutualKillDistanceUnits;
        if (distSq <= killDistSq) {
            a.alive = false;
            b.alive = false;
            DecrementOriginBaseAliveCount(world, a);
            DecrementOriginBaseAliveCount(world, b);
            return;
        }
        const float sepSq =
            GameplayConstants::kEnemyPreferredSeparationUnits * GameplayConstants::kEnemyPreferredSeparationUnits;
        if (distSq >= sepSq) {
            return;
        }
        gEnemyRuntimeStats.separationPairsResolved += 1;

        const float dist = std::sqrt(distSq);
        const float push = (GameplayConstants::kEnemyPreferredSeparationUnits - dist) * 0.5F;
        Vec2f dir = NormalizeOrZero(Vec2f{
            .x = b.position.x - a.position.x,
            .y = b.position.y - a.position.y,
        });
        if (dir.x == 0.0F && dir.y == 0.0F) {
            dir = Vec2f{.x = 1.0F, .y = 0.0F};
        }

        const Vec2f movedA{
            .x = a.position.x - dir.x * push,
            .y = a.position.y - dir.y * push,
        };
        const Vec2f movedB{
            .x = b.position.x + dir.x * push,
            .y = b.position.y + dir.y * push,
        };
        const bool aBlocked =
            game::geometry::IsPointInWall(world, movedA, GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
        const bool bBlocked =
            game::geometry::IsPointInWall(world, movedB, GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
        if (!aBlocked) {
            a.position = movedA;
        }
        if (!bBlocked) {
            b.position = movedB;
        }
        if (aBlocked || bBlocked) {
            a.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
            b.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        }
        });
    }
}

void ResolveEnemyFrontalCollisions(
    WorldState& world,
    const std::vector<Vec2f>& frameStartPositions,
    game::spatial::EnemySpatialGrid& grid,
    const std::vector<std::uint8_t>& includeMask) {
    profiling::ScopedProfile scope(profiling::Scope::EnemyFrontalCollisions, true);
    {
        profiling::ScopedProfile buildScope(profiling::Scope::EnemyFrontalGridBuild, true);
        grid.BuildFromSegments(world, frameStartPositions, &includeMask);
    }
    {
        profiling::ScopedProfile pairTraverseScope(profiling::Scope::EnemyFrontalPairTraverse, true);
        grid.ForEachPairInSameOrAdjacentCell(world.enemies, [&](int i, int j) {
        gEnemyRuntimeStats.frontalPairsVisited += 1;
        EnemyTank& a = world.enemies[static_cast<std::size_t>(i)];
        EnemyTank& b = world.enemies[static_cast<std::size_t>(j)];
        if (game::geometry::IsPointInUndestroyedBase(world, a.position, 1.0F) ||
            game::geometry::IsPointInUndestroyedBase(world, b.position, 1.0F)) {
            return;
        }
        profiling::ScopedProfile pairScope(profiling::Scope::EnemyFrontalPairNarrowphase, true);
        gEnemyRuntimeStats.frontalPairsDistanceChecks += 1;
        const Vec2f aStart = frameStartPositions[static_cast<std::size_t>(i)];
        const Vec2f aEnd = a.position;
        const Vec2f bStart = frameStartPositions[static_cast<std::size_t>(j)];
        const Vec2f bEnd = b.position;
        const float distance = SegmentToSegmentDistance(aStart, aEnd, bStart, bEnd);
        if (distance <= GameplayConstants::kEnemyMutualKillDistanceUnits) {
            a.alive = false;
            b.alive = false;
            DecrementOriginBaseAliveCount(world, a);
            DecrementOriginBaseAliveCount(world, b);
        }
        });
    }
}

struct EnemyPerception {
    Vec2f toPlayer{};
    Vec2f toPlayerNormalized{};
    float distanceToPlayerSq = 0.0F;
    float distanceToPlayer = 0.0F;
    bool playerObscured = false;
    bool assassinHasLineOfSight = false;
};

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
                const float relativeBearing = AngleDistance(enemy.headingRadians, headingToBase);
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

profiling::Scope EnemyTypeProfileScope(EnemyType type) {
    switch (type) {
    case EnemyType::Drone:
        return profiling::Scope::EnemyTypeDroneUpdate;
    case EnemyType::Torpedo:
        return profiling::Scope::EnemyTypeTorpedoUpdate;
    case EnemyType::Hunter:
        return profiling::Scope::EnemyTypeHunterUpdate;
    case EnemyType::Assassin:
        return profiling::Scope::EnemyTypeAssassinUpdate;
    }
    return profiling::Scope::EnemyTypeDroneUpdate;
}

void AdvanceCheapTierTimers(
    GameState& state,
    EnemyTank& enemy,
    const EnemyPerception& perception,
    float deltaSeconds,
    bool playerInvisible,
    const GameplayView& view) {
    enemy.aiModeElapsedSeconds += deltaSeconds;
    enemy.selfAwarenessTimerSeconds -= deltaSeconds;
    if (enemy.selfAwarenessTimerSeconds <= 0.0F) {
        if (enemy.selfAwarenessIntervalSeconds <= 0.0F) {
            enemy.selfAwarenessIntervalSeconds = (enemy.type == EnemyType::Drone) ? 9.0F : 6.0F;
        }
        enemy.selfAwarenessTimerSeconds += enemy.selfAwarenessIntervalSeconds;
    }

    if (enemy.type == EnemyType::Torpedo) {
        enemy.torpedoPlayerDetectTimerSeconds -= deltaSeconds;
        if (enemy.torpedoPlayerDetectTimerSeconds <= 0.0F) {
            enemy.torpedoPlayerDetectTimerSeconds = kOffscreenTorpedoDetectIntervalSeconds;
            enemy.torpedoPlayerDetected =
                !playerInvisible &&
                perception.distanceToPlayer <= GameplayConstants::kTorpedoDetectRangeUnits &&
                !game::geometry::IsSegmentObscuredByWall(state.world, enemy.position, state.world.player.position);
            enemy.torpedoLastKnownPlayerHeadingRadians =
                std::atan2(perception.toPlayer.x, -perception.toPlayer.y);
        }
    }

    // Keep weapon cadence realistic while cheap; this prevents on-screen burst anomalies.
    enemy.fireCooldownSeconds = std::max(0.0F, enemy.fireCooldownSeconds - deltaSeconds);
    (void)view;
}

void BuildOffscreenSegment(WorldState& world, EnemyTank& enemy, float segmentLengthUnits, Random& random) {
    profiling::ScopedProfile buildScope(profiling::Scope::EnemyCheapSegmentBuild, true);
    const float forwardHeading = QuantizeToEightDirections(enemy.headingRadians);
    const float leftHeading = QuantizeToEightDirections(forwardHeading - kEightDirectionStep);
    const float rightHeading = QuantizeToEightDirections(forwardHeading + kEightDirectionStep);
    struct Candidate {
        float heading;
        float clearDistance;
    };
    const std::array<Candidate, 3> candidates{{
        {.heading = forwardHeading,
            .clearDistance = FreeDistanceAheadWallsOnly(
                world,
                enemy.position,
                forwardHeading,
                kSegmentBuildProbeMaxUnits,
                GameplayConstants::kEnemyWallAvoidanceRadiusUnits)},
        {.heading = leftHeading,
            .clearDistance = FreeDistanceAheadWallsOnly(
                world,
                enemy.position,
                leftHeading,
                kSegmentBuildProbeMaxUnits,
                GameplayConstants::kEnemyWallAvoidanceRadiusUnits)},
        {.heading = rightHeading,
            .clearDistance = FreeDistanceAheadWallsOnly(
                world,
                enemy.position,
                rightHeading,
                kSegmentBuildProbeMaxUnits,
                GameplayConstants::kEnemyWallAvoidanceRadiusUnits)},
    }};
    float bestClear = -1.0F;
    for (const Candidate& candidate : candidates) {
        bestClear = std::max(bestClear, candidate.clearDistance);
    }
    std::array<int, 3> bestIndices{};
    int bestCount = 0;
    constexpr float kTieEpsilon = 0.001F;
    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
        if (std::fabs(candidates[static_cast<std::size_t>(i)].clearDistance - bestClear) <= kTieEpsilon) {
            bestIndices[static_cast<std::size_t>(bestCount)] = i;
            ++bestCount;
        }
    }
    const int chosenIndex =
        bestIndices[static_cast<std::size_t>(random.NextInt(0, std::max(0, bestCount - 1)))];
    const Candidate& chosen = candidates[static_cast<std::size_t>(chosenIndex)];
    const float maxSegmentLength =
        std::min(segmentLengthUnits, chosen.clearDistance - kSegmentBuildSafetyReduceUnits);
    if (maxSegmentLength < kSegmentBuildMinLengthUnits) {
        enemy.offscreenSegmentActive = false;
        enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        return;
    }
    const float targetDistance = random.NextFloat(kSegmentBuildMinLengthUnits, maxSegmentLength);
    const Vec2f dir = DirectionFromHeading(chosen.heading);
    enemy.offscreenCachedHeadingRadians = chosen.heading;
    enemy.offscreenSegmentEnd = Vec2f{
        .x = enemy.position.x + dir.x * targetDistance,
        .y = enemy.position.y + dir.y * targetDistance,
    };
    enemy.offscreenSegmentActive = true;
}

void ApplyCheapTierMovement(
    GameState& state,
    EnemyTank& enemy,
    int enemyIndex,
    float deltaSeconds,
    float speed,
    Random& random) {
    float segmentLength = kOffscreenSegmentLengthUnits;
    if (enemy.type == EnemyType::Torpedo) {
        segmentLength = kOffscreenTorpedoSegmentLengthUnits;
    }
    const bool reachedSegmentEnd =
        !enemy.offscreenSegmentActive ||
        DistanceSq(enemy.position, enemy.offscreenSegmentEnd) <= 0.04F;
    if (reachedSegmentEnd) {
        // Cheap-tier policy: decide the next segment only at segment endpoints.
        BuildOffscreenSegment(state.world, enemy, segmentLength, random);
    }

    if (!enemy.offscreenSegmentActive || std::fabs(speed) <= 0.0001F) {
        enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
        return;
    }

    const Vec2f dir = DirectionFromHeading(enemy.offscreenCachedHeadingRadians);
    const float maxStep = std::max(0.0F, speed * deltaSeconds);
    const float remaining = Distance(enemy.position, enemy.offscreenSegmentEnd);
    const float step = std::min(maxStep, remaining);
    enemy.position = Vec2f{
        .x = enemy.position.x + dir.x * step,
        .y = enemy.position.y + dir.y * step,
    };
    enemy.headingRadians = enemy.offscreenCachedHeadingRadians;
    enemy.velocity = Vec2f{
        .x = dir.x * speed,
        .y = dir.y * speed,
    };

    if (enemy.type == EnemyType::Torpedo && enemy.torpedoMoveMode == TorpedoMoveMode::Move) {
        enemy.torpedoStraightDistanceSinceTurnUnits += step;
    }
    (void)enemyIndex;
}
}  // namespace

void UpdateEnemySystem(GameState& state, const GameplayView& view, float deltaSeconds, Random& random) {
    profiling::ScopedProfile scope(profiling::Scope::EnemyUpdate, true);
    gEnemyRuntimeStats = EnemyRuntimeStats{};
    const bool playerInvisible = state.menuSettings.invisibility;
    std::vector<Vec2f> frameStartPositions{};
    frameStartPositions.reserve(state.world.enemies.size());
    for (const EnemyTank& enemy : state.world.enemies) {
        frameStartPositions.push_back(enemy.position);
    }

    game::spatial::EnemySpatialGrid rayQueryGrid;
    rayQueryGrid.BuildFromPositions(state.world, &frameStartPositions);

    for (int enemyIndex = 0; enemyIndex < static_cast<int>(state.world.enemies.size()); ++enemyIndex) {
        EnemyTank& enemy = state.world.enemies[static_cast<std::size_t>(enemyIndex)];
        if (!enemy.alive) {
            continue;
        }
        profiling::ScopedProfile enemyTypeScope(EnemyTypeProfileScope(enemy.type), true);

        const EnemySimTier previousTier = enemy.simTier;
        enemy.simTier = DetermineEnemySimTier(enemy, state, view);
        const bool reenteredFullTier =
            previousTier == EnemySimTier::Cheap &&
            enemy.simTier == EnemySimTier::Full;
        if (reenteredFullTier) {
            enemy.offscreenSegmentActive = false;
            if (enemy.type == EnemyType::Assassin) {
                enemy.pathWaypointCount = 0;
                enemy.pathWaypointIndex = 0;
            }
        }

        if (enemy.simTier == EnemySimTier::Cheap) {
            EnemyPerception cheapPerception{};
            cheapPerception.toPlayer = Vec2f{
                .x = state.world.player.position.x - enemy.position.x,
                .y = state.world.player.position.y - enemy.position.y,
            };
            cheapPerception.toPlayerNormalized = NormalizeOrZero(cheapPerception.toPlayer);
            cheapPerception.distanceToPlayerSq = DistanceSq(enemy.position, state.world.player.position);
            cheapPerception.distanceToPlayer = std::sqrt(cheapPerception.distanceToPlayerSq);
            cheapPerception.playerObscured = true;
            cheapPerception.assassinHasLineOfSight = false;

            AdvanceCheapTierTimers(
                state,
                enemy,
                cheapPerception,
                deltaSeconds,
                playerInvisible,
                view);

            float cheapSpeed = EnemySpeed(enemy.type, enemy.subtype, false);
            if (enemy.aiMode == EnemyAiMode::Watch || enemy.aiMode == EnemyAiMode::Rotate) {
                cheapSpeed = 0.0F;
            }
            ApplyCheapTierMovement(state, enemy, enemyIndex, deltaSeconds, cheapSpeed, random);
            continue;
        }

        EnemyPerception perception{};
        {
            profiling::ScopedProfile phaseScope(profiling::Scope::EnemyAiPerception, true);
            perception = RunPerceptionPhase(state, enemy, deltaSeconds, playerInvisible, random);
        }

        float movementHeading = QuantizeToEightDirections(enemy.headingRadians);
        float speed = EnemySpeed(enemy.type, enemy.subtype, perception.assassinHasLineOfSight);
        bool preserveContinuousHeading = false;
        {
            profiling::ScopedProfile phaseScope(profiling::Scope::EnemyAiDecision, true);
            if (enemy.type == EnemyType::Drone) {
                if (enemy.aiMode != EnemyAiMode::Watch && enemy.aiMode != EnemyAiMode::Wander) {
                    enemy.aiMode = EnemyAiMode::Wander;
                    enemy.aiModeElapsedSeconds = 0.0F;
                }
                if (enemy.aiMode == EnemyAiMode::Watch) {
                    speed = 0.0F;
                    preserveContinuousHeading = true;
                    movementHeading = NormalizeAngle(
                        enemy.headingRadians +
                        static_cast<float>(enemy.watchRotateDirection) *
                            (kPi * 2.0F / GameplayConstants::kSlowRotateFullTurnSeconds) * deltaSeconds);
                    const float clearDistance = game::geometry::FreeDistanceAhead(
                        state.world,
                        enemy.position,
                        movementHeading,
                        GameplayConstants::kEnemyRequiredClearRunUnits + 0.5F,
                        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                        kEnemyPlanningClearanceScale);
                    if (enemy.aiModeElapsedSeconds >= GameplayConstants::kSlowRotateFullTurnSeconds) {
                        if (enemy.returnToBase) {
                            float returnHeading = movementHeading;
                            if (SelectDroneReturnToBaseHeading(state.world, enemy, random, returnHeading)) {
                                movementHeading = returnHeading;
                                enemy.returnToBase = false;
                                enemy.aiMode = EnemyAiMode::Wander;
                                enemy.aiModeElapsedSeconds = 0.0F;
                            }
                        } else if (clearDistance > GameplayConstants::kEnemyRequiredClearRunUnits) {
                            float escapeHeading = movementHeading;
                            if (SelectDroneWatchEscapeHeading(
                                    state.world,
                                    state.world.enemies,
                                    enemyIndex,
                                    deltaSeconds,
                                    escapeHeading)) {
                                movementHeading = escapeHeading;
                                enemy.aiMode = EnemyAiMode::Wander;
                                enemy.aiModeElapsedSeconds = 0.0F;
                            }
                        }
                    }
                } else {
                    bool shouldWatch = false;
                    movementHeading = SelectScoutHeadingWithFallback(
                        state.world,
                        enemy,
                        false,
                        shouldWatch);
                    if (shouldWatch) {
                        EnterDroneWatchMode(state.world, enemy, random);
                        speed = 0.0F;
                    }
                }
            } else if (enemy.type == EnemyType::Torpedo) {
                enemy.torpedoPlayerDetectTimerSeconds -= deltaSeconds;
                if (enemy.torpedoPlayerDetectTimerSeconds <= 0.0F) {
                    enemy.torpedoPlayerDetectTimerSeconds = kTorpedoPlayerDetectIntervalSeconds;
                    enemy.torpedoPlayerDetected =
                        !playerInvisible &&
                        !perception.playerObscured &&
                        perception.distanceToPlayer <= GameplayConstants::kTorpedoDetectRangeUnits;
                    enemy.torpedoLastKnownPlayerHeadingRadians =
                        std::atan2(perception.toPlayer.x, -perception.toPlayer.y);
                }
                if (enemy.torpedoMoveMode == TorpedoMoveMode::Retreat) {
                    movementHeading = QuantizeToEightDirections(enemy.headingRadians);
                    speed = -std::abs(speed) * kTorpedoRetreatSpeedFactor;
                    const float forwardClear = game::geometry::FreeDistanceAheadWithEnemies(
                        state.world,
                        state.world.enemies,
                        enemyIndex,
                        enemy.position,
                        enemy.headingRadians,
                        kTorpedoNearCollisionCheckDistanceUnits,
                        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                        kEnemyPlanningClearanceScale,
                        &rayQueryGrid);
                    if (enemy.torpedoRetreatMovedUnits >= kTorpedoRetreatExitClearanceUnits &&
                        forwardClear >= kTorpedoRetreatExitClearanceUnits) {
                        speed = 0.0F;
                        EnterTorpedoTargetingMode(enemy);
                    }
                } else if (enemy.torpedoMoveMode == TorpedoMoveMode::Targeting) {
                    speed = 0.0F;
                    enemy.torpedoChosenHeadingRadians = SelectBestLongStraightHeading(state.world, enemy);
                    EnterTorpedoRotateMode(enemy);
                } else if (enemy.torpedoMoveMode == TorpedoMoveMode::Rotate) {
                    speed = 0.0F;
                    preserveContinuousHeading = true;
                    movementHeading = UpdateTorpedoRotateHeading(enemy, deltaSeconds);
                } else {
                    const float straightHeading = QuantizeToEightDirections(enemy.headingRadians);
                    const bool lockHeadingForBaseExit = IsPointInUndestroyedBase(
                        state.world,
                        enemy.position,
                        1.0F);
                    if (lockHeadingForBaseExit || enemy.torpedoMoveDecisionHoldRemainingUnits > 0.0F) {
                        movementHeading = straightHeading;
                        const float nearClear = game::geometry::FreeDistanceAheadWithEnemies(
                            state.world,
                            state.world.enemies,
                            enemyIndex,
                            enemy.position,
                            straightHeading,
                            kTorpedoNearCollisionCheckDistanceUnits,
                            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                            kEnemyPlanningClearanceScale,
                            &rayQueryGrid);
                        if (nearClear < kTorpedoImmediateObstacleDistanceUnits) {
                            enemy.torpedoMoveMode = TorpedoMoveMode::Retreat;
                            enemy.torpedoRetreatMovedUnits = 0.0F;
                            enemy.torpedoMoveDecisionHoldRemainingUnits = 0.0F;
                            movementHeading = straightHeading;
                            speed = -std::abs(speed) * kTorpedoRetreatSpeedFactor;
                        } else if (!lockHeadingForBaseExit &&
                            nearClear < kTorpedoNearCollisionCheckDistanceUnits) {
                            enemy.torpedoMoveDecisionHoldRemainingUnits = 0.0F;
                        } else if (lockHeadingForBaseExit) {
                            // Keep torpedo on its spawn heading until it clears base + margin.
                            enemy.torpedoMoveDecisionHoldRemainingUnits = std::max(
                                enemy.torpedoMoveDecisionHoldRemainingUnits,
                                kTorpedoMoveDecisionHoldDistanceUnits);
                        }
                    } else {
                        bool startRetreat = false;
                        bool decidedStraight = true;
                        movementHeading = SelectTorpedoMoveHeading(
                            state.world,
                            state.world.enemies,
                            enemyIndex,
                            enemy,
                            random,
                            startRetreat,
                            decidedStraight,
                            &rayQueryGrid);
                        if (startRetreat) {
                            enemy.torpedoMoveMode = TorpedoMoveMode::Retreat;
                            enemy.torpedoRetreatMovedUnits = 0.0F;
                            enemy.torpedoMoveDecisionHoldRemainingUnits = 0.0F;
                            movementHeading = straightHeading;
                            speed = -std::abs(speed) * kTorpedoRetreatSpeedFactor;
                        }
                    }
                }
            } else if (enemy.type == EnemyType::Hunter) {
                const bool canChase =
                    !playerInvisible &&
                    !perception.playerObscured &&
                    perception.distanceToPlayer <= GameplayConstants::kHunterDetectRangeUnits;
                if (canChase) {
                    enemy.aiMode = EnemyAiMode::Chase;
                    enemy.aiModeElapsedSeconds = 0.0F;
                } else if (enemy.aiMode == EnemyAiMode::Chase) {
                    enemy.aiMode = EnemyAiMode::Scout;
                    enemy.aiModeElapsedSeconds = 0.0F;
                }

                if (enemy.aiMode == EnemyAiMode::Chase) {
                    if (perception.distanceToPlayer < GameplayConstants::kHunterMinDistanceUnits) {
                        movementHeading = QuantizeToEightDirections(
                            std::atan2(-perception.toPlayer.x, perception.toPlayer.y));
                    } else if (perception.distanceToPlayer > GameplayConstants::kHunterMaxDistanceUnits) {
                        movementHeading = QuantizeToEightDirections(
                            std::atan2(perception.toPlayer.x, -perception.toPlayer.y));
                    } else {
                        speed = 0.0F;
                    }
                } else if (enemy.aiMode == EnemyAiMode::Rotate) {
                    speed = 0.0F;
                    preserveContinuousHeading = true;
                    movementHeading = NormalizeAngle(
                        enemy.headingRadians + (kPi * 2.0F / GameplayConstants::kSlowRotateFullTurnSeconds) * deltaSeconds);
                    const float clearDistance = game::geometry::FreeDistanceAhead(
                        state.world,
                        enemy.position,
                        movementHeading,
                        GameplayConstants::kEnemyRequiredClearRunUnits + 0.5F,
                        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                        kEnemyPlanningClearanceScale);
                    if (clearDistance > GameplayConstants::kEnemyRequiredClearRunUnits) {
                        enemy.aiMode = EnemyAiMode::Scout;
                        enemy.aiModeElapsedSeconds = 0.0F;
                    }
                } else {
                    bool shouldRotate = false;
                    movementHeading = SelectScoutHeadingWithFallback(
                        state.world,
                        enemy,
                        true,
                        shouldRotate);
                    if (shouldRotate) {
                        enemy.aiMode = EnemyAiMode::Rotate;
                        enemy.aiModeElapsedSeconds = 0.0F;
                        speed = 0.0F;
                    } else {
                        enemy.aiMode = EnemyAiMode::Scout;
                    }
                }
            } else {
                enemy.aiMode = EnemyAiMode::Path;
                if (!playerInvisible && perception.distanceToPlayer < GameplayConstants::kAssassinMinDistanceUnits) {
                    speed = 0.0F;
                    enemy.pathWaypointCount = 0;
                    enemy.pathWaypointIndex = 0;
                } else {
                    const float obstacleAhead = game::geometry::FreeDistanceAhead(
                        state.world,
                        enemy.position,
                        enemy.headingRadians,
                        2.0F,
                        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                        kEnemyPlanningClearanceScale);
                    const bool needRepathObstacle = obstacleAhead < 2.0F;
                    const bool needRepathEmpty = enemy.pathWaypointCount <= 0 || enemy.pathWaypointIndex >= enemy.pathWaypointCount;
                    if (needRepathObstacle || needRepathEmpty) {
                        if (playerInvisible) {
                            BuildAssassinPathToFarRandomTarget(state, enemy, enemyIndex, random);
                        } else {
                            BuildAssassinPath(state, enemy, enemyIndex);
                        }
                    }

                    if (enemy.pathWaypointCount > 0 && enemy.pathWaypointIndex < enemy.pathWaypointCount) {
                        const Vec2f waypoint = enemy.pathWaypoints[static_cast<std::size_t>(enemy.pathWaypointIndex)];
                        const Vec2f toWaypoint{
                            .x = waypoint.x - enemy.position.x,
                            .y = waypoint.y - enemy.position.y,
                        };
                        if (DistanceSq(waypoint, enemy.position) <= 0.36F) {
                            enemy.pathWaypointIndex += 1;
                            if (playerInvisible) {
                                BuildAssassinPathToFarRandomTarget(state, enemy, enemyIndex, random);
                            } else if (enemy.pathWaypointIndex < enemy.pathWaypointCount) {
                                BuildAssassinPath(state, enemy, enemyIndex);
                            }
                        }
                        const Vec2f stepDir = NormalizeOrZero(toWaypoint);
                        if (stepDir.x != 0.0F || stepDir.y != 0.0F) {
                            movementHeading = QuantizeToEightDirections(std::atan2(stepDir.x, -stepDir.y));
                        }
                    } else {
                        const Vec2f predicted{
                            .x = state.world.player.position.x +
                                state.world.player.velocity.x * GameplayConstants::kEnemyAssassinPredictionSeconds,
                            .y = state.world.player.position.y +
                                state.world.player.velocity.y * GameplayConstants::kEnemyAssassinPredictionSeconds,
                        };
                        movementHeading = QuantizeToEightDirections(
                            std::atan2(predicted.x - enemy.position.x, -(predicted.y - enemy.position.y)));
                    }
                }
            }
        }

        const Vec2f previousPosition = enemy.position;
        {
            profiling::ScopedProfile phaseScope(profiling::Scope::EnemyAiMovement, true);
            if (preserveContinuousHeading) {
                movementHeading = NormalizeAngle(movementHeading);
            } else {
                movementHeading = QuantizeToEightDirections(movementHeading);
            }
            const Vec2f snappedDirection = DirectionFromHeading(movementHeading);
            Vec2f candidatePosition{
                .x = enemy.position.x + snappedDirection.x * speed * deltaSeconds,
                .y = enemy.position.y + snappedDirection.y * speed * deltaSeconds,
            };

            // Keep enemies from overlapping: turn first, stop second.
            const float sepSq =
                GameplayConstants::kEnemyPreferredSeparationUnits * GameplayConstants::kEnemyPreferredSeparationUnits;
            float minDistSqToOthers = std::numeric_limits<float>::infinity();
            float currentMinDistSqToOthers = std::numeric_limits<float>::infinity();
            for (int i = 0; i < static_cast<int>(state.world.enemies.size()); ++i) {
                if (i == enemyIndex) {
                    continue;
                }
                const EnemyTank& other = state.world.enemies[static_cast<std::size_t>(i)];
                if (!other.alive) {
                    continue;
                }
                currentMinDistSqToOthers =
                    std::min(currentMinDistSqToOthers, DistanceSq(enemy.position, other.position));
                minDistSqToOthers =
                    std::min(minDistSqToOthers, DistanceSq(candidatePosition, other.position));
            }
            constexpr float kSeparationProgressEpsilonSq = 0.01F;
            const bool makingSeparationProgress =
                minDistSqToOthers > currentMinDistSqToOthers + kSeparationProgressEpsilonSq;
            if (std::fabs(speed) > 0.0F &&
                minDistSqToOthers < sepSq &&
                !makingSeparationProgress) {
                float turnHeading = movementHeading;
                Vec2f turnCandidate = candidatePosition;
                if (TrySeparationTurn(state.world, state.world.enemies, enemyIndex, speed, deltaSeconds, turnHeading, turnCandidate)) {
                    movementHeading = turnHeading;
                    candidatePosition = turnCandidate;
                } else {
                    speed = 0.0F;
                    candidatePosition = enemy.position;
                    enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
                    if (enemy.type == EnemyType::Drone) {
                        EnterDroneWatchMode(state.world, enemy, random);
                    }
                }
            }

            if (std::fabs(speed) > 0.0F && IsMovementBlockedByEnemies(
                    state.world.enemies,
                    frameStartPositions,
                    enemyIndex,
                    previousPosition,
                    candidatePosition,
                    GameplayConstants::kEnemyPreferredSeparationUnits)) {
                float turnHeading = movementHeading;
                Vec2f turnCandidate = candidatePosition;
                const bool foundTurn = TrySeparationTurn(
                    state.world,
                    state.world.enemies,
                    enemyIndex,
                    speed,
                    deltaSeconds,
                    turnHeading,
                    turnCandidate);
                if (foundTurn && !IsMovementBlockedByEnemies(
                        state.world.enemies,
                        frameStartPositions,
                        enemyIndex,
                        previousPosition,
                        turnCandidate,
                        GameplayConstants::kEnemyPreferredSeparationUnits)) {
                    movementHeading = turnHeading;
                    candidatePosition = turnCandidate;
                } else {
                    enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
                    candidatePosition = enemy.position;
                    if (enemy.type == EnemyType::Drone) {
                        EnterDroneWatchMode(state.world, enemy, random);
                    }
                }
            }

            if (std::fabs(speed) > 0.001F && SegmentIntersectsWall(
                    state.world,
                    previousPosition,
                    candidatePosition,
                    GameplayConstants::kEnemyWallAvoidanceRadiusUnits)) {
                enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
                enemy.aiStateTimerSeconds = 0.0F;
                if (enemy.type == EnemyType::Drone) {
                    EnterDroneWatchMode(state.world, enemy, random);
                } else if (enemy.type == EnemyType::Hunter) {
                    enemy.aiMode = EnemyAiMode::Rotate;
                    enemy.aiModeElapsedSeconds = 0.0F;
                }
            } else {
                enemy.velocity = Vec2f{
                    .x = snappedDirection.x * speed,
                    .y = snappedDirection.y * speed,
                };
                enemy.position = candidatePosition;
                enemy.headingRadians = movementHeading;
            }

            if (enemy.type == EnemyType::Torpedo && enemy.torpedoMoveMode == TorpedoMoveMode::Move) {
                const float movedDistance = Distance(enemy.position, previousPosition);
                if (movedDistance > 0.0001F) {
                    enemy.torpedoStraightDistanceSinceTurnUnits += movedDistance;
                    enemy.torpedoMoveDecisionHoldRemainingUnits = std::max(
                        0.0F,
                        enemy.torpedoMoveDecisionHoldRemainingUnits - movedDistance);
                }
            } else if (enemy.type == EnemyType::Torpedo && enemy.torpedoMoveMode == TorpedoMoveMode::Retreat) {
                const float movedDistance = Distance(enemy.position, previousPosition);
                if (movedDistance > 0.0001F) {
                    enemy.torpedoRetreatMovedUnits += movedDistance;
                }
            }
        }

        {
            profiling::ScopedProfile phaseScope(profiling::Scope::EnemyAiFiring, true);
            RunFiringPhase(state, enemy, perception, view, deltaSeconds);
        }
    }

    std::vector<std::uint8_t> fullTierMask(state.world.enemies.size(), 0U);
    for (std::size_t i = 0; i < state.world.enemies.size(); ++i) {
        const EnemyTank& enemy = state.world.enemies[i];
        if (!enemy.alive) {
            continue;
        }
        gEnemyRuntimeStats.aliveCount += 1;
        if (IsInPlayerViewport(enemy.position, state, view)) {
            gEnemyRuntimeStats.visibleInViewportCount += 1;
        }
        if (enemy.simTier == EnemySimTier::Full) {
            fullTierMask[i] = 1U;
            gEnemyRuntimeStats.fullTierCount += 1;
            if (game::geometry::IsPointInUndestroyedBase(state.world, enemy.position, 1.0F)) {
                gEnemyRuntimeStats.fullTierInBaseClearanceCount += 1;
            }
        } else {
            gEnemyRuntimeStats.cheapTierCount += 1;
        }
    }
    AccumulateEnemyWindowStats(
        gEnemyRuntimeStats.aliveCount,
        gEnemyRuntimeStats.visibleInViewportCount,
        gEnemyRuntimeStats.fullTierCount);

    if (gEnemyRuntimeStats.fullTierCount >= 2) {
        game::spatial::EnemySpatialGrid spatialGrid;
        ResolveEnemyFrontalCollisions(state.world, frameStartPositions, spatialGrid, fullTierMask);
        ResolveEnemySeparation(state.world, spatialGrid, fullTierMask);
        gEnemyRuntimeWindowStats.collisionPassRuns += 1;
    } else {
        gEnemyRuntimeWindowStats.collisionPassSkips += 1;
    }

    const auto& profiler = profiling::Profiler::Instance();
    const std::uint64_t frameIndex = profiler.FrameIndex();
    if (profiler.ShouldEmitPeriodicReport() && frameIndex != gLastEnemyStatsPrintedFrame) {
        gLastEnemyStatsPrintedFrame = frameIndex;
        std::printf(
            "[ENEMY_STATS] frame=%llu alive=%d visible=%d full=%d cheap=%d fullInBase=%d pairs(frontal=%d checks=%d separation=%d resolved=%d)\n",
            static_cast<unsigned long long>(frameIndex),
            gEnemyRuntimeStats.aliveCount,
            gEnemyRuntimeStats.visibleInViewportCount,
            gEnemyRuntimeStats.fullTierCount,
            gEnemyRuntimeStats.cheapTierCount,
            gEnemyRuntimeStats.fullTierInBaseClearanceCount,
            gEnemyRuntimeStats.frontalPairsVisited,
            gEnemyRuntimeStats.frontalPairsDistanceChecks,
            gEnemyRuntimeStats.separationPairsVisited,
            gEnemyRuntimeStats.separationPairsResolved);
        if (gEnemyRuntimeWindowStats.fixedSteps > 0) {
            std::printf(
                "[ENEMY_WINDOW] steps=%llu alive[min=%d max=%d] visible[min=%d max=%d] full[min=%d max=%d] collisionPasses[runs=%llu skips=%llu]\n",
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.fixedSteps),
                gEnemyRuntimeWindowStats.minAliveCount,
                gEnemyRuntimeWindowStats.maxAliveCount,
                gEnemyRuntimeWindowStats.minVisibleCount,
                gEnemyRuntimeWindowStats.maxVisibleCount,
                gEnemyRuntimeWindowStats.minFullTierCount,
                gEnemyRuntimeWindowStats.maxFullTierCount,
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.collisionPassRuns),
                static_cast<unsigned long long>(gEnemyRuntimeWindowStats.collisionPassSkips));
            ResetEnemyWindowStats();
        }
        std::fflush(stdout);
    }
}

const EnemyRuntimeStats& GetEnemyRuntimeStats() {
    return gEnemyRuntimeStats;
}
