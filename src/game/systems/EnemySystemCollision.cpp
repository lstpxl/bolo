#include "game/systems/EnemySystemCollision.h"

#include <algorithm>
#include <cmath>
#include "core/Profiling.h"
#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"
#include "game/systems/EnemySystemHelpers.h"
#include "game/systems/EnemySystemInternal.h"

namespace {
constexpr int kReasonFrontalCollision = 0;
constexpr int kReasonSeparationProximity = 1;

std::size_t PairTypeMatrixIndex(EnemyType a, EnemyType b) {
    int ai = EnemyTypeTelemetryIndex(a);
    int bi = EnemyTypeTelemetryIndex(b);
    if (ai > bi) {
        std::swap(ai, bi);
    }
    return static_cast<std::size_t>(ai * kEnemyTypeTelemetryCount + bi);
}
}  // namespace

void ResolveEnemyCollisionsSinglePass(
    WorldState& world,
    EnemyRuntimeStats& frameStats,
    const std::vector<Vec2f>& frameStartPositions,
    game::spatial::SweepPruneBroadPhase& broadPhase,
    const std::vector<std::uint8_t>& includeMask,
    const std::vector<std::uint8_t>& reenteredFullTierMask,
    std::vector<std::uint32_t>& pairVisitedScratch,
    std::uint32_t& pairVisitedEpoch,
    EnterUncoupleCallback enterUncoupleMode,
    ShouldEnterSeparationCallback shouldEnterSeparationUncouple) {
    (void)reenteredFullTierMask;
    profiling::ScopedProfile scope(profiling::Scope::EnemyFrontalCollisions, true);
    constexpr float kSharedBroadRadiusUnits = 1.0F;
    const float killDistSq =
        GameplayConstants::kEnemyMutualKillDistanceUnits * GameplayConstants::kEnemyMutualKillDistanceUnits;
    const float separationDistSq =
        GameplayConstants::kEnemyPreferredSeparationUnits * GameplayConstants::kEnemyPreferredSeparationUnits;
    {
        const float cellSize = GameplayConstants::kMazeCellSizeUnits;
        auto worldToCellX = [&](float x) {
            const int cx = static_cast<int>(std::floor(x / cellSize));
            return std::max(0, std::min(world.maze.widthCells - 1, cx));
        };
        auto worldToCellY = [&](float y) {
            const int cy = static_cast<int>(std::floor(y / cellSize));
            return std::max(0, std::min(world.maze.heightCells - 1, cy));
        };
        std::uint64_t included = 0;
        std::uint64_t crossedCell = 0;
        for (std::size_t i = 0; i < world.enemies.size() && i < includeMask.size(); ++i) {
            if (includeMask[i] == 0U) {
                continue;
            }
            const EnemyTank& enemy = world.enemies[i];
            if (!enemy.alive) {
                continue;
            }
            included += 1;
            const Vec2f& start = frameStartPositions[i];
            if (worldToCellX(start.x) != worldToCellX(enemy.position.x) ||
                worldToCellY(start.y) != worldToCellY(enemy.position.y)) {
                crossedCell += 1;
            }
        }
        gEnemyRuntimeWindowStats.frontalGridCandidates += included;
        gEnemyRuntimeWindowStats.separationGridCandidates += included;
        gEnemyRuntimeWindowStats.frontalGridCellTransitions += crossedCell;
        gEnemyRuntimeWindowStats.frontalGridInsertEstimate += included + crossedCell;
    }
    {
        profiling::ScopedProfile buildScope(profiling::Scope::EnemyFrontalGridBuild, true);
        broadPhase.BeginFrame(static_cast<int>(world.enemies.size()));
        for (int i = 0; i < static_cast<int>(world.enemies.size()); ++i) {
            const bool active = i < static_cast<int>(includeMask.size()) &&
                includeMask[static_cast<std::size_t>(i)] != 0U &&
                world.enemies[static_cast<std::size_t>(i)].alive;
            const Vec2f& start = frameStartPositions[static_cast<std::size_t>(i)];
            const Vec2f& end = world.enemies[static_cast<std::size_t>(i)].position;
            broadPhase.UpdateEntity(i, start, end, kSharedBroadRadiusUnits, active);
        }
    }
    {
        profiling::ScopedProfile pairTraverseScope(profiling::Scope::EnemyFrontalPairTraverse, true);
        broadPhase.ForEachCandidatePair(
            [&](int i, int j) {
                EnemyTank& a = world.enemies[static_cast<std::size_t>(i)];
                EnemyTank& b = world.enemies[static_cast<std::size_t>(j)];
                if (!a.alive || !b.alive) {
                    return;
                }

                const bool inBase =
                    game::geometry::IsPointInUndestroyedBase(world, a.position, GameplayConstants::kWallClearanceForAvoidance) ||
                    game::geometry::IsPointInUndestroyedBase(world, b.position, GameplayConstants::kWallClearanceForAvoidance);

                frameStats.frontalPairsVisited += 1;
                gEnemyRuntimeWindowStats.frontalPairsByType[PairTypeMatrixIndex(a.type, b.type)] += 1;
                if (inBase) {
                    gEnemyRuntimeWindowStats.frontalPairsBaseSkipped += 1;
                } else {
                    profiling::ScopedProfile frontalPairScope(profiling::Scope::EnemyFrontalPairNarrowphase, true);
                    frameStats.frontalPairsDistanceChecks += 1;
                    const float centerDistSq = DistanceSq(a.position, b.position);
                    if (centerDistSq <= killDistSq) {
                        enterUncoupleMode(world.enemies, j, i, kReasonFrontalCollision);
                        enterUncoupleMode(world.enemies, i, j, kReasonFrontalCollision);
                    }
                }

                if (!a.alive || !b.alive) {
                    return;
                }

                frameStats.separationPairsVisited += 1;
                gEnemyRuntimeWindowStats.separationPairsByType[PairTypeMatrixIndex(a.type, b.type)] += 1;
                if (inBase) {
                    gEnemyRuntimeWindowStats.separationPairsBaseSkipped += 1;
                    return;
                }

                profiling::ScopedProfile separationPairScope(profiling::Scope::EnemySeparationPairResolve, true);
                const float distSq = DistanceSq(a.position, b.position);
                // Close-overlap pairs are already handled in the frontal branch above.
                // Avoid re-entering uncouple again in separation for the same pair/frame.
                if (distSq <= killDistSq) {
                    return;
                }
                if (distSq >= separationDistSq) {
                    return;
                }
                if (!shouldEnterSeparationUncouple(a, b, distSq)) {
                    return;
                }

                frameStats.separationPairsResolved += 1;
                enterUncoupleMode(world.enemies, j, i, kReasonSeparationProximity);
                enterUncoupleMode(world.enemies, i, j, kReasonSeparationProximity);
            },
            pairVisitedScratch,
            pairVisitedEpoch);
    }
    const game::spatial::SweepPruneBroadPhase::FrameStats& sapStats = broadPhase.GetFrameStats();
    gEnemyRuntimeWindowStats.sapUpdateCalls += sapStats.updateCalls;
    gEnemyRuntimeWindowStats.sapActiveItems += sapStats.activeItems;
    gEnemyRuntimeWindowStats.sapCandidatePairs += sapStats.candidatePairs;
    gEnemyRuntimeWindowStats.sapXRepairs += sapStats.xLocalRepairs;
    gEnemyRuntimeWindowStats.sapYRepairs += sapStats.yLocalRepairs;
}
