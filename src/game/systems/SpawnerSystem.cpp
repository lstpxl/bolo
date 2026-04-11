#include "game/systems/SpawnerSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include "core/AngleMath.h"
#include "core/Random.h"
#include "game/EnemyAppearance.h"
#include "game/GameQueries.h"
#include "game/geometry/WorldGeometry.h"

namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kDiagonalSpawnCoreShiftUnits = 0.5F;
constexpr float kRequiredSpawnClearUnits = 6.0F;
constexpr float kSpawnProbeMaxUnits = 8.0F;

game::EnemySpawnChoice PickSpawnEnemyForLevel(int level, Random& random) {
    struct LevelChoices {
        game::EnemySpawnChoice choices[3];
        int count;
    };
    // Max 3 choices per level; mirrors EnemyTypesForLevel() but avoids a heap allocation.
    static const LevelChoices kByLevel[] = {
        /* 1 */ {{{EnemyType::Drone,    EnemySubtype::Basic},    {}, {}}, 1},
        /* 2 */ {{{EnemyType::Drone,    EnemySubtype::Advanced}, {}, {}}, 1},
        /* 3 */ {{{EnemyType::Drone,    EnemySubtype::Advanced},
                  {EnemyType::Torpedo,  EnemySubtype::Basic},    {}}, 2},
        /* 4 */ {{{EnemyType::Drone,    EnemySubtype::Advanced},
                  {EnemyType::Torpedo,  EnemySubtype::Advanced}, {}}, 2},
        /* 5 */ {{{EnemyType::Drone,    EnemySubtype::Advanced},
                  {EnemyType::Torpedo,  EnemySubtype::Advanced},
                  {EnemyType::Hunter,   EnemySubtype::Basic}},      3},
        /* 6 */ {{{EnemyType::Drone,    EnemySubtype::Advanced},
                  {EnemyType::Torpedo,  EnemySubtype::Advanced},
                  {EnemyType::Hunter,   EnemySubtype::Advanced}},   3},
        /* 7 */ {{{EnemyType::Hunter,   EnemySubtype::Advanced}, {}, {}}, 1},
        /* 8 */ {{{EnemyType::Hunter,   EnemySubtype::Advanced},
                  {EnemyType::Assassin, EnemySubtype::Advanced}, {}}, 2},
        /* 9 */ {{{EnemyType::Assassin, EnemySubtype::Advanced}, {}, {}}, 1},
    };
    static constexpr game::EnemySpawnChoice kDefault{
        .type = EnemyType::Drone, .subtype = EnemySubtype::Advanced};

    static constexpr int kLevelCount = static_cast<int>(sizeof(kByLevel) / sizeof(kByLevel[0]));
    if (level < 1 || level > kLevelCount) {
        return kDefault;
    }
    const LevelChoices& entry = kByLevel[static_cast<std::size_t>(level - 1)];
    if (entry.count <= 0) {
        return kDefault;
    }
    const int pickedIndex = random.NextInt(0, entry.count - 1);
    return entry.choices[static_cast<std::size_t>(pickedIndex)];
}

struct SpawnRayChoice {
    bool found = false;
    float heading = 0.0F;
    float clearDistance = 0.0F;
};

struct PendingSpawnCandidate {
    bool found = false;
    EnemyType type = EnemyType::Drone;
    EnemySubtype subtype = EnemySubtype::Advanced;
    float heading = 0.0F;
    Vec2f position{.x = 0.0F, .y = 0.0F};
};

BaseOuterSegment MostDamagedSegment(const EnemyBase& base) {
    BaseOuterSegment mostDamaged = BaseOuterSegment::Top;
    int minHealth = base.topSegmentHealth;
    const auto consider = [&](BaseOuterSegment segment) {
        const int health = base.SegmentHealth(segment);
        if (health < minHealth) {
            minHealth = health;
            mostDamaged = segment;
        }
    };
    consider(BaseOuterSegment::Right);
    consider(BaseOuterSegment::Bottom);
    consider(BaseOuterSegment::Left);
    return mostDamaged;
}

void TickBaseRepair(EnemyBase& base, float deltaSeconds) {
    if (!base.HasDamagedSegments()) {
        base.repairCountdownSeconds = 0.0F;
        return;
    }
    if (base.repairCountdownSeconds <= 0.0F) {
        base.repairCountdownSeconds = GameplayConstants::kBaseRepairDelaySeconds;
    }
    base.repairCountdownSeconds -= deltaSeconds;
    while (base.repairCountdownSeconds <= 0.0F && base.HasDamagedSegments()) {
        int& health = base.SegmentHealthRef(MostDamagedSegment(base));
        health = std::min(
            GameplayConstants::kBaseOuterSegmentMaxHealth,
            health + GameplayConstants::kBaseRepairHealthPerTick);
        if (base.HasDamagedSegments()) {
            base.repairCountdownSeconds += GameplayConstants::kBaseRepairDelaySeconds;
        } else {
            base.repairCountdownSeconds = 0.0F;
        }
    }
}

