#include "app/AudioEventRouter.h"

#include "core/Math.h"
#include "core/Profiling.h"

namespace {
float ComputeSpatialVolume(const Vec2f& listener, const Vec2f& source) {
    const float r1 = 3.0F * static_cast<float>(GameplayConstants::kMazeCellSizeUnits);
    const float r2 = 10.0F * static_cast<float>(GameplayConstants::kMazeCellSizeUnits);
    const float d = core::math::ApproximateEuclideanDistanceOctile(listener, source);
    if (d > r2) {
        return 0.0F;
    }
    if (d <= r1) {
        return 1.0F;
    }
    return 1.0F - (d - r1) / (r2 - r1);
}

void PlaySpatialSound(SoundPool<4>& pool, const Vec2f& listener, const Vec2f& source) {
    const float volume = ComputeSpatialVolume(listener, source);
    if (volume <= 0.0F) {
        return;
    }
    pool.Play(volume);
}
}  // namespace

void AudioEventRouter::RouteStep(
    const Vec2f& listener,
    GameplayEventQueue& events,
    const AudioEventRouterConfig& config) const {
    if (!config.audioReady) {
        events.Clear();
        return;
    }
    for (std::size_t i = 0; i < events.count; ++i) {
        const GameplayEvent& event = events.events[i];
        switch (event.type) {
        case GameplayEventType::ProjectileFired:
            if (event.projectileOwner == ProjectileOwner::Player) {
                if (config.playerShotLoaded && config.playerShotSound != nullptr) {
                    PlaySpatialSound(*config.playerShotSound, listener, event.position);
                }
            } else if (config.enemyShotLoaded && config.enemyShotSound != nullptr) {
                PlaySpatialSound(*config.enemyShotSound, listener, event.position);
            }
            break;
        case GameplayEventType::EnemyDestroyed:
            if (config.enemyExplodingLoaded && config.enemyExplodingSound != nullptr) {
                profiling::ScopedProfile removedEnemyMatchScope(profiling::Scope::AudioRouteRemovedEnemyMatch);
                PlaySpatialSound(*config.enemyExplodingSound, listener, event.position);
            }
            break;
        case GameplayEventType::EnemySpawned:
            if (config.enemySpawningLoaded && config.enemySpawningSound != nullptr) {
                PlaySpatialSound(*config.enemySpawningSound, listener, event.position);
            }
            break;
        case GameplayEventType::BaseDestroyed:
            if (config.baseExplodingLoaded && config.baseExplodingSound != nullptr) {
                PlaySpatialSound(*config.baseExplodingSound, listener, event.position);
            }
            break;
        case GameplayEventType::ProjectileHitWall:
            if (config.projectileWallHitLoaded && config.projectileWallHitSound != nullptr) {
                PlaySpatialSound(*config.projectileWallHitSound, listener, event.position);
            }
            break;
        case GameplayEventType::StartModeStarted:
            if (config.powerUpLoaded && config.powerUpSound != nullptr) {
                PlaySpatialSound(*config.powerUpSound, listener, event.position);
            }
            break;
        case GameplayEventType::PlayerExplosion:
            if (config.playerExplosionLoaded && config.playerExplosionSound != nullptr) {
                PlaySpatialSound(*config.playerExplosionSound, listener, event.position);
            }
            break;
        }
    }
    events.Clear();
}
