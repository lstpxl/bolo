#include "app/AudioEventRouter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "game/GameQueries.h"

namespace {
float Distance(const Vec2f& a, const Vec2f& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

float ComputeSpatialVolume(const Vec2f& listener, const Vec2f& source) {
    const float r1 = 3.0F * static_cast<float>(GameplayConstants::kMazeCellSizeUnits);
    const float r2 = 10.0F * static_cast<float>(GameplayConstants::kMazeCellSizeUnits);
    const float d = Distance(listener, source);
    if (d > r2) {
        return 0.0F;
    }
    if (d <= r1) {
        return 1.0F;
    }
    return 1.0F - (d - r1) / (r2 - r1);
}

void PlaySpatialSound(Sound& sound, const Vec2f& listener, const Vec2f& source) {
    const float volume = ComputeSpatialVolume(listener, source);
    if (volume <= 0.0F) {
        return;
    }
    SetSoundVolume(sound, volume);
    PlaySound(sound);
}

bool FindClosestFreshProjectilePosition(
    const GameState& state,
    ProjectileOwner owner,
    float stepSeconds,
    const Vec2f& listener,
    Vec2f& outPosition) {
    const float freshThreshold = GameplayConstants::kProjectileLifetimeSeconds - stepSeconds - 0.001F;
    bool found = false;
    float bestDistance = std::numeric_limits<float>::infinity();
    for (const Projectile& projectile : state.world.projectiles) {
        if (!projectile.alive || projectile.owner != owner) {
            continue;
        }
        if (projectile.remainingLifeSeconds < freshThreshold) {
            continue;
        }
        const float distance = Distance(listener, projectile.position);
        if (!found || distance < bestDistance) {
            found = true;
            bestDistance = distance;
            outPosition = projectile.position;
        }
    }
    return found;
}

bool FindClosestRemovedEnemyPosition(
    const GameState& before,
    const GameState& after,
    const Vec2f& listener,
    Vec2f& outPosition) {
    std::vector<Vec2f> afterAlivePositions{};
    afterAlivePositions.reserve(after.world.enemies.size());
    for (const EnemyTank& enemy : after.world.enemies) {
        if (enemy.alive) {
            afterAlivePositions.push_back(enemy.position);
        }
    }
    std::vector<bool> matched(afterAlivePositions.size(), false);
    std::vector<Vec2f> removedPositions{};
    constexpr float kMatchDistanceUnits = 1.5F;
    for (const EnemyTank& beforeEnemy : before.world.enemies) {
        if (!beforeEnemy.alive) {
            continue;
        }
        int bestIndex = -1;
        float bestDist = kMatchDistanceUnits;
        for (int i = 0; i < static_cast<int>(afterAlivePositions.size()); ++i) {
            if (matched[static_cast<std::size_t>(i)]) {
                continue;
            }
            const float dist = Distance(beforeEnemy.position, afterAlivePositions[static_cast<std::size_t>(i)]);
            if (dist <= bestDist) {
                bestDist = dist;
                bestIndex = i;
            }
        }
        if (bestIndex >= 0) {
            matched[static_cast<std::size_t>(bestIndex)] = true;
        } else {
            removedPositions.push_back(beforeEnemy.position);
        }
    }
    if (removedPositions.empty()) {
        return false;
    }
    float bestListenerDistance = std::numeric_limits<float>::infinity();
    for (const Vec2f& pos : removedPositions) {
        const float dist = Distance(listener, pos);
        if (dist < bestListenerDistance) {
            bestListenerDistance = dist;
            outPosition = pos;
        }
    }
    return true;
}

bool FindClosestSpawnedEnemyPosition(
    const GameState& before,
    const GameState& after,
    const Vec2f& listener,
    Vec2f& outPosition) {
    std::vector<Vec2f> beforeAlivePositions{};
    beforeAlivePositions.reserve(before.world.enemies.size());
    for (const EnemyTank& enemy : before.world.enemies) {
        if (enemy.alive) {
            beforeAlivePositions.push_back(enemy.position);
        }
    }
    std::vector<bool> matched(beforeAlivePositions.size(), false);
    std::vector<Vec2f> spawnedPositions{};
    constexpr float kMatchDistanceUnits = 1.5F;
    for (const EnemyTank& afterEnemy : after.world.enemies) {
        if (!afterEnemy.alive) {
            continue;
        }
        int bestIndex = -1;
        float bestDist = kMatchDistanceUnits;
        for (int i = 0; i < static_cast<int>(beforeAlivePositions.size()); ++i) {
            if (matched[static_cast<std::size_t>(i)]) {
                continue;
            }
            const float dist = Distance(afterEnemy.position, beforeAlivePositions[static_cast<std::size_t>(i)]);
            if (dist <= bestDist) {
                bestDist = dist;
                bestIndex = i;
            }
        }
        if (bestIndex >= 0) {
            matched[static_cast<std::size_t>(bestIndex)] = true;
        } else {
            spawnedPositions.push_back(afterEnemy.position);
        }
    }
    if (spawnedPositions.empty()) {
        return false;
    }
    float bestListenerDistance = std::numeric_limits<float>::infinity();
    for (const Vec2f& pos : spawnedPositions) {
        const float dist = Distance(listener, pos);
        if (dist < bestListenerDistance) {
            bestListenerDistance = dist;
            outPosition = pos;
        }
    }
    return true;
}

bool FindClosestDestroyedBasePosition(
    const GameState& before,
    const GameState& after,
    const Vec2f& listener,
    Vec2f& outPosition) {
    bool found = false;
    float bestListenerDistance = std::numeric_limits<float>::infinity();
    const int count = std::min(
        static_cast<int>(before.world.enemyBases.size()),
        static_cast<int>(after.world.enemyBases.size()));
    for (int i = 0; i < count; ++i) {
        const EnemyBase& beforeBase = before.world.enemyBases[static_cast<std::size_t>(i)];
        const EnemyBase& afterBase = after.world.enemyBases[static_cast<std::size_t>(i)];
        if (beforeBase.destroyed || !afterBase.destroyed) {
            continue;
        }
        const float dist = Distance(listener, afterBase.position);
        if (!found || dist < bestListenerDistance) {
            found = true;
            bestListenerDistance = dist;
            outPosition = afterBase.position;
        }
    }
    return found;
}
}  // namespace

void AudioEventRouter::RouteStep(
    const GameState& beforeUpdate,
    const GameState& afterUpdate,
    float stepSeconds,
    const AudioEventRouterConfig& config) const {
    if (!config.audioReady) {
        return;
    }

    const int playerProjectilesBefore =
        game::queries::CountProjectilesByOwner(beforeUpdate, ProjectileOwner::Player);
    const int enemyProjectilesBefore =
        game::queries::CountProjectilesByOwner(beforeUpdate, ProjectileOwner::Enemy);
    const int aliveEnemiesBefore = game::queries::CountAliveEnemies(beforeUpdate);
    const int aliveBasesBefore = game::queries::CountAliveBases(beforeUpdate);

    const int playerProjectilesAfter =
        game::queries::CountProjectilesByOwner(afterUpdate, ProjectileOwner::Player);
    const int enemyProjectilesAfter =
        game::queries::CountProjectilesByOwner(afterUpdate, ProjectileOwner::Enemy);
    const int aliveEnemiesAfter = game::queries::CountAliveEnemies(afterUpdate);
    const int aliveBasesAfter = game::queries::CountAliveBases(afterUpdate);

    const Vec2f listener = afterUpdate.world.player.position;

    if (config.playerShotLoaded && config.playerShotSound != nullptr && playerProjectilesAfter > playerProjectilesBefore) {
        Vec2f source = listener;
        if (FindClosestFreshProjectilePosition(
                afterUpdate,
                ProjectileOwner::Player,
                stepSeconds,
                listener,
                source)) {
            PlaySpatialSound(*config.playerShotSound, listener, source);
        } else {
            PlaySpatialSound(*config.playerShotSound, listener, listener);
        }
    }

    if (config.enemyShotLoaded && config.enemyShotSound != nullptr && enemyProjectilesAfter > enemyProjectilesBefore) {
        Vec2f source = listener;
        if (FindClosestFreshProjectilePosition(
                afterUpdate,
                ProjectileOwner::Enemy,
                stepSeconds,
                listener,
                source)) {
            PlaySpatialSound(*config.enemyShotSound, listener, source);
        }
    }

    if (config.enemySpawningLoaded && config.enemySpawningSound != nullptr && aliveEnemiesAfter > aliveEnemiesBefore) {
        Vec2f source{};
        if (FindClosestSpawnedEnemyPosition(beforeUpdate, afterUpdate, listener, source)) {
            PlaySpatialSound(*config.enemySpawningSound, listener, source);
        }
    }

    if (config.enemyExplodingLoaded && config.enemyExplodingSound != nullptr && aliveEnemiesAfter < aliveEnemiesBefore) {
        Vec2f source{};
        if (FindClosestRemovedEnemyPosition(beforeUpdate, afterUpdate, listener, source)) {
            PlaySpatialSound(*config.enemyExplodingSound, listener, source);
        }
    }

    if (config.baseExplodingLoaded && config.baseExplodingSound != nullptr && aliveBasesAfter < aliveBasesBefore) {
        Vec2f source{};
        if (FindClosestDestroyedBasePosition(beforeUpdate, afterUpdate, listener, source)) {
            PlaySpatialSound(*config.baseExplodingSound, listener, source);
        }
    }

    if (config.powerUpLoaded &&
        config.powerUpSound != nullptr &&
        beforeUpdate.world.startModeRemainingSeconds <= 0.0F &&
        afterUpdate.world.startModeRemainingSeconds > 0.0F) {
        PlaySpatialSound(*config.powerUpSound, listener, listener);
    }
}