bool IsDirectionBlockedByDamagedSegments(const EnemyBase& base, const Vec2f& direction) {
    if (base.IsSegmentDamaged(BaseOuterSegment::Top) && direction.y < -0.5F) {
        return true;
    }
    if (base.IsSegmentDamaged(BaseOuterSegment::Bottom) && direction.y > 0.5F) {
        return true;
    }
    if (base.IsSegmentDamaged(BaseOuterSegment::Right) && direction.x > 0.5F) {
        return true;
    }
    if (base.IsSegmentDamaged(BaseOuterSegment::Left) && direction.x < -0.5F) {
        return true;
    }
    return false;
}

SpawnRayChoice PickSpawnDirection(
    const WorldState& world,
    const EnemyBase& base,
    Random& random,
    bool ignoreDamagedSegmentDirectionBlock) {
    constexpr std::array<float, 8> kHeadings{
        0.0F, kPi * 0.25F, kPi * 0.5F, kPi * 0.75F, kPi, kPi * 1.25F, kPi * 1.5F, kPi * 1.75F};

    std::array<SpawnRayChoice, 8> choices{};
    std::array<int, 8> validIndices{};
    int validCount = 0;
    int bestIndex = 0;
    float bestClear = -1.0F;
    for (int i = 0; i < static_cast<int>(kHeadings.size()); ++i) {
        const float heading = kHeadings[static_cast<std::size_t>(i)];
        const Vec2f direction = core::angle::DirectionFromHeading(heading);
        if (!ignoreDamagedSegmentDirectionBlock && IsDirectionBlockedByDamagedSegments(base, direction)) {
            continue;
        }
        const float clearDistance = game::geometry::FreeDistanceAhead(
            world,
            base.position,
            heading,
            kSpawnProbeMaxUnits,
            GameplayConstants::kWallClearanceForAvoidance);
        choices[static_cast<std::size_t>(i)] = SpawnRayChoice{
            .heading = heading,
            .clearDistance = clearDistance,
        };
        if (clearDistance > bestClear) {
            bestClear = clearDistance;
            bestIndex = i;
        }
        if (clearDistance >= kRequiredSpawnClearUnits) {
            validIndices[static_cast<std::size_t>(validCount)] = i;
            ++validCount;
        }
    }

    if (validCount > 0) {
        const int picked = validIndices[static_cast<std::size_t>(random.NextInt(0, validCount - 1))];
        SpawnRayChoice choice = choices[static_cast<std::size_t>(picked)];
        choice.found = true;
        return choice;
    }
    (void)bestIndex;
    (void)bestClear;
    return SpawnRayChoice{};
}

bool IsSpawnPositionFree(const GameState& state, const Vec2f& spawnPosition) {
    if (game::geometry::IsPointInWall(state.world, spawnPosition, GameplayConstants::kWallClearanceForAvoidance)) {
        return false;
    }
    const float minDistanceSq =
        GameplayConstants::kEnemyPreferredSeparationUnits * GameplayConstants::kEnemyPreferredSeparationUnits;
    for (const EnemyTank& enemy : state.world.enemies) {
        if (!enemy.alive) {
            continue;
        }
        const float dx = spawnPosition.x - enemy.position.x;
        const float dy = spawnPosition.y - enemy.position.y;
        const float distanceSq = dx * dx + dy * dy;
        if (distanceSq < minDistanceSq) {
            return false;
        }
    }
    return true;
}

bool IsDiagonalDirection(const Vec2f& dir) {
    return std::fabs(dir.x) > 0.5F && std::fabs(dir.y) > 0.5F;
}

bool IsPointInsideBaseFootprint(const EnemyBase& base, const Vec2f& point) {
    const float halfBase = GameplayConstants::kEnemyBaseSizeUnits * 0.5F;
    const float sideInsetUnitsPerHealthPoint = 3.0F / static_cast<float>(GameplayConstants::kPixelsPerUnit);
    const auto sideInset = [&](int health) {
        const int clamped = std::clamp(health, 0, GameplayConstants::kBaseOuterSegmentMaxHealth);
        return static_cast<float>(GameplayConstants::kBaseOuterSegmentMaxHealth - clamped) * sideInsetUnitsPerHealthPoint;
    };
    const float minX = base.position.x - halfBase + sideInset(base.leftSegmentHealth);
    const float maxX = base.position.x + halfBase - sideInset(base.rightSegmentHealth);
    const float minY = base.position.y - halfBase + sideInset(base.topSegmentHealth);
    const float maxY = base.position.y + halfBase - sideInset(base.bottomSegmentHealth);
    return point.x >= minX && point.x <= maxX && point.y >= minY && point.y <= maxY;
}

