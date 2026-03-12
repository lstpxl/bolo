#include "game/systems/EnemySystem.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <future>
#include <limits>
#include <vector>

#include "core/AngleMath.h"
#include "core/Log.h"
#include "core/Profiling.h"
#include "core/Random.h"
#include "game/geometry/WorldGeometry.h"
#include "game/navigation/CellCoordCache.h"
#include "game/navigation/PlayerFlowField.h"
#include "game/spatial/EnemyCellOccupancy.h"
#include "game/spatial/SweepPruneBroadPhase.h"
#include "game/systems/EnemyAssassin.h"
#include "game/systems/EnemyDrone.h"
#include "game/systems/EnemyHunter.h"
#include "game/systems/EnemySystemCheapTier.h"
#include "game/systems/EnemySystemCollision.h"
#include "game/systems/EnemySystemCombatPhase.h"
#include "game/systems/EnemySystemHelpers.h"
#include "game/systems/EnemySystemInternal.h"
#include "game/systems/EnemySystemPathfinding.h"
#include "game/systems/EnemySystemTelemetry.h"
#include "game/systems/EnemySystemUncouple.h"
#include "game/systems/EnemyTorpedo.h"

EnemyRuntimeWindowStats gEnemyRuntimeWindowStats{};

namespace
{
constexpr float kPi = 3.14159265358979323846F;
constexpr float kEightDirectionStep = kPi / 4.0F;
constexpr float kEnemyPlanningClearanceScale = 1.5F;
constexpr float kTorpedoNearCollisionCheckDistanceUnits = 3.0F;
constexpr float kTorpedoMoveDecisionHoldDistanceUnits = 1.0F;
constexpr float kTorpedoRetreatExitClearanceUnits = 2.0F;
constexpr float kTorpedoRetreatSpeedFactor = 0.1F;
constexpr float kTorpedoImmediateObstacleDistanceUnits = 1.0F;
constexpr float kTorpedoPlayerDetectIntervalSeconds = 0.25F;
constexpr float kParallelWallSideProbeUnits = 0.75F;
constexpr float kParallelWallContactThresholdUnits = 0.18F;
constexpr bool kUseFlowFieldPathGuidance = true;
constexpr bool kUseAssassinFlowFieldOnlyNavigation = true;
constexpr bool kUseAssassinAStarBackupNavigation = false;
constexpr float kUncouplePriorityEpsilon = 0.05F;
constexpr int kAssassinWallPhaseUncouple = 1;
constexpr int kAssassinWallPhaseFull = 2;

EnemyRuntimeStats gEnemyRuntimeStats{};
std::uint64_t gLastEnemyStatsPrintedFrame = 0;

[[maybe_unused]] void RecordEnemyEnemyMutualKillDebug(
    const WorldState& world, int i, int j, const EnemyTank& a, const EnemyTank& b,
    float centerDistance, bool reenteredA, bool reenteredB, bool frontalPass)
{
    EnemyRuntimeWindowStats& stats = gEnemyRuntimeWindowStats;
    stats.killDebugEnemyEnemyEvents += 1;
    if (frontalPass) {
        stats.killDebugEnemyEnemyFrontalEvents += 1;
    } else {
        stats.killDebugEnemyEnemySeparationEvents += 1;
    }
    if (reenteredA || reenteredB) {
        stats.killDebugEnemyEnemyReenterEither += 1;
    }
    if (reenteredA && reenteredB) {
        stats.killDebugEnemyEnemyReenterBoth += 1;
    }
    const bool wallA = game::geometry::IsPointInWall(
        world, a.position, GameplayConstants::kTankCollisionRadiusUnits);
    const bool wallB = game::geometry::IsPointInWall(
        world, b.position, GameplayConstants::kTankCollisionRadiusUnits);
    if (wallA || wallB) {
        stats.killDebugEnemyEnemyWallContact += 1;
    }
    stats.killDebugEnemyEnemyMinDistance =
        std::min(stats.killDebugEnemyEnemyMinDistance, centerDistance);
    stats.killDebugEnemyEnemyMaxDistance =
        std::max(stats.killDebugEnemyEnemyMaxDistance, centerDistance);
    stats.killDebugEnemyEnemyDistanceSum += centerDistance;

    if (stats.killDebugEnemyEnemySamplesPrinted < 12U) {
        bolt::log::Profile(
            "[ENEMY_KILL_DEBUG_EVENT] reason=enemy_enemy pass=%s pair=%d,%d dist=%.3f "
            "killDist=%.3f reenter=%d,%d wallContact=%d,%d tier=%d,%d posA=(%.2f,%.2f) "
            "posB=(%.2f,%.2f)\n",
            frontalPass ? "frontal" : "separation", i, j, centerDistance,
            GameplayConstants::kEnemyMutualKillDistanceUnits, reenteredA ? 1 : 0,
            reenteredB ? 1 : 0, wallA ? 1 : 0, wallB ? 1 : 0, static_cast<int>(a.simTier),
            static_cast<int>(b.simTier), a.position.x, a.position.y, b.position.x, b.position.y);
        stats.killDebugEnemyEnemySamplesPrinted += 1;
    }
}

float NormalizeAngle(float angleRadians) { return core::angle::NormalizeAngle(angleRadians); }

float QuantizeToEightDirections(float angleRadians)
{
    return core::angle::QuantizeToEightDirections(angleRadians);
}

Vec2f DirectionFromHeading(float headingRadians)
{
    return core::angle::DirectionFromHeading(headingRadians);
}

void EnterUncoupleByReasonCode(
    std::vector<EnemyTank>& enemies, int selfIndex, int partnerIndex, int reasonCode)
{
    UncoupleReason reason = UncoupleReason::SeparationProximity;
    if (reasonCode == 0) {
        reason = UncoupleReason::FrontalCollision;
    } else if (reasonCode == 2) {
        reason = UncoupleReason::SelfWallContact;
    }
    EnterUncoupleMode(enemies, selfIndex, partnerIndex, reason);
}

float EnemySubtypeSpeedMultiplier(EnemyType type, EnemySubtype subtype)
{
    if (subtype == EnemySubtype::Basic) {
        return 0.75F;
    }
    if (subtype == EnemySubtype::Lord && type == EnemyType::Hunter) {
        return 1.25F;
    }
    return 1.0F;
}

const char* EnemyTypeLabel(EnemyType type)
{
    switch (type) {
        case EnemyType::Drone:
            return "Drone";
        case EnemyType::Torpedo:
            return "Torpedo";
        case EnemyType::Hunter:
            return "Hunter";
        case EnemyType::Assassin:
            return "Assassin";
    }
    return "Unknown";
}

const char* EnemySubtypeLabel(EnemySubtype subtype)
{
    switch (subtype) {
        case EnemySubtype::Basic:
            return "Basic";
        case EnemySubtype::Advanced:
            return "Advanced";
        case EnemySubtype::Lord:
            return "Lord";
    }
    return "Unknown";
}

const char* EnemyAiModeLabel(EnemyAiMode mode)
{
    switch (mode) {
        case EnemyAiMode::Wander:
            return "Wander";
        case EnemyAiMode::Watch:
            return "Watch";
        case EnemyAiMode::Scout:
            return "Scout";
        case EnemyAiMode::Chase:
            return "Chase";
        case EnemyAiMode::Rotate:
            return "Rotate";
        case EnemyAiMode::Path:
            return "Path";
        case EnemyAiMode::Pursuit:
            return "Pursuit";
        case EnemyAiMode::Uncouple:
            return "Uncouple";
    }
    return "Unknown";
}

const char* EnemySimTierLabel(EnemySimTier tier)
{
    switch (tier) {
        case EnemySimTier::Full:
            return "Full";
        case EnemySimTier::Cheap:
            return "Cheap";
    }
    return "Unknown";
}

const char* TorpedoMoveModeLabel(TorpedoMoveMode mode)
{
    switch (mode) {
        case TorpedoMoveMode::Move:
            return "Move";
        case TorpedoMoveMode::Retreat:
            return "Retreat";
        case TorpedoMoveMode::Targeting:
            return "Targeting";
        case TorpedoMoveMode::Rotate:
            return "Rotate";
    }
    return "Unknown";
}

float EnemySpeed(EnemyType type, EnemySubtype subtype, bool assassinHasLineOfSight, int levelNumber)
{
    float baseSpeed = GameplayConstants::kEnemyDroneSpeed;
    if (type == EnemyType::Drone) {
        baseSpeed = GameplayConstants::kEnemyDroneSpeed;
    } else if (type == EnemyType::Torpedo) {
        baseSpeed = GameplayConstants::kEnemyTorpedoSpeed;
    } else if (type == EnemyType::Hunter) {
        baseSpeed = GameplayConstants::kEnemyHunterSpeed;
    } else {
        baseSpeed = assassinHasLineOfSight ? 3.0F : 1.5F;
    }
    float speed = baseSpeed * EnemySubtypeSpeedMultiplier(type, subtype);
    // Level 9: assassin-only debug level with 4× assassin speed for flow-field testing.
    if (levelNumber == 9 && type == EnemyType::Assassin) {
        speed *= 4.0F;
    }
    return speed;
}

bool IsPointInUndestroyedBase(const WorldState& world, const Vec2f& point, float clearanceUnits)
{
    return game::geometry::IsPointInUndestroyedBase(world, point, clearanceUnits);
}

bool IsInPlayerViewport(const Vec2f& point, const GameState& state, const GameplayView& view)
{
    const float halfWidth = view.viewportWidthUnits * 0.5F;
    const float halfHeight = view.viewportHeightUnits * 0.5F;
    const Vec2f center = state.world.player.position;
    return point.x >= center.x - halfWidth && point.x <= center.x + halfWidth &&
           point.y >= center.y - halfHeight && point.y <= center.y + halfHeight;
}

EnemySimTier DetermineEnemySimTier(
    const EnemyTank& enemy, const game::navigation::CellCoordCache& cellCache)
{
    constexpr int fullTierRadiusCells = 3;
    const game::navigation::MazeCellCoord playerCell = cellCache.PlayerCell();
    const int dx = std::abs(enemy.cellCoord.x - playerCell.x);
    const int dy = std::abs(enemy.cellCoord.y - playerCell.y);
    const bool nearPlayer = std::max(dx, dy) <= fullTierRadiusCells;
    const bool forceFullForTorpedoState =
        enemy.type == EnemyType::Torpedo && enemy.torpedoMoveMode != TorpedoMoveMode::Move;
    return (nearPlayer || forceFullForTorpedoState) ? EnemySimTier::Full : EnemySimTier::Cheap;
}

bool SegmentIntersectsWall(
    const WorldState& world, const Vec2f& from, const Vec2f& to, float clearanceUnits)
{
    return game::geometry::SegmentIntersectsWall(world, from, to, clearanceUnits);
}

bool IsEdgeOnWallContact(
    const WorldState& world, const Vec2f& candidatePosition, float movementHeadingRadians)
{
    const float leftHeading = NormalizeAngle(movementHeadingRadians - (kPi * 0.5F));
    const float rightHeading = NormalizeAngle(movementHeadingRadians + (kPi * 0.5F));
    const float leftClear = game::geometry::FreeDistanceAhead(
        world, candidatePosition, leftHeading, kParallelWallSideProbeUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits, 1.0F);
    const float rightClear = game::geometry::FreeDistanceAhead(
        world, candidatePosition, rightHeading, kParallelWallSideProbeUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits, 1.0F);
    return leftClear <= kParallelWallContactThresholdUnits ||
           rightClear <= kParallelWallContactThresholdUnits;
}

[[maybe_unused]] void DecrementOriginBaseAliveCount(WorldState& world, EnemyTank& enemy)
{
    if (enemy.originBaseIndex < 0 ||
        enemy.originBaseIndex >= static_cast<int>(world.enemyBases.size())) {
        enemy.originBaseIndex = -1;
        return;
    }
    EnemyBase& origin = world.enemyBases[static_cast<std::size_t>(enemy.originBaseIndex)];
    origin.activeEnemies = std::max(0, origin.activeEnemies - 1);
    enemy.originBaseIndex = -1;
}

bool TrySeparationTurn(
    const WorldState& world, const std::vector<EnemyTank>& enemies, int selfIndex, float speed,
    float deltaSeconds, float& chosenHeading, Vec2f& candidatePosition)
{
    const EnemyTank& self = enemies[static_cast<std::size_t>(selfIndex)];
    const std::array<float, 2> turnOffsets{-kEightDirectionStep, kEightDirectionStep};
    float bestDistanceSq = -1.0F;
    const float sepSq = GameplayConstants::kEnemyPreferredSeparationUnits *
                        GameplayConstants::kEnemyPreferredSeparationUnits;
    bool found = false;
    for (float offset : turnOffsets) {
        const float candidateHeading = QuantizeToEightDirections(self.headingRadians + offset);
        const Vec2f dir = DirectionFromHeading(candidateHeading);
        const Vec2f candidate{
            .x = self.position.x + dir.x * speed * deltaSeconds,
            .y = self.position.y + dir.y * speed * deltaSeconds,
        };
        if (SegmentIntersectsWall(
                world, self.position, candidate,
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
    const std::vector<EnemyTank>& enemies, const std::vector<Vec2f>& frameStartPositions,
    int selfIndex, const Vec2f& from, const Vec2f& to, float minSeparation,
    const std::vector<float>* uncoupleEscapeScores = nullptr)
{
    auto otherYieldsToSelf = [&](int otherIndex) {
        if (uncoupleEscapeScores == nullptr) {
            return false;
        }
        if (selfIndex < 0 || otherIndex < 0 || selfIndex >= static_cast<int>(enemies.size()) ||
            otherIndex >= static_cast<int>(enemies.size())) {
            return false;
        }
        const EnemyTank& self = enemies[static_cast<std::size_t>(selfIndex)];
        const EnemyTank& other = enemies[static_cast<std::size_t>(otherIndex)];
        if (self.aiMode != EnemyAiMode::Uncouple || other.aiMode != EnemyAiMode::Uncouple) {
            return false;
        }
        if (selfIndex >= static_cast<int>(uncoupleEscapeScores->size()) ||
            otherIndex >= static_cast<int>(uncoupleEscapeScores->size())) {
            return false;
        }
        const float selfScore = (*uncoupleEscapeScores)[static_cast<std::size_t>(selfIndex)];
        const float otherScore = (*uncoupleEscapeScores)[static_cast<std::size_t>(otherIndex)];
        if (selfScore > otherScore + kUncouplePriorityEpsilon) {
            return true;
        }
        if (std::fabs(selfScore - otherScore) <= kUncouplePriorityEpsilon) {
            return selfIndex < otherIndex;
        }
        return false;
    };

    constexpr float kSeparationProgressEpsilon = 0.001F;
    for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
        if (i == selfIndex) {
            continue;
        }
        if (otherYieldsToSelf(i)) {
            continue;
        }
        const EnemyTank& other = enemies[static_cast<std::size_t>(i)];
        if (!other.alive) {
            continue;
        }

        // Use updated position for already-processed enemies and frame-start position for others.
        const Vec2f otherObstacle =
            (i < selfIndex) ? other.position : frameStartPositions[static_cast<std::size_t>(i)];

        const float fromDistance = Distance(from, otherObstacle);
        const float toDistance = Distance(to, otherObstacle);
        const bool separatingFromOverlap =
            fromDistance < minSeparation && toDistance > fromDistance + kSeparationProgressEpsilon;

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

profiling::Scope EnemyTypeProfileScope(EnemyType type)
{
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

}  // namespace

void UpdateEnemySystem(
    GameState& state, const GameplayView& view, float deltaSeconds, Random& random,
    game::navigation::FlowRebuildWorker& flowWorker)
{
    profiling::ScopedProfile scope(profiling::Scope::EnemyUpdate, true);
    gEnemyRuntimeStats = EnemyRuntimeStats{};
    const bool playerInvisible = state.menuSettings.invisibility;
    std::vector<Vec2f> frameStartPositions{};
    frameStartPositions.reserve(state.world.enemies.size());
    for (const EnemyTank& enemy : state.world.enemies) {
        frameStartPositions.push_back(enemy.position);
    }

    NavigationRuntimeCache& navigationCache = state.world.navigationCache;
    game::navigation::CellCoordCache& cellCache = navigationCache.cellCoords;

    game::spatial::EnemyCellOccupancy& occupancy = navigationCache.enemyCellOccupancy;
    game::spatial::EnemyCellOccupancy& rayQueryOccupancy = navigationCache.enemyRayQueryOccupancy;
    const int maxEnemies = std::max(
        static_cast<int>(state.world.enemies.size()),
        static_cast<int>(GameplayConstants::kMaxAliveEnemies));
    occupancy.Reserve(
        cellCache.WidthCells(), cellCache.HeightCells(), maxEnemies, cellCache.CellSizeUnits());
    for (int i = 0; i < static_cast<int>(state.world.enemies.size()); ++i) {
        EnemyTank& e = state.world.enemies[static_cast<std::size_t>(i)];
        if (!e.alive) {
            occupancy.Remove(i);
            continue;
        }
        const game::navigation::MazeCellCoord c =
            cellCache.WorldToCell(frameStartPositions[static_cast<std::size_t>(i)]);
        e.cellCoord = c;
        occupancy.SetCell(i, c.x, c.y);
    }
    rayQueryOccupancy.Reserve(
        cellCache.WidthCells(), cellCache.HeightCells(), maxEnemies, cellCache.CellSizeUnits());
    rayQueryOccupancy.BuildFromPositions(state.world, &frameStartPositions);
    game::navigation::PlayerFlowField& playerFlowField = navigationCache.playerFlowField;

    cellCache.UpdatePlayerCell(state.world.player.position);
    if (kUseFlowFieldPathGuidance) {
        game::navigation::FlowFieldUpdateStats flowStats;
        playerFlowField.Update(
            flowWorker, navigationCache, state.world, cellCache.PlayerCell().x,
            cellCache.PlayerCell().y, &flowStats);
        if (flowStats.playerCellChanged) {
            gEnemyRuntimeWindowStats.navPlayerCellChanges += 1;
        }
        if (flowStats.rebuildScheduled) {
            gEnemyRuntimeWindowStats.navFlowRebuilds += 1;
        }
    }

    std::vector<std::uint8_t> reenteredFullTierMask(state.world.enemies.size(), 0U);
    std::vector<float> uncoupleEscapeScores(state.world.enemies.size(), -1000.0F);
    for (int enemyIndex = 0; enemyIndex < static_cast<int>(state.world.enemies.size());
        ++enemyIndex) {
        uncoupleEscapeScores[static_cast<std::size_t>(enemyIndex)] = ComputeUncoupleEscapeScore(
            state.world, cellCache, playerFlowField, state.world.enemies, enemyIndex);
    }

    for (int enemyIndex = 0; enemyIndex < static_cast<int>(state.world.enemies.size());
        ++enemyIndex) {
        EnemyTank& enemy = state.world.enemies[static_cast<std::size_t>(enemyIndex)];
        if (!enemy.alive) {
            continue;
        }
        profiling::ScopedProfile enemyTypeScope(EnemyTypeProfileScope(enemy.type), true);

        const EnemySimTier previousTier = enemy.simTier;
        enemy.simTier = DetermineEnemySimTier(enemy, cellCache);
        const bool reenteredFullTier =
            previousTier == EnemySimTier::Cheap && enemy.simTier == EnemySimTier::Full;
        if (reenteredFullTier) {
            reenteredFullTierMask[static_cast<std::size_t>(enemyIndex)] = 1U;
            if (enemy.type == EnemyType::Assassin && enemy.offscreenSegmentActive) {
                bolt::log::Profile(
                    "[ENEMY_ASSASSIN_SEGMENT_DROP] id=%d reason=tier_reentered_full "
                    "pos=(%.3f,%.3f) cell=(%d,%d)\n",
                    enemyIndex,
                    enemy.position.x,
                    enemy.position.y,
                    enemy.cellCoord.x,
                    enemy.cellCoord.y);
            }
            enemy.offscreenSegmentActive = false;
            enemy.cheapSegmentBuildFailCount = 0;
            enemy.cheapSegmentLastFailCellHash = -1;
            enemy.cheapSegmentLastFailReason = CheapSegmentFailReason::None;
            enemy.cheapSegmentBuildMethodStage = 0;
            enemy.cheapSegmentInsideWallAvoidLastFrame = false;
            if (enemy.type == EnemyType::Assassin) {
                enemy.pathWaypointCount = 0;
                enemy.pathWaypointIndex = 0;
                enemy.expectedPathCellHash = -1;
                enemy.cachedFlowFromCellHash = -1;
                enemy.cheapTierCrowdedSlowMode = false;
            }
        }

        if (enemy.simTier == EnemySimTier::Cheap) {
            if (enemy.type == EnemyType::Assassin && previousTier != EnemySimTier::Cheap) {
                const bool insideWallOnCheapEntry = game::geometry::IsPointInWall(
                    state.world, enemy.position, GameplayConstants::kTankCollisionRadiusUnits);
                if (insideWallOnCheapEntry) {
                    bolt::log::Profile(
                        "[ENEMY_ASSASSIN_CHEAP_ENTRY_IN_WALL] id=%d pos=(%.3f,%.3f) cell=(%d,%d) "
                        "cachedFlowFromHash=%d expectedPathHash=%d\n",
                        enemyIndex,
                        enemy.position.x,
                        enemy.position.y,
                        enemy.cellCoord.x,
                        enemy.cellCoord.y,
                        enemy.cachedFlowFromCellHash,
                        enemy.expectedPathCellHash);
                }
            }
            AdvanceCheapTierTimers(state, enemy, deltaSeconds, playerInvisible, view);

            float cheapSpeed =
                EnemySpeed(enemy.type, enemy.subtype, false, state.menuSettings.levelNumber);
            if (enemy.aiMode == EnemyAiMode::Watch || enemy.aiMode == EnemyAiMode::Rotate) {
                cheapSpeed = 0.0F;
            }
            ApplyCheapTierMovement(
                state, cellCache, playerFlowField, enemy, enemyIndex, deltaSeconds, cheapSpeed,
                random);
            const game::navigation::MazeCellCoord cheapCell = cellCache.WorldToCell(enemy.position);
            enemy.cellCoord = cheapCell;
            occupancy.SetCell(enemyIndex, cheapCell.x, cheapCell.y);
            continue;
        }

        // Cheap tier can't get here

        EnemyPerception perception{};
        {
            profiling::ScopedProfile phaseScope(profiling::Scope::EnemyAiPerception, true);
            perception = RunPerceptionPhase(state, enemy, deltaSeconds, playerInvisible, random);
        }

        float movementHeading = QuantizeToEightDirections(enemy.headingRadians);
        float speed = EnemySpeed(
            enemy.type, enemy.subtype, perception.assassinHasLineOfSight,
            state.menuSettings.levelNumber);
        bool preserveContinuousHeading = false;
        bool handledByUncoupleMovement = false;
        const bool startedInsideWallAvoid = enemy.type == EnemyType::Assassin &&
            game::geometry::IsPointInWall(
                state.world, enemy.position, GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
        const char* movementSourceLabel = "full_ai";
        int movementSourceBucket = kAssassinWallPhaseFull;
        {
            profiling::ScopedProfile phaseScope(profiling::Scope::EnemyAiDecision, true);
            if (enemy.aiMode == EnemyAiMode::Uncouple) {
                enemy.aiStateTimerSeconds =
                    std::max(0.0F, enemy.aiStateTimerSeconds - deltaSeconds);
                if (enemy.aiStateTimerSeconds > 0.0F) {
                    float uncoupleFallbackHeading = enemy.desiredHeadingRadians;
                    if (enemy.type == EnemyType::Assassin) {
                        float flowHeading = uncoupleFallbackHeading;
                        if (TryGetAssassinFlowHeading(
                                cellCache, playerFlowField, enemy, flowHeading)) {
                            uncoupleFallbackHeading = flowHeading;
                        }
                    }
                    movementHeading = SelectUncoupleHeading(
                        state.world, state.world.enemies, enemyIndex, uncoupleFallbackHeading,
                        random);
                    enemy.desiredHeadingRadians = movementHeading;
                    handledByUncoupleMovement = true;
                } else {
                    RestoreFromUncoupleMode(enemy);
                }
            }

            if (!handledByUncoupleMovement && enemy.type == EnemyType::Drone) {
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
                            (kPi * 2.0F / GameplayConstants::kSlowRotateFullTurnSeconds) *
                            deltaSeconds);
                    const float clearDistance = game::geometry::FreeDistanceAhead(
                        state.world, enemy.position, movementHeading,
                        GameplayConstants::kEnemyRequiredClearRunUnits + 0.5F,
                        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                        kEnemyPlanningClearanceScale);
                    if (enemy.aiModeElapsedSeconds >=
                        GameplayConstants::kSlowRotateFullTurnSeconds) {
                        if (enemy.returnToBase) {
                            float returnHeading = movementHeading;
                            if (SelectDroneReturnToBaseHeading(
                                    state.world, enemy, random, returnHeading)) {
                                movementHeading = returnHeading;
                                enemy.returnToBase = false;
                                enemy.aiMode = EnemyAiMode::Wander;
                                enemy.aiModeElapsedSeconds = 0.0F;
                            }
                        } else if (clearDistance > GameplayConstants::kEnemyRequiredClearRunUnits) {
                            float escapeHeading = movementHeading;
                            if (SelectDroneWatchEscapeHeading(
                                    state.world, state.world.enemies, enemyIndex, deltaSeconds,
                                    escapeHeading)) {
                                movementHeading = escapeHeading;
                                enemy.aiMode = EnemyAiMode::Wander;
                                enemy.aiModeElapsedSeconds = 0.0F;
                            }
                        }
                    }
                } else {
                    bool shouldWatch = false;
                    movementHeading =
                        SelectScoutHeadingWithFallback(state.world, enemy, false, shouldWatch);
                    if (shouldWatch) {
                        EnterDroneWatchMode(state.world, enemy, random);
                        speed = 0.0F;
                    }
                }
            } else if (!handledByUncoupleMovement && enemy.type == EnemyType::Torpedo) {
                enemy.torpedoPlayerDetectTimerSeconds -= deltaSeconds;
                if (enemy.torpedoPlayerDetectTimerSeconds <= 0.0F) {
                    enemy.torpedoPlayerDetectTimerSeconds = kTorpedoPlayerDetectIntervalSeconds;
                    enemy.torpedoPlayerDetected =
                        !playerInvisible && !perception.playerObscured &&
                        perception.distanceToPlayer <= GameplayConstants::kTorpedoDetectRangeUnits;
                    enemy.torpedoLastKnownPlayerHeadingRadians =
                        std::atan2(perception.toPlayer.x, -perception.toPlayer.y);
                }
                if (enemy.torpedoMoveMode == TorpedoMoveMode::Retreat) {
                    movementHeading = QuantizeToEightDirections(enemy.headingRadians);
                    speed = -std::abs(speed) * kTorpedoRetreatSpeedFactor;
                    const float forwardClear = game::geometry::FreeDistanceAheadWithEnemies(
                        state.world, state.world.enemies, enemyIndex, enemy.position,
                        enemy.headingRadians, kTorpedoNearCollisionCheckDistanceUnits,
                        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                        kEnemyPlanningClearanceScale, &rayQueryOccupancy);
                    if (enemy.torpedoRetreatMovedUnits >= kTorpedoRetreatExitClearanceUnits &&
                        forwardClear >= kTorpedoRetreatExitClearanceUnits) {
                        speed = 0.0F;
                        EnterTorpedoTargetingMode(enemy);
                    }
                } else if (enemy.torpedoMoveMode == TorpedoMoveMode::Targeting) {
                    speed = 0.0F;
                    enemy.torpedoChosenHeadingRadians =
                        SelectBestLongStraightHeading(state.world, enemy);
                    EnterTorpedoRotateMode(enemy);
                } else if (enemy.torpedoMoveMode == TorpedoMoveMode::Rotate) {
                    speed = 0.0F;
                    preserveContinuousHeading = true;
                    movementHeading = UpdateTorpedoRotateHeading(enemy, deltaSeconds);
                } else {
                    const float straightHeading = QuantizeToEightDirections(enemy.headingRadians);
                    const bool lockHeadingForBaseExit =
                        IsPointInUndestroyedBase(state.world, enemy.position, 1.0F);
                    if (lockHeadingForBaseExit ||
                        enemy.torpedoMoveDecisionHoldRemainingUnits > 0.0F) {
                        movementHeading = straightHeading;
                        const float nearClear = game::geometry::FreeDistanceAheadWithEnemies(
                            state.world, state.world.enemies, enemyIndex, enemy.position,
                            straightHeading, kTorpedoNearCollisionCheckDistanceUnits,
                            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                            kEnemyPlanningClearanceScale, &rayQueryOccupancy);
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
                            state.world, state.world.enemies, enemyIndex, enemy, random,
                            startRetreat, decidedStraight, &rayQueryOccupancy);
                        if (startRetreat) {
                            enemy.torpedoMoveMode = TorpedoMoveMode::Retreat;
                            enemy.torpedoRetreatMovedUnits = 0.0F;
                            enemy.torpedoMoveDecisionHoldRemainingUnits = 0.0F;
                            movementHeading = straightHeading;
                            speed = -std::abs(speed) * kTorpedoRetreatSpeedFactor;
                        }
                    }
                }
            } else if (!handledByUncoupleMovement && enemy.type == EnemyType::Hunter) {
                const bool canChase =
                    !playerInvisible && !perception.playerObscured &&
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
                    } else if (perception.distanceToPlayer >
                               GameplayConstants::kHunterMaxDistanceUnits) {
                        movementHeading = QuantizeToEightDirections(
                            std::atan2(perception.toPlayer.x, -perception.toPlayer.y));
                    } else {
                        speed = 0.0F;
                    }
                } else if (enemy.aiMode == EnemyAiMode::Rotate) {
                    speed = 0.0F;
                    preserveContinuousHeading = true;
                    movementHeading = NormalizeAngle(
                        enemy.headingRadians +
                        (kPi * 2.0F / GameplayConstants::kSlowRotateFullTurnSeconds) *
                            deltaSeconds);
                    const float clearDistance = game::geometry::FreeDistanceAhead(
                        state.world, enemy.position, movementHeading,
                        GameplayConstants::kEnemyRequiredClearRunUnits + 0.5F,
                        GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                        kEnemyPlanningClearanceScale);
                    if (clearDistance > GameplayConstants::kEnemyRequiredClearRunUnits) {
                        enemy.aiMode = EnemyAiMode::Scout;
                        enemy.aiModeElapsedSeconds = 0.0F;
                    }
                } else {
                    bool shouldRotate = false;
                    movementHeading =
                        SelectScoutHeadingWithFallback(state.world, enemy, true, shouldRotate);
                    if (shouldRotate) {
                        enemy.aiMode = EnemyAiMode::Rotate;
                        enemy.aiModeElapsedSeconds = 0.0F;
                        speed = 0.0F;
                    } else {
                        enemy.aiMode = EnemyAiMode::Scout;
                    }
                }
            } else if (!handledByUncoupleMovement) {
                enemy.aiMode = EnemyAiMode::Pursuit;
                if (!playerInvisible &&
                    perception.distanceToPlayer < GameplayConstants::kAssassinMinDistanceUnits) {
                    speed = 0.0F;
                    enemy.pathWaypointCount = 0;
                    enemy.pathWaypointIndex = 0;
                    enemy.expectedPathCellHash = -1;
                    enemy.cachedFlowFromCellHash = -1;
                } else {
                    if (kUseAssassinFlowFieldOnlyNavigation) {
                        bool flowHeadingSelected = false;
                        if (kUseFlowFieldPathGuidance) {
                            flowHeadingSelected = TrySelectAssassinFlowNextStep(
                                cellCache, playerFlowField, enemy, movementHeading);
                        }

                        enemy.pathWaypointCount = 0;
                        enemy.pathWaypointIndex = 0;

                        if (!flowHeadingSelected) {
                            const Vec2f predicted{
                                .x = state.world.player.position.x +
                                     state.world.player.velocity.x *
                                         GameplayConstants::kEnemyAssassinPredictionSeconds,
                                .y = state.world.player.position.y +
                                     state.world.player.velocity.y *
                                         GameplayConstants::kEnemyAssassinPredictionSeconds,
                            };
                            movementHeading = QuantizeToEightDirections(
                                std::atan2(
                                    predicted.x - enemy.position.x,
                                    -(predicted.y - enemy.position.y)));
                        }
                    } else if (kUseAssassinAStarBackupNavigation) {
                        const float obstacleAhead = game::geometry::FreeDistanceAhead(
                            state.world, enemy.position, enemy.headingRadians, 2.0F,
                            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                            kEnemyPlanningClearanceScale);
                        const bool needRepathObstacle = obstacleAhead < 2.0F;
                        const bool needRepathEmpty =
                            enemy.pathWaypointCount <= 0 ||
                            enemy.pathWaypointIndex >= enemy.pathWaypointCount;
                        if (needRepathObstacle || needRepathEmpty) {
                            if (playerInvisible) {
                                BuildAssassinPathToFarRandomTarget(
                                    state, cellCache, enemy, enemyIndex, random);
                            } else {
                                BuildAssassinPath(state, cellCache, enemy, enemyIndex);
                            }
                        }

                        if (enemy.pathWaypointCount > 0 &&
                            enemy.pathWaypointIndex < enemy.pathWaypointCount) {
                            const Vec2f waypoint = enemy.pathWaypoints[static_cast<std::size_t>(
                                enemy.pathWaypointIndex)];
                            const Vec2f toWaypoint{
                                .x = waypoint.x - enemy.position.x,
                                .y = waypoint.y - enemy.position.y,
                            };
                            if (DistanceSq(waypoint, enemy.position) <= 0.36F) {
                                enemy.pathWaypointIndex += 1;
                                if (playerInvisible) {
                                    BuildAssassinPathToFarRandomTarget(
                                        state, cellCache, enemy, enemyIndex, random);
                                } else if (enemy.pathWaypointIndex < enemy.pathWaypointCount) {
                                    BuildAssassinPath(state, cellCache, enemy, enemyIndex);
                                }
                            }
                            const Vec2f stepDir = NormalizeOrZero(toWaypoint);
                            if (stepDir.x != 0.0F || stepDir.y != 0.0F) {
                                movementHeading =
                                    QuantizeToEightDirections(std::atan2(stepDir.x, -stepDir.y));
                            }
                        } else {
                            const Vec2f predicted{
                                .x = state.world.player.position.x +
                                     state.world.player.velocity.x *
                                         GameplayConstants::kEnemyAssassinPredictionSeconds,
                                .y = state.world.player.position.y +
                                     state.world.player.velocity.y *
                                         GameplayConstants::kEnemyAssassinPredictionSeconds,
                            };
                            movementHeading = QuantizeToEightDirections(
                                std::atan2(
                                    predicted.x - enemy.position.x,
                                    -(predicted.y - enemy.position.y)));
                        }
                    } else {
                        const Vec2f predicted{
                            .x = state.world.player.position.x +
                                 state.world.player.velocity.x *
                                     GameplayConstants::kEnemyAssassinPredictionSeconds,
                            .y = state.world.player.position.y +
                                 state.world.player.velocity.y *
                                     GameplayConstants::kEnemyAssassinPredictionSeconds,
                        };
                        movementHeading = QuantizeToEightDirections(
                            std::atan2(
                                predicted.x - enemy.position.x, -(predicted.y - enemy.position.y)));
                    }
                }
            }
        }
        if (handledByUncoupleMovement) {
            movementSourceLabel = "uncouple";
            movementSourceBucket = kAssassinWallPhaseUncouple;
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
            {
                profiling::ScopedProfile sepScope(
                    profiling::Scope::EnemyMovementSeparationProbe, true);
                constexpr float sepSq = GameplayConstants::kEnemyPreferredSeparationUnits *
                                        GameplayConstants::kEnemyPreferredSeparationUnits;
                float minDistSqToOthers = std::numeric_limits<float>::infinity();
                float currentMinDistSqToOthers = std::numeric_limits<float>::infinity();
                const float selfUncoupleScore =
                    enemyIndex < static_cast<int>(uncoupleEscapeScores.size())
                        ? uncoupleEscapeScores[static_cast<std::size_t>(enemyIndex)]
                        : -1000.0F;
                for (int i = 0; i < static_cast<int>(state.world.enemies.size()); ++i) {
                    if (i == enemyIndex) {
                        continue;
                    }
                    const EnemyTank& other = state.world.enemies[static_cast<std::size_t>(i)];
                    if (!other.alive) {
                        continue;
                    }
                    if (enemy.aiMode == EnemyAiMode::Uncouple &&
                        other.aiMode == EnemyAiMode::Uncouple &&
                        i < static_cast<int>(uncoupleEscapeScores.size())) {
                        const float otherScore = uncoupleEscapeScores[static_cast<std::size_t>(i)];
                        const bool otherYieldsToSelf =
                            (selfUncoupleScore > otherScore + kUncouplePriorityEpsilon) ||
                            (std::fabs(selfUncoupleScore - otherScore) <=
                                    kUncouplePriorityEpsilon &&
                                enemyIndex < i);
                        if (otherYieldsToSelf) {
                            continue;
                        }
                    }
                    currentMinDistSqToOthers = std::min(
                        currentMinDistSqToOthers, DistanceSq(enemy.position, other.position));
                    minDistSqToOthers =
                        std::min(minDistSqToOthers, DistanceSq(candidatePosition, other.position));
                }
                constexpr float kSeparationProgressEpsilonSq = 0.01F;
                const bool makingSeparationProgress =
                    minDistSqToOthers > currentMinDistSqToOthers + kSeparationProgressEpsilonSq;
                if (std::fabs(speed) > 0.0F && minDistSqToOthers < sepSq &&
                    !makingSeparationProgress) {
                    float turnHeading = movementHeading;
                    Vec2f turnCandidate = candidatePosition;
                    if (TrySeparationTurn(
                            state.world, state.world.enemies, enemyIndex, speed, deltaSeconds,
                            turnHeading, turnCandidate)) {
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
            }

            {
                profiling::ScopedProfile overlapScope(
                    profiling::Scope::EnemyMovementOverlapCheck, true);
                bool blocked = false;
                if (std::fabs(speed) > 0.0F) {
                    {
                        profiling::ScopedProfile scope(
                            profiling::Scope::EnemyMovementOverlapIsBlocked, true);
                        blocked = IsMovementBlockedByEnemies(
                            state.world.enemies, frameStartPositions, enemyIndex, previousPosition,
                            candidatePosition, GameplayConstants::kEnemyPreferredSeparationUnits,
                            &uncoupleEscapeScores);
                    }
                }
                if (blocked) {
                    float turnHeading = movementHeading;
                    Vec2f turnCandidate = candidatePosition;
                    bool foundTurn = false;
                    {
                        profiling::ScopedProfile scope(
                            profiling::Scope::EnemyMovementOverlapSeparationTurn, true);
                        foundTurn = TrySeparationTurn(
                            state.world, state.world.enemies, enemyIndex, speed, deltaSeconds,
                            turnHeading, turnCandidate);
                    }
                    bool turnValid = false;
                    if (foundTurn) {
                        profiling::ScopedProfile scope(
                            profiling::Scope::EnemyMovementOverlapTurnValid, true);
                        turnValid = !IsMovementBlockedByEnemies(
                            state.world.enemies, frameStartPositions, enemyIndex, previousPosition,
                            turnCandidate, GameplayConstants::kEnemyPreferredSeparationUnits,
                            &uncoupleEscapeScores);
                    }
                    if (turnValid) {
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
            }

            {
                profiling::ScopedProfile wallScope(profiling::Scope::EnemyMovementWallCheck, true);
                const bool moving = std::fabs(speed) > 0.001F;
                const bool segmentWallHit =
                    moving && SegmentIntersectsWall(
                                  state.world, previousPosition, candidatePosition,
                                  GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
                const bool edgeOnWallContact =
                    moving && !segmentWallHit &&
                    IsEdgeOnWallContact(state.world, candidatePosition, movementHeading);
                if (enemy.type == EnemyType::Assassin && moving) {
                    const bool candidateInsideWallAvoid = game::geometry::IsPointInWall(
                        state.world, candidatePosition, GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
                    if (!startedInsideWallAvoid && candidateInsideWallAvoid) {
                        const float clearAhead = game::geometry::FreeDistanceAhead(
                            state.world,
                            previousPosition,
                            movementHeading,
                            std::max(1.5F, Distance(candidatePosition, previousPosition) + 0.5F),
                            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
                            1.0F);
                        bolt::log::Profile(
                            "[ENEMY_ASSASSIN_WALL_CAUSE] id=%d source=%s "
                            "posPrev=(%.3f,%.3f) posCandidate=(%.3f,%.3f) heading=%.3f "
                            "speed=%.3f delta=%.3f clearAhead=%.3f segmentWallHit=%d edgeOnWall=%d "
                            "insideWallAvoidPrev=%d insideWallAvoidCandidate=%d aiMode=%s\n",
                            enemyIndex,
                            movementSourceLabel,
                            previousPosition.x,
                            previousPosition.y,
                            candidatePosition.x,
                            candidatePosition.y,
                            movementHeading,
                            speed,
                            deltaSeconds,
                            clearAhead,
                            segmentWallHit ? 1 : 0,
                            edgeOnWallContact ? 1 : 0,
                            startedInsideWallAvoid ? 1 : 0,
                            candidateInsideWallAvoid ? 1 : 0,
                            EnemyAiModeLabel(enemy.aiMode));
                    }
                }
                if (segmentWallHit || edgeOnWallContact) {
                    const float movedLastFrameUnits = Distance(candidatePosition, previousPosition);
                    enemy.velocity = Vec2f{.x = 0.0F, .y = 0.0F};
                    // Only zero timer when entering uncouple from non-uncouple; preserve it on
                    // re-entry so the assassin can accumulate movement progress and eventually
                    // escape.
                    if (enemy.aiMode != EnemyAiMode::Uncouple) {
                        enemy.aiStateTimerSeconds = 0.0F;
                    }
                    // Wall contact enters uncouple so wall and neighbor repulsion can resolve local
                    // jams.
                    EnterUncoupleMode(
                        state.world.enemies, enemyIndex, enemyIndex,
                        UncoupleReason::SelfWallContact, movedLastFrameUnits);
                } else {
                    enemy.velocity = Vec2f{
                        .x = snappedDirection.x * speed,
                        .y = snappedDirection.y * speed,
                    };
                    enemy.position = candidatePosition;
                    enemy.headingRadians = movementHeading;
                }
                if (enemy.type == EnemyType::Assassin) {
                    const bool nowInsideWallAvoid = game::geometry::IsPointInWall(
                        state.world, enemy.position, GameplayConstants::kEnemyWallAvoidanceRadiusUnits);
                    if (!startedInsideWallAvoid && nowInsideWallAvoid) {
                        gEnemyRuntimeWindowStats.assassinWallAvoidEntriesByPhase
                            [static_cast<std::size_t>(movementSourceBucket)] += 1;
                        const game::navigation::MazeCellCoord wallCell = cellCache.WorldToCell(enemy.position);
                        const int wallCellHash = cellCache.CellHash(wallCell.x, wallCell.y);
                        const int flowNextHash = playerFlowField.HasBuild()
                            ? playerFlowField.NextCellHash(wallCellHash)
                            : -1;
                        const bool insideWallTank = game::geometry::IsPointInWall(
                            state.world, enemy.position, GameplayConstants::kTankCollisionRadiusUnits);
                        const bool insideBase = game::geometry::IsPointInUndestroyedBase(
                            state.world, enemy.position, GameplayConstants::kTankCollisionRadiusUnits);
                        bolt::log::Profile(
                            "[ENEMY_ASSASSIN_WALL_STATE_CHANGE] id=%d phase=%s insideWallAvoid=1 "
                            "posFrom=(%.3f,%.3f) posTo=(%.3f,%.3f) cell=(%d,%d) cellHash=%d "
                            "flowNextHash=%d heading=%.3f speed=%.3f aiMode=%s "
                            "insideWallTank=%d insideBase=%d\n",
                            enemyIndex,
                            movementSourceLabel,
                            previousPosition.x,
                            previousPosition.y,
                            enemy.position.x,
                            enemy.position.y,
                            wallCell.x,
                            wallCell.y,
                            wallCellHash,
                            flowNextHash,
                            movementHeading,
                            speed,
                            EnemyAiModeLabel(enemy.aiMode),
                            insideWallTank ? 1 : 0,
                            insideBase ? 1 : 0);
                    }
                    enemy.cheapSegmentInsideWallAvoidLastFrame = nowInsideWallAvoid;
                }
            }

            if (enemy.type == EnemyType::Torpedo &&
                enemy.torpedoMoveMode == TorpedoMoveMode::Move) {
                const float movedDistance = Distance(enemy.position, previousPosition);
                if (movedDistance > 0.0001F) {
                    enemy.torpedoStraightDistanceSinceTurnUnits += movedDistance;
                    enemy.torpedoMoveDecisionHoldRemainingUnits =
                        std::max(0.0F, enemy.torpedoMoveDecisionHoldRemainingUnits - movedDistance);
                }
            } else if (enemy.type == EnemyType::Torpedo &&
                       enemy.torpedoMoveMode == TorpedoMoveMode::Retreat) {
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

        const game::navigation::MazeCellCoord fullCell = cellCache.WorldToCell(enemy.position);
        enemy.cellCoord = fullCell;
        occupancy.SetCell(enemyIndex, fullCell.x, fullCell.y);
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
        gEnemyRuntimeStats.aliveCount, gEnemyRuntimeStats.visibleInViewportCount,
        gEnemyRuntimeStats.fullTierCount);
    AccumulateEnemyWindowTime(deltaSeconds);

    if (gEnemyRuntimeStats.fullTierCount >= 2) {
        game::spatial::SweepPruneBroadPhase& broadPhase = state.world.collisionCache.sweepPrune;
        ResolveEnemyCollisionsSinglePass(
            state.world, gEnemyRuntimeStats, frameStartPositions, broadPhase, fullTierMask,
            reenteredFullTierMask, EnterUncoupleByReasonCode, ShouldEnterSeparationUncouple);
        gEnemyRuntimeWindowStats.collisionPassRuns += 1;
    } else {
        gEnemyRuntimeWindowStats.collisionPassSkips += 1;
    }
    AccumulateEnemyWindowPairStats(gEnemyRuntimeStats);

    const auto& profiler = profiling::Profiler::Instance();
    const std::uint64_t frameIndex = profiler.FrameIndex();
    if (profiler.ShouldEmitPeriodicReport() && frameIndex != gLastEnemyStatsPrintedFrame) {
        gLastEnemyStatsPrintedFrame = frameIndex;
        EmitEnemyPeriodicWindowLogs(gEnemyRuntimeStats, frameIndex);
    }
}

const EnemyRuntimeStats& GetEnemyRuntimeStats() { return gEnemyRuntimeStats; }

void DebugLogEnemiesAtPosition(
    const GameState& state,
    const Vec2f& worldPosition,
    const game::navigation::MazeCellCoord& clickedCell)
{
    constexpr float kClickMatchRadiusUnits = 1.5F;
    const float matchRadiusSq = kClickMatchRadiusUnits * kClickMatchRadiusUnits;
    const game::navigation::CellCoordCache& cellCache = state.world.navigationCache.cellCoords;
    const bool flowHasBuild = state.world.navigationCache.playerFlowField.HasBuild();
    int loggedCount = 0;

    for (int i = 0; i < static_cast<int>(state.world.enemies.size()); ++i) {
        const EnemyTank& enemy = state.world.enemies[static_cast<std::size_t>(i)];
        if (!enemy.alive) {
            continue;
        }
        const bool cellMatch = enemy.cellCoord.x == clickedCell.x && enemy.cellCoord.y == clickedCell.y;
        const bool radiusMatch = DistanceSq(enemy.position, worldPosition) <= matchRadiusSq;
        if (!cellMatch && !radiusMatch) {
            continue;
        }

        const bool insideWall = game::geometry::IsPointInWall(
            state.world, enemy.position, GameplayConstants::kTankCollisionRadiusUnits);
        const bool insideBase = game::geometry::IsPointInUndestroyedBase(
            state.world, enemy.position, GameplayConstants::kTankCollisionRadiusUnits);
        const float clearAhead = game::geometry::FreeDistanceAhead(
            state.world,
            enemy.position,
            enemy.headingRadians,
            6.0F,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
            1.0F);
        const float distPlayer = Distance(enemy.position, state.world.player.position);
        const float nearestBase = NearestBaseDistance(state.world, enemy.position);
        const int enemyCellHash = cellCache.CellHash(enemy.cellCoord.x, enemy.cellCoord.y);
        const int flowNextHash =
            flowHasBuild ? state.world.navigationCache.playerFlowField.NextCellHash(enemyCellHash) : -1;
        const bool hasCurrentWaypoint =
            enemy.pathWaypointCount > 0 &&
            enemy.pathWaypointIndex >= 0 &&
            enemy.pathWaypointIndex < enemy.pathWaypointCount;
        const Vec2f currentWaypoint = hasCurrentWaypoint
            ? enemy.pathWaypoints[static_cast<std::size_t>(enemy.pathWaypointIndex)]
            : Vec2f{.x = 0.0F, .y = 0.0F};

        bolt::log::Profile(
            "[ENEMY_CLICK_DEBUG_ENEMY] id=%d matchCell=%d matchRadius=%d "
            "type=%s subtype=%s tier=%s ai=%s preUncouple=%s alive=%d "
            "pos=(%.3f,%.3f) vel=(%.3f,%.3f) heading=%.3f desiredHeading=%.3f "
            "cell=(%d,%d) cellHash=%d clickCell=(%d,%d)\n",
            i,
            cellMatch ? 1 : 0,
            radiusMatch ? 1 : 0,
            EnemyTypeLabel(enemy.type),
            EnemySubtypeLabel(enemy.subtype),
            EnemySimTierLabel(enemy.simTier),
            EnemyAiModeLabel(enemy.aiMode),
            EnemyAiModeLabel(enemy.preUncoupleAiMode),
            enemy.alive ? 1 : 0,
            enemy.position.x,
            enemy.position.y,
            enemy.velocity.x,
            enemy.velocity.y,
            enemy.headingRadians,
            enemy.desiredHeadingRadians,
            enemy.cellCoord.x,
            enemy.cellCoord.y,
            enemyCellHash,
            clickedCell.x,
            clickedCell.y);

        bolt::log::Profile(
            "[ENEMY_CLICK_DEBUG_STATE] id=%d fireCd=%.3f aiState=%.3f aiElapsed=%.3f "
            "selfAwareInt=%.3f selfAwareTimer=%.3f insideWall=%d insideBase=%d "
            "distPlayer=%.3f nearestBase=%.3f clearAhead6=%.3f torpedoMode=%s "
            "torpedoStraight=%.3f hold=%.3f retreat=%.3f detectTimer=%.3f detected=%d "
            "waypoints=%d idx=%d hasWp=%d wp=(%.3f,%.3f)\n",
            i,
            enemy.fireCooldownSeconds,
            enemy.aiStateTimerSeconds,
            enemy.aiModeElapsedSeconds,
            enemy.selfAwarenessIntervalSeconds,
            enemy.selfAwarenessTimerSeconds,
            insideWall ? 1 : 0,
            insideBase ? 1 : 0,
            distPlayer,
            nearestBase,
            clearAhead,
            TorpedoMoveModeLabel(enemy.torpedoMoveMode),
            enemy.torpedoStraightDistanceSinceTurnUnits,
            enemy.torpedoMoveDecisionHoldRemainingUnits,
            enemy.torpedoRetreatMovedUnits,
            enemy.torpedoPlayerDetectTimerSeconds,
            enemy.torpedoPlayerDetected ? 1 : 0,
            enemy.pathWaypointCount,
            enemy.pathWaypointIndex,
            hasCurrentWaypoint ? 1 : 0,
            currentWaypoint.x,
            currentWaypoint.y);

        bolt::log::Profile(
            "[ENEMY_CLICK_DEBUG_NAV] id=%d cachedPlayerHash=%d expectedPathHash=%d "
            "cachedFlowFromHash=%d cachedFlowHeading=%.3f flowHasBuild=%d flowNextHash=%d "
            "offscreenActive=%d offscreenHeading=%.3f offscreenEnd=(%.3f,%.3f) "
            "cheapCrowdedSlow=%d cheapFailCount=%d cheapLastFailCellHash=%d "
            "cheapLastFailReason=%d cheapMethodStage=%d\n",
            i,
            enemy.cachedPlayerCellHash,
            enemy.expectedPathCellHash,
            enemy.cachedFlowFromCellHash,
            enemy.cachedFlowHeadingRadians,
            flowHasBuild ? 1 : 0,
            flowNextHash,
            enemy.offscreenSegmentActive ? 1 : 0,
            enemy.offscreenCachedHeadingRadians,
            enemy.offscreenSegmentEnd.x,
            enemy.offscreenSegmentEnd.y,
            enemy.cheapTierCrowdedSlowMode ? 1 : 0,
            enemy.cheapSegmentBuildFailCount,
            enemy.cheapSegmentLastFailCellHash,
            static_cast<int>(enemy.cheapSegmentLastFailReason),
            enemy.cheapSegmentBuildMethodStage);
        loggedCount += 1;
    }

    bolt::log::Profile(
        "[ENEMY_CLICK_DEBUG_RESULT] clickWorld=(%.3f,%.3f) clickCell=(%d,%d) found=%d\n",
        worldPosition.x,
        worldPosition.y,
        clickedCell.x,
        clickedCell.y,
        loggedCount);
}
