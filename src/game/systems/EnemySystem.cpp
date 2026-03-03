#include "game/systems/EnemySystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <vector>
#include "core/Random.h"
#include "game/systems/ProjectileSystem.h"
#include "raylib.h"

namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kEightDirectionStep = kPi / 4.0F;
constexpr float kCosThirtyDegrees = 0.8660254F;
constexpr float kDroneBaseBearingThresholdRadians = 1.3962634F;  // 80 degrees
constexpr float kDroneReturnRequiredClearRunUnits = 6.0F;
constexpr float kEnemyPlanningClearanceScale = 1.5F;
constexpr float kTorpedoObstacleScanDistanceUnits = 16.0F;
constexpr float kTorpedoMinStraightBeforeTurnUnits = 3.0F;
constexpr float kTorpedoRetreatExitClearanceUnits = 2.0F;
constexpr float kTorpedoRetreatSpeedFactor = 0.1F;
constexpr float kTorpedoImmediateObstacleDistanceUnits = 1.0F;
constexpr float kTorpedoLongPathProbeUnits = 24.0F;

float NormalizeAngle(float angleRadians) {
    const float twoPi = kPi * 2.0F;
    float normalized = std::fmod(angleRadians, twoPi);
    if (normalized < 0.0F) {
        normalized += twoPi;
    }
    return normalized;
}

float AngleDistance(float aRadians, float bRadians) {
    const float twoPi = kPi * 2.0F;
    const float a = NormalizeAngle(aRadians);
    const float b = NormalizeAngle(bRadians);
    const float diff = std::fabs(a - b);
    return std::min(diff, twoPi - diff);
}

float SignedAngleDelta(float fromRadians, float toRadians) {
    float delta = NormalizeAngle(toRadians) - NormalizeAngle(fromRadians);
    if (delta > kPi) {
        delta -= kPi * 2.0F;
    } else if (delta < -kPi) {
        delta += kPi * 2.0F;
    }
    return delta;
}