bool HasOriginEnemyInsideBaseFootprint(const GameState& state, int baseIndex, const EnemyBase& base) {
    for (const EnemyTank& enemy : state.world.enemies) {
        if (!enemy.alive || enemy.originBaseIndex != baseIndex) {
            continue;
        }
        if (IsPointInsideBaseFootprint(base, enemy.position)) {
            return true;
        }
    }
    return false;
}

void ResetSpawnTimerAfterFailedAttempt(EnemyBase& base) {
    base.enemyGenerationTimerSeconds = base.enemyGenerationIntervalSeconds;
}

bool BuildPendingSpawnCandidate(
    const GameState& state,
    const EnemyBase& base,
    Random& random,
    PendingSpawnCandidate& outCandidate) {
    outCandidate = PendingSpawnCandidate{};
    const game::EnemySpawnChoice spawnedEnemy = PickSpawnEnemyForLevel(state.menuSettings.levelNumber, random);
    SpawnRayChoice spawnDirection = PickSpawnDirection(state.world, base, random, false);
    if (!spawnDirection.found) {
        return false;
    }

    const Vec2f dir = core::angle::DirectionFromHeading(spawnDirection.heading);
    const float baseHalfSizeUnits = GameplayConstants::kEnemyBaseSizeUnits * 0.5F;
    const bool diagonalSpawn = IsDiagonalDirection(dir);
    Vec2f noseAnchor = Vec2f{
        .x = base.position.x + dir.x * baseHalfSizeUnits,
        .y = base.position.y + dir.y * baseHalfSizeUnits,
    };
    if (diagonalSpawn) {
        noseAnchor = Vec2f{
            .x = base.position.x + (dir.x >= 0.0F ? baseHalfSizeUnits : -baseHalfSizeUnits),
            .y = base.position.y + (dir.y >= 0.0F ? baseHalfSizeUnits : -baseHalfSizeUnits),
        };
    }
    const float spawnBackoffUnits =
        GameplayConstants::kEntitySizeUnits * 0.5F +
        (diagonalSpawn ? kDiagonalSpawnCoreShiftUnits : 0.0F);
    const Vec2f spawnPosition{
        .x = noseAnchor.x - dir.x * spawnBackoffUnits,
        .y = noseAnchor.y - dir.y * spawnBackoffUnits,
    };
    if (!IsSpawnPositionFree(state, spawnPosition)) {
        return false;
    }

    const float forwardClearWithEnemies = game::geometry::FreeDistanceAheadWithEnemies(
        state.world,
        state.world.enemies,
        -1,
        spawnPosition,
        spawnDirection.heading,
        kRequiredSpawnClearUnits,
        GameplayConstants::kWallClearanceForAvoidance);
    if (forwardClearWithEnemies < kRequiredSpawnClearUnits) {
        return false;
    }

    outCandidate.found = true;
    outCandidate.type = spawnedEnemy.type;
    outCandidate.subtype = spawnedEnemy.subtype;
    outCandidate.heading = spawnDirection.heading;
    outCandidate.position = spawnPosition;
    return true;
}

