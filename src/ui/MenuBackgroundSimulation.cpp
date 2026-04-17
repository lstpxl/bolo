#include "ui/MenuBackgroundSimulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <stack>
#include <vector>
#include "core/AngleMath.h"
#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"
#include "game/navigation/AdjacentCellSegmentPlanner.h"
#include "game/systems/EnemySystemCollision.h"
#include "game/systems/EnemySystemUncouple.h"

namespace {
constexpr float kDestinationReachedThresholdUnits = 0.2F;
constexpr float kCameraMoveSpeedUnitsPerSecond = 4.5F;
constexpr float kEnemyPreferredSeparationUnits = GameplayConstants::kEnemyPreferredSeparationUnits;
constexpr float kMenuAvoidMinCenterDistSq =
    (kEnemyPreferredSeparationUnits + 0.05F) * (kEnemyPreferredSeparationUnits + 0.05F);
constexpr float kMenuAvoidMaxRangeSq = 14.0F * 14.0F;
constexpr float kMenuOncomingHeadingDot = -0.22F;
constexpr float kMenuFrontConeMinCos = 0.4F;
constexpr float kMenuLaneBiasStrength = 0.5F;
constexpr float kMenuIntrusionLookahead = 10.0F;
constexpr float kMenuIntrusionHalfWidth = 1.05F;
constexpr float kMenuIntrusionBiasStrength = 0.7F;
constexpr float kMenuMaxSteerRadPerSec = 1.15F;
constexpr float kMenuWallProbeDistance = 6.0F;
constexpr float kMenuEnemyProbeRadius = 1.5F;
constexpr float kMenuEnemyProbeRadiusSq = kMenuEnemyProbeRadius * kMenuEnemyProbeRadius;
constexpr float kMenuSideProbeAlong = 2.5F;
constexpr float kMenuSideProbeAcross = 1.1F;

struct CellCoord {
    int x = 0;
    int y = 0;
};

int ToIndex(const MazeState& maze, int x, int y) {
    return y * maze.widthCells + x;
}

bool IsInBounds(const MazeState& maze, int x, int y) {
    return x >= 0 && y >= 0 && x < maze.widthCells && y < maze.heightCells;
}

void RemoveWallBetween(MazeState& maze, const CellCoord& a, const CellCoord& b) {
    MazeCell& cellA = maze.cells[static_cast<std::size_t>(ToIndex(maze, a.x, a.y))];
    MazeCell& cellB = maze.cells[static_cast<std::size_t>(ToIndex(maze, b.x, b.y))];
    if (a.x == b.x) {
        if (a.y < b.y) {
            cellA.southWall = false;
            cellB.northWall = false;
        } else {
            cellA.northWall = false;
            cellB.southWall = false;
        }
        return;
    }
    if (a.x < b.x) {
        cellA.eastWall = false;
        cellB.westWall = false;
    } else {
        cellA.westWall = false;
        cellB.eastWall = false;
    }
}

Vec2f VecTo(const Vec2f& from, const Vec2f& to) {
    return Vec2f{.x = to.x - from.x, .y = to.y - from.y};
}

float VecLength(const Vec2f& value) {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

float HeadingFromDirectionUnit(const Vec2f& dir) {
    return std::atan2(dir.x, -dir.y);
}

}  // namespace

void MenuBackgroundSimulation::Initialize() {
    maze_ = MazeState{};
    maze_.widthCells = kMazeWidthCells;
    maze_.heightCells = kMazeHeightCells;
    maze_.cellSizeUnits = GameplayConstants::kMazeCellSizeUnits;

    GenerateMaze();
    plannerWorld_ = WorldState{};
    plannerWorld_.maze = maze_;
    plannerCellCache_ = game::navigation::CellCoordCache{};
    plannerCellCache_.ConfigureFromMaze(maze_);
    SpawnInitialEnemies();
    const int cameraStartHash = random_.NextInt(0, maze_.widthCells * maze_.heightCells - 1);
    cameraMoverPosition_ = CellCenterFromHash(cameraStartHash);
    cameraRuntime_ = CameraRuntimeState{};
    PickNewDestinationAndPathFromPosition(
        cameraMoverPosition_,
        cameraRuntime_.destinationCellHash,
        cameraRuntime_.pathCellHashes,
        cameraRuntime_.pathCellIndex);
    initialized_ = true;
}

void MenuBackgroundSimulation::Reset() {
    initialized_ = false;
    maze_ = MazeState{};
    enemies_.clear();
    enemyRuntime_.clear();
    cameraMoverPosition_ = Vec2f{.x = 0.0F, .y = 0.0F};
    cameraRuntime_ = CameraRuntimeState{};
}

void MenuBackgroundSimulation::Update(float deltaSeconds) {
    if (!initialized_) {
        return;
    }
    EnsureEnemyCount();
    frameStartPositions_.assign(enemies_.size(), Vec2f{.x = 0.0F, .y = 0.0F});
    for (std::size_t i = 0; i < enemies_.size(); ++i) {
        frameStartPositions_[i] = enemies_[i].position;
    }

    for (std::size_t i = 0; i < enemies_.size() && i < enemyRuntime_.size(); ++i) {
        UpdateEnemy(i, deltaSeconds);
    }
    ResolveEnemyCollisionsFromGameplay();
    UpdateCameraMover(deltaSeconds);
}

bool MenuBackgroundSimulation::IsInitialized() const {
    return initialized_;
}

const MazeState& MenuBackgroundSimulation::Maze() const {
    return maze_;
}

const std::vector<EnemyTank>& MenuBackgroundSimulation::Enemies() const {
    return enemies_;
}

Vec2f MenuBackgroundSimulation::CameraTarget() const {
    if (cameraRuntime_.pathCellHashes.empty()) {
        const float worldW = static_cast<float>(maze_.widthCells * maze_.cellSizeUnits);
        const float worldH = static_cast<float>(maze_.heightCells * maze_.cellSizeUnits);
        return Vec2f{.x = worldW * 0.5F, .y = worldH * 0.5F};
    }
    return cameraMoverPosition_;
}

void MenuBackgroundSimulation::GenerateMaze() {
    const int totalCells = maze_.widthCells * maze_.heightCells;
    maze_.cells.assign(static_cast<std::size_t>(totalCells), MazeCell{});

    std::vector<bool> visited(static_cast<std::size_t>(totalCells), false);
    std::stack<CellCoord> stack{};
    const CellCoord start{
        .x = random_.NextInt(0, maze_.widthCells - 1),
        .y = random_.NextInt(0, maze_.heightCells - 1),
    };
    stack.push(start);
    visited[static_cast<std::size_t>(ToIndex(maze_, start.x, start.y))] = true;

    constexpr std::array<CellCoord, 4> offsets{{
        {.x = 0, .y = -1},
        {.x = 1, .y = 0},
        {.x = 0, .y = 1},
        {.x = -1, .y = 0},
    }};

    while (!stack.empty()) {
        const CellCoord current = stack.top();
        std::vector<CellCoord> unvisitedNeighbors{};
        unvisitedNeighbors.reserve(4);
        for (const CellCoord& offset : offsets) {
            const int nx = current.x + offset.x;
            const int ny = current.y + offset.y;
            if (!IsInBounds(maze_, nx, ny)) {
                continue;
            }
            const int nextIndex = ToIndex(maze_, nx, ny);
            if (!visited[static_cast<std::size_t>(nextIndex)]) {
                unvisitedNeighbors.push_back(CellCoord{.x = nx, .y = ny});
            }
        }
        if (unvisitedNeighbors.empty()) {
            stack.pop();
            continue;
        }
        const int picked = random_.NextInt(0, static_cast<int>(unvisitedNeighbors.size()) - 1);
        const CellCoord next = unvisitedNeighbors[static_cast<std::size_t>(picked)];
        RemoveWallBetween(maze_, current, next);
        visited[static_cast<std::size_t>(ToIndex(maze_, next.x, next.y))] = true;
        stack.push(next);
    }

    const int horizontalConnections = maze_.widthCells * (maze_.heightCells - 1);
    const int verticalConnections = (maze_.widthCells - 1) * maze_.heightCells;
    const int totalInternalEdges = horizontalConnections + verticalConnections;
    const float densityT = static_cast<float>(kMazeDensity - 1) / 4.0F;
    const float targetWallSegmentsPer100Cells =
        GameplayConstants::kDensity1WallsPer100Cells +
        (GameplayConstants::kDensity5WallsPer100Cells - GameplayConstants::kDensity1WallsPer100Cells) * densityT;
    int targetInternalWalls = static_cast<int>(
        std::round(targetWallSegmentsPer100Cells * static_cast<float>(totalCells) / 100.0F));
    targetInternalWalls = std::clamp(targetInternalWalls, 0, totalInternalEdges);
    int targetOpenEdges = std::max(totalInternalEdges - targetInternalWalls, totalCells - 1);
    int extraOpeningsNeeded = targetOpenEdges - (totalCells - 1);
    if (extraOpeningsNeeded <= 0) {
        return;
    }

    struct MazeEdge {
        CellCoord a{};
        CellCoord b{};
    };
    std::vector<MazeEdge> edges{};
    edges.reserve(static_cast<std::size_t>(totalInternalEdges));
    for (int y = 0; y < maze_.heightCells; ++y) {
        for (int x = 0; x < maze_.widthCells; ++x) {
            if (x + 1 < maze_.widthCells) {
                edges.push_back(MazeEdge{
                    .a = CellCoord{.x = x, .y = y},
                    .b = CellCoord{.x = x + 1, .y = y},
                });
            }
            if (y + 1 < maze_.heightCells) {
                edges.push_back(MazeEdge{
                    .a = CellCoord{.x = x, .y = y},
                    .b = CellCoord{.x = x, .y = y + 1},
                });
            }
        }
    }

    for (std::size_t i = 0; i < edges.size() && extraOpeningsNeeded > 0; ++i) {
        const int swapIndex = random_.NextInt(static_cast<int>(i), static_cast<int>(edges.size() - 1));
        std::swap(edges[i], edges[static_cast<std::size_t>(swapIndex)]);
        const MazeEdge& edge = edges[i];
        const MazeCell& a = maze_.cells[static_cast<std::size_t>(ToIndex(maze_, edge.a.x, edge.a.y))];
        const MazeCell& b = maze_.cells[static_cast<std::size_t>(ToIndex(maze_, edge.b.x, edge.b.y))];
        const bool alreadyOpen = edge.a.x == edge.b.x ? (!a.southWall && !b.northWall) : (!a.eastWall && !b.westWall);
        if (alreadyOpen) {
            continue;
        }
        RemoveWallBetween(maze_, edge.a, edge.b);
        --extraOpeningsNeeded;
    }
}

void MenuBackgroundSimulation::SpawnInitialEnemies() {
    enemies_.clear();
    enemyRuntime_.clear();
    enemies_.reserve(kTargetEnemyCount);
    enemyRuntime_.reserve(kTargetEnemyCount);

    for (int i = 0; i < kTargetEnemyCount; ++i) {
        const int spawnHash = random_.NextInt(0, maze_.widthCells * maze_.heightCells - 1);
        const Vec2f spawnPosition = CellCenterFromHash(spawnHash);
        const EnemyType type = RandomEnemyType();
        EnemyTank enemy{};
        enemy.alive = true;
        enemy.position = spawnPosition;
        enemy.headingRadians = 0.0F;
        enemy.type = type;
        enemy.subtype = EnemySubtype::Advanced;
        enemy.aiMode = EnemyAiMode::Wander;
        enemy.simTier = EnemySimTier::Full;
        enemies_.push_back(enemy);

        EnemyRuntimeState runtime{};
        PickNewDestinationAndPath(enemy, runtime);
        enemyRuntime_.push_back(runtime);
    }
}

void MenuBackgroundSimulation::EnsureEnemyCount() {
    while (static_cast<int>(enemies_.size()) < kTargetEnemyCount) {
        const int spawnHash = random_.NextInt(0, maze_.widthCells * maze_.heightCells - 1);
        const Vec2f spawnPosition = CellCenterFromHash(spawnHash);
        const EnemyType type = RandomEnemyType();
        EnemyTank enemy{};
        enemy.alive = true;
        enemy.position = spawnPosition;
        enemy.headingRadians = 0.0F;
        enemy.type = type;
        enemy.subtype = EnemySubtype::Advanced;
        enemy.aiMode = EnemyAiMode::Wander;
        enemy.simTier = EnemySimTier::Full;
        enemies_.push_back(enemy);

        EnemyRuntimeState runtime{};
        PickNewDestinationAndPath(enemy, runtime);
        enemyRuntime_.push_back(runtime);
    }
}

float MenuBackgroundSimulation::WallFreeAheadFrom(const Vec2f& from, float headingRadians) const {
    return game::geometry::FreeDistanceAheadGridWallsOnly(
        plannerWorld_,
        from,
        headingRadians,
        kMenuWallProbeDistance,
        GameplayConstants::kWallClearanceForAvoidance);
}

int MenuBackgroundSimulation::CountEnemiesNearProbe(
    std::size_t selfIndex,
    const Vec2f& probeCenter,
    float radiusSq) const {
    int count = 0;
    for (std::size_t j = 0; j < enemies_.size(); ++j) {
        if (j == selfIndex || !enemies_[j].alive) {
            continue;
        }
        const Vec2f d = VecTo(probeCenter, frameStartPositions_[j]);
        const float dsq = d.x * d.x + d.y * d.y;
        if (dsq < radiusSq) {
            ++count;
        }
    }
    return count;
}

Vec2f MenuBackgroundSimulation::ComputeMenuSteeringBias(std::size_t selfIndex, const Vec2f& pathForwardUnit) const {
    if (!enemies_[selfIndex].alive || selfIndex >= frameStartPositions_.size()) {
        return Vec2f{.x = 0.0F, .y = 0.0F};
    }
    const Vec2f selfPos = frameStartPositions_[selfIndex];
    const Vec2f pathFwd = pathForwardUnit;
    const float pathLenSq = pathFwd.x * pathFwd.x + pathFwd.y * pathFwd.y;
    if (pathLenSq < 1.0e-6F) {
        return Vec2f{.x = 0.0F, .y = 0.0F};
    }

    const Vec2f right{.x = pathFwd.y, .y = -pathFwd.x};
    Vec2f laneBias{.x = 0.0F, .y = 0.0F};
    Vec2f intrusionBias{.x = 0.0F, .y = 0.0F};

    for (std::size_t j = 0; j < enemies_.size(); ++j) {
        if (j == selfIndex || !enemies_[j].alive) {
            continue;
        }
        const Vec2f otherPos = frameStartPositions_[j];
        const Vec2f rel = VecTo(selfPos, otherPos);
        const float distSq = rel.x * rel.x + rel.y * rel.y;
        if (distSq < 1.0e-5F || distSq > kMenuAvoidMaxRangeSq) {
            continue;
        }
        const float dist = std::sqrt(distSq);
        const float invDist = 1.0F / dist;
        const Vec2f relN{.x = rel.x * invDist, .y = rel.y * invDist};
        const float aheadDot = relN.x * pathFwd.x + relN.y * pathFwd.y;

        if (distSq >= kMenuAvoidMinCenterDistSq && aheadDot >= kMenuFrontConeMinCos) {
            const Vec2f otherFwd = core::angle::DirectionFromHeading(enemies_[j].headingRadians);
            const float oppDot = pathFwd.x * otherFwd.x + pathFwd.y * otherFwd.y;
            if (oppDot <= kMenuOncomingHeadingDot) {
                const float minD = std::sqrt(kMenuAvoidMinCenterDistSq);
                const float maxD = std::sqrt(kMenuAvoidMaxRangeSq);
                const float t = (dist - minD) / std::max(0.0001F, maxD - minD);
                const float falloff = std::clamp(1.0F - t, 0.0F, 1.0F) * aheadDot;
                laneBias.x += right.x * falloff * kMenuLaneBiasStrength;
                laneBias.y += right.y * falloff * kMenuLaneBiasStrength;
            }
        }

        const float along = rel.x * pathFwd.x + rel.y * pathFwd.y;
        if (along > 0.15F && along < kMenuIntrusionLookahead) {
            const float perpSq = distSq - along * along;
            const float perp = std::sqrt(std::max(0.0F, perpSq));
            if (perp < kMenuIntrusionHalfWidth) {
                const float crossZ = pathFwd.x * rel.y - pathFwd.y * rel.x;
                const Vec2f steerRight = right;
                const Vec2f steerLeft{.x = -right.x, .y = -right.y};
                const float depth = 1.0F - perp / std::max(0.0001F, kMenuIntrusionHalfWidth);
                const float alongW = 1.0F - along / kMenuIntrusionLookahead;
                const float w = depth * alongW * kMenuIntrusionBiasStrength;

                Vec2f preferred = crossZ >= 0.0F ? steerRight : steerLeft;
                if (std::fabs(crossZ) < 0.08F) {
                    const Vec2f probeL{
                        .x = selfPos.x + pathFwd.x * kMenuSideProbeAlong + steerLeft.x * kMenuSideProbeAcross,
                        .y = selfPos.y + pathFwd.y * kMenuSideProbeAlong + steerLeft.y * kMenuSideProbeAcross,
                    };
                    const Vec2f probeR{
                        .x = selfPos.x + pathFwd.x * kMenuSideProbeAlong + steerRight.x * kMenuSideProbeAcross,
                        .y = selfPos.y + pathFwd.y * kMenuSideProbeAlong + steerRight.y * kMenuSideProbeAcross,
                    };
                    const float hL = HeadingFromDirectionUnit(steerLeft);
                    const float hR = HeadingFromDirectionUnit(steerRight);
                    const float wallL = WallFreeAheadFrom(probeL, hL);
                    const float wallR = WallFreeAheadFrom(probeR, hR);
                    const int enL = CountEnemiesNearProbe(selfIndex, probeL, kMenuEnemyProbeRadiusSq);
                    const int enR = CountEnemiesNearProbe(selfIndex, probeR, kMenuEnemyProbeRadiusSq);
                    const float scoreL = wallL - static_cast<float>(enL) * 0.35F;
                    const float scoreR = wallR - static_cast<float>(enR) * 0.35F;
                    preferred = scoreL >= scoreR ? steerLeft : steerRight;
                }
                intrusionBias.x += preferred.x * w;
                intrusionBias.y += preferred.y * w;
            }
        }
    }

    const float hLaneRight = HeadingFromDirectionUnit(right);
    const float clearAlongLaneRight = WallFreeAheadFrom(selfPos, hLaneRight);
    if (clearAlongLaneRight < 1.15F) {
        const float scale = std::clamp(clearAlongLaneRight, 0.0F, 1.15F) / 1.15F;
        laneBias.x *= scale;
        laneBias.y *= scale;
    }

    return Vec2f{.x = laneBias.x + intrusionBias.x, .y = laneBias.y + intrusionBias.y};
}

void MenuBackgroundSimulation::UpdateEnemy(std::size_t selfIndex, float deltaSeconds) {
    if (selfIndex >= enemies_.size() || selfIndex >= enemyRuntime_.size()) {
        return;
    }
    EnemyTank& enemy = enemies_[selfIndex];
    EnemyRuntimeState& runtime = enemyRuntime_[selfIndex];
    if (!enemy.alive) {
        return;
    }
    if (runtime.pathCellHashes.empty() || runtime.pathCellIndex >= runtime.pathCellHashes.size()) {
        PickNewDestinationAndPath(enemy, runtime);
    }
    if (runtime.pathCellHashes.empty()) {
        return;
    }

    while (runtime.pathCellIndex + 1 < runtime.pathCellHashes.size()) {
        const int fromCellHash = CellHash(
            std::clamp(
                static_cast<int>(enemy.position.x / static_cast<float>(maze_.cellSizeUnits)),
                0,
                maze_.widthCells - 1),
            std::clamp(
                static_cast<int>(enemy.position.y / static_cast<float>(maze_.cellSizeUnits)),
                0,
                maze_.heightCells - 1));
        const int nextCellHash = runtime.pathCellHashes[runtime.pathCellIndex];
        if (runtime.segmentCount <= 0 || runtime.segmentTargetCellHash != nextCellHash) {
            if (!BuildStepSegment(enemy, runtime, fromCellHash, nextCellHash)) {
                PickNewDestinationAndPath(enemy, runtime);
                return;
            }
        }
        if (runtime.segmentIndex < runtime.segmentCount) {
            break;
        }
        runtime.segmentCount = 0;
        runtime.segmentIndex = 0;
        runtime.segmentTargetCellHash = -1;
        ++runtime.pathCellIndex;
    }

    if (runtime.segmentCount <= 0 || runtime.segmentIndex >= runtime.segmentCount) {
        PickNewDestinationAndPath(enemy, runtime);
        return;
    }

    const Vec2f target = runtime.segmentPoints[static_cast<std::size_t>(runtime.segmentIndex)];
    const Vec2f delta = VecTo(enemy.position, target);
    const float remaining = VecLength(delta);
    const float speed = EnemySpeedForType(enemy.type);
    const float maxStep = speed * deltaSeconds;
    const float step = std::min(maxStep, remaining);
    if (remaining > 0.0001F) {
        const float pathNx = delta.x / remaining;
        const float pathNy = delta.y / remaining;
        const Vec2f pathFwd{.x = pathNx, .y = pathNy};
        const Vec2f bias = ComputeMenuSteeringBias(selfIndex, pathFwd);
        Vec2f combined{
            .x = pathFwd.x + bias.x,
            .y = pathFwd.y + bias.y,
        };
        float clen = VecLength(combined);
        if (clen < 1.0e-5F) {
            combined = pathFwd;
            clen = 1.0F;
        } else {
            combined.x /= clen;
            combined.y /= clen;
        }
        const float maxTurn = kMenuMaxSteerRadPerSec * deltaSeconds;
        const float sinA = pathFwd.x * combined.y - pathFwd.y * combined.x;
        const float cosA = pathFwd.x * combined.x + pathFwd.y * combined.y;
        float angle = std::atan2(sinA, cosA);
        if (angle > maxTurn) {
            angle = maxTurn;
        } else if (angle < -maxTurn) {
            angle = -maxTurn;
        }
        const float ca = std::cos(angle);
        const float sa = std::sin(angle);
        const float nx = pathFwd.x * ca - pathFwd.y * sa;
        const float ny = pathFwd.x * sa + pathFwd.y * ca;
        enemy.position.x += nx * step;
        enemy.position.y += ny * step;
        enemy.headingRadians = std::atan2(nx, -ny);
    }
    const Vec2f afterMoveDelta = VecTo(enemy.position, target);
    if (VecLength(afterMoveDelta) <= kDestinationReachedThresholdUnits) {
        enemy.position = target;
        ++runtime.segmentIndex;
        if (runtime.segmentIndex >= runtime.segmentCount) {
            runtime.segmentCount = 0;
            runtime.segmentIndex = 0;
            runtime.segmentTargetCellHash = -1;
            if (runtime.pathCellIndex + 1 < runtime.pathCellHashes.size()) {
                ++runtime.pathCellIndex;
            }
        }
    }
}

void MenuBackgroundSimulation::PickNewDestinationAndPath(
    const EnemyTank& enemy,
    EnemyRuntimeState& runtime) {
    PickNewDestinationAndPathFromPosition(
        enemy.position,
        runtime.destinationCellHash,
        runtime.pathCellHashes,
        runtime.pathCellIndex);
    runtime.segmentTargetCellHash = -1;
    runtime.segmentCount = 0;
    runtime.segmentIndex = 0;
}

bool MenuBackgroundSimulation::BuildStepSegment(
    const EnemyTank& enemy,
    EnemyRuntimeState& runtime,
    int fromCellHash,
    int targetCellHash) {
    const game::navigation::MazeCellCoord fromCell{
        .x = fromCellHash % maze_.widthCells,
        .y = fromCellHash / maze_.widthCells,
    };
    const game::navigation::MazeCellCoord targetCell{
        .x = targetCellHash % maze_.widthCells,
        .y = targetCellHash / maze_.widthCells,
    };
    if (!game::navigation::AdjacentCellSegmentPlanner::Build(
            plannerWorld_,
            plannerCellCache_,
            fromCell,
            targetCell,
            enemy.position,
            runtime.segmentPoints,
            runtime.segmentCount)) {
        return false;
    }
    runtime.segmentTargetCellHash = targetCellHash;
    runtime.segmentIndex = 0;
    return runtime.segmentCount > 0;
}

void MenuBackgroundSimulation::PickNewDestinationAndPathFromPosition(
    const Vec2f& position,
    int& destinationCellHash,
    std::vector<int>& pathCellHashes,
    std::size_t& pathCellIndex) {
    const int startCellX = std::clamp(
        static_cast<int>(position.x / static_cast<float>(maze_.cellSizeUnits)),
        0,
        maze_.widthCells - 1);
    const int startCellY = std::clamp(
        static_cast<int>(position.y / static_cast<float>(maze_.cellSizeUnits)),
        0,
        maze_.heightCells - 1);
    const int startHash = CellHash(startCellX, startCellY);

    std::vector<int> path{};
    for (int attempts = 0; attempts < 16; ++attempts) {
        const int destHash = random_.NextInt(0, maze_.widthCells * maze_.heightCells - 1);
        if (destHash == startHash) {
            continue;
        }
        path.clear();
        if (!BuildPathAStar(startHash, destHash, path)) {
            continue;
        }
        destinationCellHash = destHash;
        pathCellHashes = path;
        pathCellIndex = pathCellHashes.size() > 1 ? 1 : 0;
        return;
    }
    destinationCellHash = startHash;
    pathCellHashes = {startHash};
    pathCellIndex = 0;
}

bool MenuBackgroundSimulation::BuildPathAStar(
    int startHash,
    int goalHash,
    std::vector<int>& outPath) const {
    outPath.clear();
    if (startHash == goalHash) {
        outPath.push_back(startHash);
        return true;
    }

    const int totalCells = maze_.widthCells * maze_.heightCells;
    const int kUnvisited = std::numeric_limits<int>::max();
    std::vector<int> gScore(static_cast<std::size_t>(totalCells), kUnvisited);
    std::vector<int> cameFrom(static_cast<std::size_t>(totalCells), -1);

    struct OpenNode {
        int hash = -1;
        int fScore = 0;
        bool operator<(const OpenNode& other) const {
            return fScore > other.fScore;
        }
    };
    std::priority_queue<OpenNode> open{};
    const CellCoord goal{
        .x = goalHash % maze_.widthCells,
        .y = goalHash / maze_.widthCells,
    };

    gScore[static_cast<std::size_t>(startHash)] = 0;
    const CellCoord start{
        .x = startHash % maze_.widthCells,
        .y = startHash / maze_.widthCells,
    };
    const int startDx = std::abs(start.x - goal.x);
    const int startDy = std::abs(start.y - goal.y);
    open.push(OpenNode{
        .hash = startHash,
        .fScore = 10 * (startDx + startDy) - 6 * std::min(startDx, startDy),
    });

    constexpr std::array<CellCoord, 8> neighbors{{
        {.x = 0, .y = -1},
        {.x = 1, .y = 0},
        {.x = 0, .y = 1},
        {.x = -1, .y = 0},
        {.x = 1, .y = -1},
        {.x = 1, .y = 1},
        {.x = -1, .y = 1},
        {.x = -1, .y = -1},
    }};

    while (!open.empty()) {
        const int currentHash = open.top().hash;
        open.pop();
        if (currentHash == goalHash) {
            std::vector<int> reversePath{};
            int walk = goalHash;
            reversePath.push_back(walk);
            while (walk != startHash) {
                walk = cameFrom[static_cast<std::size_t>(walk)];
                if (walk < 0) {
                    return false;
                }
                reversePath.push_back(walk);
            }
            outPath.assign(reversePath.rbegin(), reversePath.rend());
            return true;
        }

        const CellCoord current{
            .x = currentHash % maze_.widthCells,
            .y = currentHash / maze_.widthCells,
        };
        const int currentG = gScore[static_cast<std::size_t>(currentHash)];
        for (const CellCoord& offset : neighbors) {
            const int nx = current.x + offset.x;
            const int ny = current.y + offset.y;
            if (!IsInBounds(maze_, nx, ny)) {
                continue;
            }
            if (!CanMoveToNeighbor(current.x, current.y, offset.x, offset.y)) {
                continue;
            }
            const int nextHash = CellHash(nx, ny);
            const int stepCost = (offset.x == 0 || offset.y == 0) ? 10 : 14;
            const int tentativeG = currentG + stepCost;
            if (tentativeG >= gScore[static_cast<std::size_t>(nextHash)]) {
                continue;
            }
            cameFrom[static_cast<std::size_t>(nextHash)] = currentHash;
            gScore[static_cast<std::size_t>(nextHash)] = tentativeG;
            const CellCoord nextCoord{.x = nx, .y = ny};
            const int dx = std::abs(nextCoord.x - goal.x);
            const int dy = std::abs(nextCoord.y - goal.y);
            const int h = 10 * (dx + dy) - 6 * std::min(dx, dy);
            open.push(OpenNode{
                .hash = nextHash,
                .fScore = tentativeG + h,
            });
        }
    }
    return false;
}

bool MenuBackgroundSimulation::CanMoveCardinal(int cellX, int cellY, int dx, int dy) const {
    const MazeCell& cell = maze_.cells[static_cast<std::size_t>(ToIndex(maze_, cellX, cellY))];
    if (dx == 1 && dy == 0) {
        return !cell.eastWall;
    }
    if (dx == -1 && dy == 0) {
        return !cell.westWall;
    }
    if (dx == 0 && dy == 1) {
        return !cell.southWall;
    }
    if (dx == 0 && dy == -1) {
        return !cell.northWall;
    }
    return false;
}

bool MenuBackgroundSimulation::CanMoveToNeighbor(int cellX, int cellY, int dx, int dy) const {
    if (dx == 0 && dy == 0) {
        return false;
    }
    if (dx == 0 || dy == 0) {
        return CanMoveCardinal(cellX, cellY, dx, dy);
    }

    // Diagonal movement is allowed when at least one bend route is traversable.
    const bool routeXThenY =
        CanMoveCardinal(cellX, cellY, dx, 0) &&
        CanMoveCardinal(cellX + dx, cellY, 0, dy);
    const bool routeYThenX =
        CanMoveCardinal(cellX, cellY, 0, dy) &&
        CanMoveCardinal(cellX, cellY + dy, dx, 0);
    return routeXThenY || routeYThenX;
}

int MenuBackgroundSimulation::CellHash(int cellX, int cellY) const {
    return cellY * maze_.widthCells + cellX;
}

Vec2f MenuBackgroundSimulation::CellCenterFromHash(int hash) const {
    const int cellX = hash % maze_.widthCells;
    const int cellY = hash / maze_.widthCells;
    const float halfCell = static_cast<float>(maze_.cellSizeUnits) * 0.5F;
    return Vec2f{
        .x = static_cast<float>(cellX * maze_.cellSizeUnits) + halfCell,
        .y = static_cast<float>(cellY * maze_.cellSizeUnits) + halfCell,
    };
}

EnemyType MenuBackgroundSimulation::RandomEnemyType() {
    const int roll = random_.NextInt(0, 3);
    if (roll == 0) {
        return EnemyType::Drone;
    }
    if (roll == 1) {
        return EnemyType::Torpedo;
    }
    if (roll == 2) {
        return EnemyType::Hunter;
    }
    return EnemyType::Assassin;
}

float MenuBackgroundSimulation::EnemySpeedForType(EnemyType type) const {
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

void MenuBackgroundSimulation::ResolveEnemyCollisionsFromGameplay() {
    plannerWorld_.enemies = enemies_;
    collisionIncludeMask_.assign(plannerWorld_.enemies.size(), 1U);
    collisionReenteredMask_.assign(plannerWorld_.enemies.size(), 0U);
    const std::size_t pairCount = plannerWorld_.enemies.size() * plannerWorld_.enemies.size();
    if (pairVisitedScratch_.size() < pairCount) {
        pairVisitedScratch_.assign(pairCount, 0U);
        pairVisitedEpoch_ = 1U;
    }

    ResolveEnemyCollisionsSinglePass(
        plannerWorld_,
        collisionRuntimeStats_,
        frameStartPositions_,
        collisionBroadPhase_,
        collisionIncludeMask_,
        collisionReenteredMask_,
        pairVisitedScratch_,
        pairVisitedEpoch_,
        [](std::vector<EnemyTank>& enemies, int selfIndex, int partnerIndex, int reasonCode) {
            UncoupleReason reason = UncoupleReason::SeparationProximity;
            if (reasonCode == 0) {
                reason = UncoupleReason::FrontalCollision;
            } else if (reasonCode == 2) {
                reason = UncoupleReason::SelfWallContact;
            }
            EnterUncoupleMode(enemies, selfIndex, partnerIndex, reason);
        },
        ShouldEnterSeparationUncouple);

    const float minDistanceSq = kEnemyPreferredSeparationUnits * kEnemyPreferredSeparationUnits;
    const int iterations = 2;
    for (int pass = 0; pass < iterations; ++pass) {
        for (std::size_t i = 0; i < plannerWorld_.enemies.size(); ++i) {
            EnemyTank& a = plannerWorld_.enemies[i];
            if (!a.alive) {
                continue;
            }
            for (std::size_t j = i + 1; j < plannerWorld_.enemies.size(); ++j) {
                EnemyTank& b = plannerWorld_.enemies[j];
                if (!b.alive) {
                    continue;
                }
                const Vec2f delta = VecTo(a.position, b.position);
                const float distSq = delta.x * delta.x + delta.y * delta.y;
                if (distSq >= minDistanceSq) {
                    continue;
                }
                if (!ShouldEnterSeparationUncouple(a, b, distSq)) {
                    continue;
                }

                const float dist = std::sqrt(std::max(0.0001F, distSq));
                const float overlap = kEnemyPreferredSeparationUnits - dist;
                float nx = 0.0F;
                float ny = 0.0F;
                if (dist > 0.0001F) {
                    nx = delta.x / dist;
                    ny = delta.y / dist;
                } else {
                    nx = 1.0F;
                    ny = 0.0F;
                }
                const float push = overlap * 0.5F;
                a.position.x -= nx * push;
                a.position.y -= ny * push;
                b.position.x += nx * push;
                b.position.y += ny * push;
            }
        }
    }

    enemies_ = plannerWorld_.enemies;
}

void MenuBackgroundSimulation::UpdateCameraMover(float deltaSeconds) {
    if (cameraRuntime_.pathCellHashes.empty() ||
        cameraRuntime_.pathCellIndex >= cameraRuntime_.pathCellHashes.size()) {
        PickNewDestinationAndPathFromPosition(
            cameraMoverPosition_,
            cameraRuntime_.destinationCellHash,
            cameraRuntime_.pathCellHashes,
            cameraRuntime_.pathCellIndex);
    }
    if (cameraRuntime_.pathCellHashes.empty()) {
        return;
    }

    Vec2f target = CellCenterFromHash(cameraRuntime_.pathCellHashes[cameraRuntime_.pathCellIndex]);
    Vec2f delta = VecTo(cameraMoverPosition_, target);
    float remaining = VecLength(delta);
    while (remaining <= kDestinationReachedThresholdUnits &&
           cameraRuntime_.pathCellIndex + 1 < cameraRuntime_.pathCellHashes.size()) {
        ++cameraRuntime_.pathCellIndex;
        target = CellCenterFromHash(cameraRuntime_.pathCellHashes[cameraRuntime_.pathCellIndex]);
        delta = VecTo(cameraMoverPosition_, target);
        remaining = VecLength(delta);
    }

    if (cameraRuntime_.pathCellIndex + 1 >= cameraRuntime_.pathCellHashes.size() &&
        remaining <= kDestinationReachedThresholdUnits) {
        PickNewDestinationAndPathFromPosition(
            cameraMoverPosition_,
            cameraRuntime_.destinationCellHash,
            cameraRuntime_.pathCellHashes,
            cameraRuntime_.pathCellIndex);
        return;
    }

    if (remaining <= 0.0001F) {
        return;
    }

    const float maxStep = kCameraMoveSpeedUnitsPerSecond * deltaSeconds;
    const float step = std::min(maxStep, remaining);
    const float nx = delta.x / remaining;
    const float ny = delta.y / remaining;
    cameraMoverPosition_.x += nx * step;
    cameraMoverPosition_.y += ny * step;
}