float QuantizeToEightDirections(float angleRadians) {
    constexpr float kStep = kEightDirectionStep;
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

bool IsPointInUndestroyedBase(const WorldState& world, const Vec2f& point, float clearanceUnits) {
    const float halfBase = GameplayConstants::kEnemyBaseSizeUnits * 0.5F;
    for (const EnemyBase& base : world.enemyBases) {
        if (base.destroyed) {
            continue;
        }
        const float dx = std::fabs(point.x - base.position.x);
        const float dy = std::fabs(point.y - base.position.y);
        if (dx <= halfBase + clearanceUnits && dy <= halfBase + clearanceUnits) {
            return true;
        }
    }
    return false;
}

float FreeDistanceAhead(const WorldState& world, const Vec2f& from, float headingRadians, float maxDistance, float clearanceUnits) {
    const float planningClearance =
        clearanceUnits > 0.0F ? clearanceUnits * kEnemyPlanningClearanceScale : clearanceUnits;
    const Vec2f dir = DirectionFromHeading(headingRadians);
    const bool startsInsideBase = IsPointInUndestroyedBase(world, from, planningClearance);
    const float sampleSpacing = 0.08F;
    const int steps = std::max(1, static_cast<int>(std::ceil(maxDistance / sampleSpacing)));
    for (int i = 1; i <= steps; ++i) {
        const float dist = std::min(maxDistance, static_cast<float>(i) * sampleSpacing);
        const Vec2f sample{
            .x = from.x + dir.x * dist,
            .y = from.y + dir.y * dist,
        };
        if (IsPointInWall(world, sample, planningClearance)) {
            return dist;
        }
        if (!startsInsideBase && IsPointInUndestroyedBase(world, sample, planningClearance)) {
            return dist;
        }
    }
    return maxDistance;
}

float FreeDistanceAheadWithEnemies(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    const Vec2f& from,
    float headingRadians,
    float maxDistance,
    float clearanceUnits) {
    const float staticObstacleDistance = FreeDistanceAhead(world, from, headingRadians, maxDistance, clearanceUnits);
    const float probeDistance = std::min(maxDistance, staticObstacleDistance);
    const Vec2f dir = DirectionFromHeading(headingRadians);
    const float sampleSpacing = 0.08F;
    const int steps = std::max(1, static_cast<int>(std::ceil(probeDistance / sampleSpacing)));
    for (int i = 1; i <= steps; ++i) {
        const float dist = std::min(probeDistance, static_cast<float>(i) * sampleSpacing);
        const Vec2f sample{
            .x = from.x + dir.x * dist,
            .y = from.y + dir.y * dist,
        };
        for (int enemyIndex = 0; enemyIndex < static_cast<int>(enemies.size()); ++enemyIndex) {
            if (enemyIndex == selfIndex) {
                continue;
            }
            const EnemyTank& other = enemies[static_cast<std::size_t>(enemyIndex)];
            if (!other.alive) {
                continue;
            }
            if (Distance(sample, other.position) < GameplayConstants::kEnemyPreferredSeparationUnits) {
                return dist;
            }
        }
    }
    return probeDistance;
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
    const bool startsInsideBase = IsPointInUndestroyedBase(world, from, clearanceUnits);
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= 0.001F) {
        return IsPointInWall(world, to, clearanceUnits) ||
            (!startsInsideBase && IsPointInUndestroyedBase(world, to, clearanceUnits));
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
        if (!startsInsideBase && IsPointInUndestroyedBase(world, sample, clearanceUnits)) {
            return true;
        }
    }
    return false;
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
        const float freeDist = FreeDistanceAhead(
            world,
            origin,
            candidate,
            requiredDistance + 2.0F,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
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

    std::vector<bool> occupied{};
    BuildEnemyOccupancy(state, enemyIndex, occupied);
    occupied[static_cast<std::size_t>(startIndex)] = false;
    occupied[static_cast<std::size_t>(goalIndex)] = false;

    std::vector<AStarNode> nodes(static_cast<std::size_t>(totalCells));
    std::priority_queue<OpenNode, std::vector<OpenNode>, OpenNodeCompare> openSet{};
    nodes[static_cast<std::size_t>(startIndex)].g = 0.0F;
    nodes[static_cast<std::size_t>(startIndex)].f = HeuristicManhattan(startX, startY, goalX, goalY);
    nodes[static_cast<std::size_t>(startIndex)].open = true;
    openSet.push(OpenNode{.index = startIndex, .f = nodes[static_cast<std::size_t>(startIndex)].f});

    const std::array<int, 4> dx{1, -1, 0, 0};
    const std::array<int, 4> dy{0, 0, 1, -1};
    bool found = false;
    while (!openSet.empty()) {
        const OpenNode top = openSet.top();
        openSet.pop();
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
            openSet.push(OpenNode{.index = ni, .f = neighbor.f});
        }
    }

    if (!found) {
        enemy.pathWaypointCount = 0;
        enemy.pathWaypointIndex = 0;
        return false;
    }

    std::vector<int> pathCells{};
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
        const bool turnPoint = (i == 1) || (stepX != lastStepX) || (stepY != lastStepY) || (i == static_cast<int>(pathCells.size()) - 1);
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
    constexpr float kMinRandomTargetDistanceUnits = 24.0F;
    constexpr int kMaxTargetAttempts = 24;
    for (int attempt = 0; attempt < kMaxTargetAttempts; ++attempt) {
        const Vec2f randomTarget = RandomMazePoint(state.world, random);
        if (Distance(randomTarget, enemy.position) < kMinRandomTargetDistanceUnits) {
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
        const float clearDistance = FreeDistanceAhead(
            world,
            enemy.position,
            candidate,
            kDroneReturnRequiredClearRunUnits,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
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
        const float clearDistance = FreeDistanceAhead(
            world,
            self.position,
            candidateHeading,
            GameplayConstants::kEnemyRequiredClearRunUnits + 0.5F,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
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
        const float clearDist = FreeDistanceAhead(
            world,
            enemy.position,
            candidate,
            kTorpedoLongPathProbeUnits,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
        if (clearDist > bestClear) {
            bestClear = clearDist;
            bestHeading = candidate;
        }
    }
    return QuantizeToEightDirections(bestHeading);
}

float StepHeadingByFortyFiveToward(float currentHeading, float desiredHeading) {
    const float signedDelta = SignedAngleDelta(currentHeading, desiredHeading);
    if (std::fabs(signedDelta) <= 0.0001F) {
        return QuantizeToEightDirections(currentHeading);
    }
    const float step = signedDelta > 0.0F ? kEightDirectionStep : -kEightDirectionStep;
    return QuantizeToEightDirections(currentHeading + step);
}

float SelectTorpedoMoveHeading(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    EnemyTank& enemy,
    bool playerDetected,
    float directHeadingToPlayer,
    bool& startRetreat) {
    startRetreat = false;
    const float straightHeading = QuantizeToEightDirections(enemy.headingRadians);
    const float straightClearWithEnemies = FreeDistanceAheadWithEnemies(
        world,
        enemies,
        selfIndex,
        enemy.position,
        straightHeading,
        kTorpedoObstacleScanDistanceUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
    const float leftHeading = QuantizeToEightDirections(straightHeading - kEightDirectionStep);
    const float rightHeading = QuantizeToEightDirections(straightHeading + kEightDirectionStep);
    const float leftClear = FreeDistanceAheadWithEnemies(
        world,
        enemies,
        selfIndex,
        enemy.position,
        leftHeading,
        kTorpedoObstacleScanDistanceUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
    const float rightClear = FreeDistanceAheadWithEnemies(
        world,
        enemies,
        selfIndex,
        enemy.position,
        rightHeading,
        kTorpedoObstacleScanDistanceUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits);

    if (straightClearWithEnemies < kTorpedoImmediateObstacleDistanceUnits &&
        leftClear < kTorpedoImmediateObstacleDistanceUnits &&
        rightClear < kTorpedoImmediateObstacleDistanceUnits) {
        startRetreat = true;
        return straightHeading;
    }

    const bool canTurnNow =
        enemy.torpedoStraightDistanceSinceTurnUnits >= kTorpedoMinStraightBeforeTurnUnits;
    if (straightClearWithEnemies < kTorpedoObstacleScanDistanceUnits && canTurnNow) {
        if (leftClear > straightClearWithEnemies || rightClear > straightClearWithEnemies) {
            const bool takeLeft = leftClear >= rightClear;
            enemy.torpedoStraightDistanceSinceTurnUnits = 0.0F;
            return takeLeft ? leftHeading : rightHeading;
        }
    }

    if (playerDetected && canTurnNow) {
        const float stepped = StepHeadingByFortyFiveToward(straightHeading, directHeadingToPlayer);
        if (stepped != straightHeading) {
            const float steppedClear = FreeDistanceAheadWithEnemies(
                world,
                enemies,
                selfIndex,
                enemy.position,
                stepped,
                kTorpedoObstacleScanDistanceUnits,
                GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
            if (steppedClear >= straightClearWithEnemies) {
                enemy.torpedoStraightDistanceSinceTurnUnits = 0.0F;
                return stepped;
            }
        }
    }

    return straightHeading;
}

void EnterTorpedoRotateMode(const WorldState& world, EnemyTank& enemy, Random& random) {
    enemy.torpedoMoveMode = TorpedoMoveMode::Rotate;
    enemy.watchRotateDirection = RandomRotateDirection(random);
    enemy.torpedoRotateTargetHeadingRadians = SelectBestLongStraightHeading(world, enemy);
}

float UpdateTorpedoRotateHeading(EnemyTank& enemy, float deltaSeconds) {
    const float rotateStep =
        (kPi * 2.0F / GameplayConstants::kSlowRotateFullTurnSeconds) * deltaSeconds;
    float heading = NormalizeAngle(
        enemy.headingRadians + static_cast<float>(enemy.watchRotateDirection) * rotateStep);
    if (AngleDistance(heading, enemy.torpedoRotateTargetHeadingRadians) <= rotateStep + 0.0001F) {
        heading = QuantizeToEightDirections(enemy.torpedoRotateTargetHeadingRadians);
        enemy.torpedoMoveMode = TorpedoMoveMode::Move;
        enemy.torpedoStraightDistanceSinceTurnUnits = 0.0F;
    }
    return heading;
}

float SelectScoutHeadingWithFallback(
    const WorldState& world,
    const EnemyTank& enemy,
    bool allowNinetyTurns,
    bool& shouldRotate) {
    const float lookahead = FreeDistanceAhead(
        world,
        enemy.position,
        enemy.headingRadians,
        GameplayConstants::kEnemyLookaheadObstacleUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
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
    float bestDistance = -1.0F;
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
        float nearest = std::numeric_limits<float>::infinity();
        for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
            if (i == selfIndex) {
                continue;
            }
            const EnemyTank& other = enemies[static_cast<std::size_t>(i)];
            if (!other.alive) {
                continue;
            }
            nearest = std::min(nearest, Distance(candidate, other.position));
        }
        if (nearest > bestDistance) {
            bestDistance = nearest;
            chosenHeading = candidateHeading;
            candidatePosition = candidate;
            found = true;
        }
    }
    return found && bestDistance >= GameplayConstants::kEnemyPreferredSeparationUnits;
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

void ResolveEnemySeparation(WorldState& world) {
    for (int i = 0; i < static_cast<int>(world.enemies.size()); ++i) {
        EnemyTank& a = world.enemies[static_cast<std::size_t>(i)];
        if (!a.alive) {
            continue;
        }
        for (int j = i + 1; j < static_cast<int>(world.enemies.size()); ++j) {
            EnemyTank& b = world.enemies[static_cast<std::size_t>(j)];
            if (!b.alive) {
                continue;
            }

            const float dist = Distance(a.position, b.position);
            if (dist <= GameplayConstants::kEnemyMutualKillDistanceUnits) {
                a.alive = false;
                b.alive = false;
                DecrementOriginBaseAliveCount(world, a);
                DecrementOriginBaseAliveCount(world, b);
                continue;
            }
            if (dist >= GameplayConstants::kEnemyPreferredSeparationUnits) {
                continue;
            }

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
            const bool aBlocked = IsPointInWall(world, movedA, GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
            const bool bBlocked = IsPointInWall(world, movedB, GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
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
        }
    }
}

void ResolveEnemyFrontalCollisions(WorldState& world, const std::vector<Vec2f>& frameStartPositions) {
    for (int i = 0; i < static_cast<int>(world.enemies.size()); ++i) {
        EnemyTank& a = world.enemies[static_cast<std::size_t>(i)];
        if (!a.alive) {
            continue;
        }
        const Vec2f aStart = frameStartPositions[static_cast<std::size_t>(i)];
        const Vec2f aEnd = a.position;
        for (int j = i + 1; j < static_cast<int>(world.enemies.size()); ++j) {
            EnemyTank& b = world.enemies[static_cast<std::size_t>(j)];
            if (!b.alive) {
                continue;
            }
            const Vec2f bStart = frameStartPositions[static_cast<std::size_t>(j)];
            const Vec2f bEnd = b.position;
            const float distance = SegmentToSegmentDistance(aStart, aEnd, bStart, bEnd);
            if (distance <= GameplayConstants::kEnemyMutualKillDistanceUnits) {
                a.alive = false;
                b.alive = false;
                DecrementOriginBaseAliveCount(world, a);
                DecrementOriginBaseAliveCount(world, b);
            }
        }
    }
}
}  // namespace

void UpdateEnemySystem(GameState& state, const AppConfig& config, float deltaSeconds) {
    static Random random(static_cast<std::uint32_t>(GetTime() * 1000.0));
    const bool playerInvisible = state.menuSettings.invisibility;
    std::vector<Vec2f> frameStartPositions{};
    frameStartPositions.reserve(state.world.enemies.size());
    for (const EnemyTank& enemy : state.world.enemies) {
        frameStartPositions.push_back(enemy.position);
    }

    for (int enemyIndex = 0; enemyIndex < static_cast<int>(state.world.enemies.size()); ++enemyIndex) {
        EnemyTank& enemy = state.world.enemies[static_cast<std::size_t>(enemyIndex)];
        if (!enemy.alive) {
            continue;
        }
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
        const Vec2f toPlayer{
            .x = state.world.player.position.x - enemy.position.x,
            .y = state.world.player.position.y - enemy.position.y,
        };
        const Vec2f toPlayerNormalized = NormalizeOrZero(toPlayer);
        const float distanceToPlayerSq = DistanceSq(enemy.position, state.world.player.position);
        const float distanceToPlayer = std::sqrt(distanceToPlayerSq);
        const bool playerInAggroRange =
            !playerInvisible &&
            distanceToPlayerSq <=
            (GameplayConstants::kEnemyAggroRangeUnits * GameplayConstants::kEnemyAggroRangeUnits);
        const bool playerObscured =
            playerInvisible ||
            IsSegmentObscuredByWall(state.world, enemy.position, state.world.player.position);
        const bool assassinHasLineOfSight =
            enemy.type == EnemyType::Assassin && playerInAggroRange && !playerObscured;
        float movementHeading = QuantizeToEightDirections(enemy.headingRadians);
        float speed = EnemySpeed(enemy.type, enemy.subtype, assassinHasLineOfSight);
        bool preserveContinuousHeading = false;

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
                const float clearDistance = FreeDistanceAhead(
                    state.world,
                    enemy.position,
                    movementHeading,
                    GameplayConstants::kEnemyRequiredClearRunUnits + 0.5F,
                    GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
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
            const bool playerDetected =
                !playerInvisible &&
                !playerObscured &&
                distanceToPlayer <= GameplayConstants::kTorpedoDetectRangeUnits;
            const float directHeading = std::atan2(toPlayer.x, -toPlayer.y);
            if (enemy.torpedoMoveMode == TorpedoMoveMode::Retreat) {
                movementHeading = QuantizeToEightDirections(enemy.headingRadians);
                speed = -std::abs(speed) * kTorpedoRetreatSpeedFactor;
                const float forwardClear = FreeDistanceAheadWithEnemies(
                    state.world,
                    state.world.enemies,
                    enemyIndex,
                    enemy.position,
                    enemy.headingRadians,
                    kTorpedoObstacleScanDistanceUnits,
                    GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
                if (enemy.torpedoRetreatMovedUnits >= kTorpedoRetreatExitClearanceUnits &&
                    forwardClear >= kTorpedoRetreatExitClearanceUnits) {
                    speed = 0.0F;
                    EnterTorpedoRotateMode(state.world, enemy, random);
                }
            } else if (enemy.torpedoMoveMode == TorpedoMoveMode::Rotate) {
                speed = 0.0F;
                preserveContinuousHeading = true;
                movementHeading = UpdateTorpedoRotateHeading(enemy, deltaSeconds);
            } else {
                bool startRetreat = false;
                movementHeading = SelectTorpedoMoveHeading(
                    state.world,
                    state.world.enemies,
                    enemyIndex,
                    enemy,
                    playerDetected,
                    directHeading,
                    startRetreat);
                if (startRetreat) {
                    enemy.torpedoMoveMode = TorpedoMoveMode::Retreat;
                    enemy.torpedoRetreatMovedUnits = 0.0F;
                    movementHeading = QuantizeToEightDirections(enemy.headingRadians);
                    speed = -std::abs(speed) * kTorpedoRetreatSpeedFactor;
                }
            }
        } else if (enemy.type == EnemyType::Hunter) {
            const bool canChase =
                !playerInvisible &&
                !playerObscured &&
                distanceToPlayer <= GameplayConstants::kHunterDetectRangeUnits;
            if (canChase) {
                enemy.aiMode = EnemyAiMode::Chase;
                enemy.aiModeElapsedSeconds = 0.0F;
            } else if (enemy.aiMode == EnemyAiMode::Chase) {
                enemy.aiMode = EnemyAiMode::Scout;
                enemy.aiModeElapsedSeconds = 0.0F;
            }

            if (enemy.aiMode == EnemyAiMode::Chase) {
                if (distanceToPlayer < GameplayConstants::kHunterMinDistanceUnits) {
                    movementHeading = QuantizeToEightDirections(std::atan2(-toPlayer.x, toPlayer.y));
                } else if (distanceToPlayer > GameplayConstants::kHunterMaxDistanceUnits) {
                    movementHeading = QuantizeToEightDirections(std::atan2(toPlayer.x, -toPlayer.y));
                } else {
                    speed = 0.0F;
                }
            } else if (enemy.aiMode == EnemyAiMode::Rotate) {
                speed = 0.0F;
                preserveContinuousHeading = true;
                movementHeading = NormalizeAngle(
                    enemy.headingRadians + (kPi * 2.0F / GameplayConstants::kSlowRotateFullTurnSeconds) * deltaSeconds);
                const float clearDistance = FreeDistanceAhead(
                    state.world,
                    enemy.position,
                    movementHeading,
                    GameplayConstants::kEnemyRequiredClearRunUnits + 0.5F,
                    GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
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
            if (!playerInvisible && distanceToPlayer < GameplayConstants::kAssassinMinDistanceUnits) {
                speed = 0.0F;
                enemy.pathWaypointCount = 0;
                enemy.pathWaypointIndex = 0;
            } else {
                const float obstacleAhead = FreeDistanceAhead(
                    state.world,
                    enemy.position,
                    enemy.headingRadians,
                    2.0F,
                    GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
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
        const Vec2f previousPosition = enemy.position;

        // Keep enemies from overlapping: turn first, stop second.
        float minDistanceToOthers = std::numeric_limits<float>::infinity();
        float currentMinDistanceToOthers = std::numeric_limits<float>::infinity();
        for (int i = 0; i < static_cast<int>(state.world.enemies.size()); ++i) {
            if (i == enemyIndex) {
                continue;
            }
            const EnemyTank& other = state.world.enemies[static_cast<std::size_t>(i)];
            if (!other.alive) {
                continue;
            }
            currentMinDistanceToOthers = std::min(currentMinDistanceToOthers, Distance(enemy.position, other.position));
            minDistanceToOthers = std::min(minDistanceToOthers, Distance(candidatePosition, other.position));
        }
        const bool makingSeparationProgress =
            minDistanceToOthers > currentMinDistanceToOthers + 0.001F;
        if (std::fabs(speed) > 0.0F &&
            minDistanceToOthers < GameplayConstants::kEnemyPreferredSeparationUnits &&
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
            }
        } else if (enemy.type == EnemyType::Torpedo && enemy.torpedoMoveMode == TorpedoMoveMode::Retreat) {
            const float movedDistance = Distance(enemy.position, previousPosition);
            if (movedDistance > 0.0001F) {
                enemy.torpedoRetreatMovedUnits += movedDistance;
            }
        }

        enemy.fireCooldownSeconds -= deltaSeconds;
        const bool enemyVisibleInViewport = IsInPlayerViewport(enemy.position, state, config);
        bool canFireTypeSpecific = true;
        if (enemy.type == EnemyType::Torpedo) {
            canFireTypeSpecific = PlayerAheadForTorpedo(enemy, toPlayerNormalized);
        }
        if (state.world.player.alive &&
            enemy.fireCooldownSeconds <= 0.0F &&
            enemyVisibleInViewport &&
            !playerObscured &&
            canFireTypeSpecific &&
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

    ResolveEnemyFrontalCollisions(state.world, frameStartPositions);
    ResolveEnemySeparation(state.world);
}