void SpawnEnemyFromBasePending(GameState& state, EnemyBase& base, int baseIndex, Random& random) {
    EnemyAiMode mode = EnemyAiMode::Wander;
    if (base.pendingSpawnType == EnemyType::Hunter) {
        mode = EnemyAiMode::Scout;
    } else if (base.pendingSpawnType == EnemyType::Assassin) {
        mode = EnemyAiMode::Pursuit;
    } else if (base.pendingSpawnType == EnemyType::Torpedo) {
        mode = EnemyAiMode::Fly;
    }

    const float selfAwarenessInterval = (base.pendingSpawnType == EnemyType::Drone)
        ? random.NextFloat(
              GameplayConstants::kDroneSelfAwarenessIntervalMinSeconds,
              GameplayConstants::kDroneSelfAwarenessIntervalMaxSeconds)
        : random.NextFloat(4.0F, 8.0F);

    state.world.enemies.push_back(EnemyTank{
        .position = base.pendingSpawnPosition,
        .velocity = Vec2f{.x = 0.0F, .y = 0.0F},
        .headingRadians = base.pendingSpawnHeadingRadians,
        .type = base.pendingSpawnType,
        .subtype = base.pendingSpawnSubtype,
        .aiMode = mode,
        .fireCooldownSeconds = GameplayConstants::kEnemyInitialFireCooldownSeconds,
        .aiStateTimerSeconds = 0.0F,
        .aiModeElapsedSeconds = 0.0F,
        .selfAwarenessIntervalSeconds = selfAwarenessInterval,
        .selfAwarenessTimerSeconds = selfAwarenessInterval,
        .desiredHeadingRadians = 0.0F,
        .wanderDirection = Vec2f{.x = 0.0F, .y = -1.0F},
        .originBaseIndex = baseIndex,
        .pathWaypoints = {},
        .pathWaypointCount = 0,
        .pathWaypointIndex = 0,
        .spawnExitLockActive = true,
        .alive = true,
        .spawnSessionId = state.world.nextEnemySpawnSessionId++,
    });
    state.world.gameplayEvents.Push(GameplayEvent{
        .type = GameplayEventType::EnemySpawned,
        .position = base.pendingSpawnPosition,
        .enemyType = base.pendingSpawnType,
        .enemySubtype = base.pendingSpawnSubtype,
        .baseIndex = baseIndex,
    });
}
}  // namespace

void UpdateSpawnerSystem(GameState& state, float deltaSeconds, Random& random) {
    for (EnemyBase& base : state.world.enemyBases) {
        base.activeEnemies = 0;
    }
    for (const EnemyTank& enemy : state.world.enemies) {
        if (!enemy.alive) {
            continue;
        }
        if (enemy.originBaseIndex < 0 || enemy.originBaseIndex >= static_cast<int>(state.world.enemyBases.size())) {
            continue;
        }
        EnemyBase& origin = state.world.enemyBases[static_cast<std::size_t>(enemy.originBaseIndex)];
        if (!origin.destroyed) {
            origin.activeEnemies += 1;
        }
    }

    int aliveEnemies = game::queries::CountAliveEnemies(state);
    for (int baseIndex = 0; baseIndex < static_cast<int>(state.world.enemyBases.size()); ++baseIndex) {
        EnemyBase& base = state.world.enemyBases[static_cast<std::size_t>(baseIndex)];
        if (base.destroyed) {
            base.activeEnemies = 0;
            base.repairCountdownSeconds = 0.0F;
            base.spawnPreparationActive = false;
            base.spawnPreparationRemainingSeconds = 0.0F;
            continue;
        }

        TickBaseRepair(base, deltaSeconds);

        if (base.enemyGenerationIntervalSeconds <= 0.0F) {
            base.enemyGenerationIntervalSeconds = GameplayConstants::kBaseSpawnCooldownSeconds;
        }
        base.enemyGenerationTimerSeconds -= deltaSeconds;
        const int maxPerBase = (state.menuSettings.levelNumber == 9) ? 6 : GameplayConstants::kMaxAliveEnemiesPerBase;
        if (base.spawnPreparationActive) {
            base.spawnPreparationRemainingSeconds -= deltaSeconds;
            if (base.spawnPreparationRemainingSeconds > 0.0F) {
                continue;
            }
            base.spawnPreparationActive = false;
            base.spawnPreparationRemainingSeconds = 0.0F;
            SpawnEnemyFromBasePending(state, base, baseIndex, random);
            base.activeEnemies += 1;
            base.enemyGenerationTimerSeconds = std::max(
                0.0F,
                base.enemyGenerationIntervalSeconds - GameplayConstants::kBaseSpawnCoreGrowDurationSeconds);
            ++aliveEnemies;
            continue;
        }

        if (aliveEnemies >= GameplayConstants::kMaxAliveEnemies ||
            base.enemyGenerationTimerSeconds > 0.0F ||
            base.activeEnemies >= maxPerBase ||
            HasOriginEnemyInsideBaseFootprint(state, baseIndex, base)) {
            continue;
        }

        PendingSpawnCandidate candidate{};
        if (!BuildPendingSpawnCandidate(state, base, random, candidate)) {
            ResetSpawnTimerAfterFailedAttempt(base);
            continue;
        }

        base.pendingSpawnType = candidate.type;
        base.pendingSpawnSubtype = candidate.subtype;
        base.pendingSpawnHeadingRadians = candidate.heading;
        base.pendingSpawnPosition = candidate.position;
        base.spawnPreparationActive = true;
        base.spawnPreparationRemainingSeconds = GameplayConstants::kBaseSpawnCoreGrowDurationSeconds;
    }
}
