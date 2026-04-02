#include "app/AudioEventRouter.h"

#include <algorithm>
#include <cstdint>

#include "core/Log.h"
#include "core/Math.h"
#include "core/Profiling.h"

namespace {
struct AudioRouteWindowStats {
    std::uint64_t routeSteps = 0;
    std::uint64_t eventsTotal = 0;
    std::uint64_t soundsPlayedTotal = 0;
    std::uint64_t projectileFiredPlayerEvents = 0;
    std::uint64_t projectileFiredEnemyEvents = 0;
    std::uint64_t enemyDestroyedEvents = 0;
    std::uint64_t enemySpawnedEvents = 0;
    std::uint64_t baseDestroyedEvents = 0;
    std::uint64_t projectileHitWallEvents = 0;
    std::uint64_t startModeStartedEvents = 0;
    std::uint64_t playerExplosionEvents = 0;
    std::uint64_t playerShotSounds = 0;
    std::uint64_t enemyShotSounds = 0;
    std::uint64_t enemyDestroyedSounds = 0;
    std::uint64_t enemySpawnedSounds = 0;
    std::uint64_t baseDestroyedSounds = 0;
    std::uint64_t projectileHitWallSounds = 0;
    std::uint64_t startModeStartedSounds = 0;
    std::uint64_t playerExplosionSounds = 0;
    std::uint64_t maxEventsPerStep = 0;
    std::uint64_t maxSoundsPerStep = 0;
};

AudioRouteWindowStats gAudioRouteWindowStats{};
std::uint64_t gLastAudioRouteWindowPrintedFrame = 0;

float ComputeSpatialVolume(const Vec2f& listener, const Vec2f& source) {
    const float r1 = 3.0F * static_cast<float>(GameplayConstants::kMazeCellSizeUnits);
    const float r2 = 10.0F * static_cast<float>(GameplayConstants::kMazeCellSizeUnits);
    // Octile approximation overestimates diagonal distances by up to ~4%.
    // Intentional: the error is negligible at gameplay scale and avoids a sqrt per sound event.
    const float d = core::math::ApproximateEuclideanDistanceOctile(listener, source);
    if (d > r2) {
        return 0.0F;
    }
    if (d <= r1) {
        return 1.0F;
    }
    return 1.0F - (d - r1) / (r2 - r1);
}

bool PlaySpatialSound(SoundPool<4>& pool, const Vec2f& listener, const Vec2f& source) {
    const float volume = ComputeSpatialVolume(listener, source);
    if (volume <= 0.0F) {
        return false;
    }
    pool.Play(volume);
    return true;
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

    std::uint64_t soundsPlayedThisStep = 0;
    gAudioRouteWindowStats.routeSteps += 1;
    gAudioRouteWindowStats.eventsTotal += events.count;
    gAudioRouteWindowStats.maxEventsPerStep =
        std::max<std::uint64_t>(gAudioRouteWindowStats.maxEventsPerStep, events.count);

    for (std::size_t i = 0; i < events.count; ++i) {
        const GameplayEvent& event = events.events[i];
        switch (event.type) {
        case GameplayEventType::ProjectileFired:
            if (event.projectileOwner == ProjectileOwner::Player) {
                gAudioRouteWindowStats.projectileFiredPlayerEvents += 1;
                if (config.playerShotLoaded && config.playerShotSound != nullptr) {
                    if (PlaySpatialSound(*config.playerShotSound, listener, event.position)) {
                        gAudioRouteWindowStats.playerShotSounds += 1;
                        gAudioRouteWindowStats.soundsPlayedTotal += 1;
                        soundsPlayedThisStep += 1;
                    }
                }
            } else {
                gAudioRouteWindowStats.projectileFiredEnemyEvents += 1;
                if (config.enemyShotLoaded && config.enemyShotSound != nullptr) {
                    if (PlaySpatialSound(*config.enemyShotSound, listener, event.position)) {
                        gAudioRouteWindowStats.enemyShotSounds += 1;
                        gAudioRouteWindowStats.soundsPlayedTotal += 1;
                        soundsPlayedThisStep += 1;
                    }
                }
            }
            break;
        case GameplayEventType::EnemyDestroyed:
            gAudioRouteWindowStats.enemyDestroyedEvents += 1;
            if (config.enemyExplodingLoaded && config.enemyExplodingSound != nullptr) {
                profiling::ScopedProfile removedEnemyMatchScope(profiling::Scope::AudioRouteRemovedEnemyMatch);
                if (PlaySpatialSound(*config.enemyExplodingSound, listener, event.position)) {
                    gAudioRouteWindowStats.enemyDestroyedSounds += 1;
                    gAudioRouteWindowStats.soundsPlayedTotal += 1;
                    soundsPlayedThisStep += 1;
                }
            }
            break;
        case GameplayEventType::EnemySpawned:
            gAudioRouteWindowStats.enemySpawnedEvents += 1;
            if (config.enemySpawningLoaded && config.enemySpawningSound != nullptr) {
                if (PlaySpatialSound(*config.enemySpawningSound, listener, event.position)) {
                    gAudioRouteWindowStats.enemySpawnedSounds += 1;
                    gAudioRouteWindowStats.soundsPlayedTotal += 1;
                    soundsPlayedThisStep += 1;
                }
            }
            break;
        case GameplayEventType::BaseDestroyed:
            gAudioRouteWindowStats.baseDestroyedEvents += 1;
            if (config.baseExplodingLoaded && config.baseExplodingSound != nullptr) {
                if (PlaySpatialSound(*config.baseExplodingSound, listener, event.position)) {
                    gAudioRouteWindowStats.baseDestroyedSounds += 1;
                    gAudioRouteWindowStats.soundsPlayedTotal += 1;
                    soundsPlayedThisStep += 1;
                }
            }
            break;
        case GameplayEventType::ProjectileHitWall:
            gAudioRouteWindowStats.projectileHitWallEvents += 1;
            if (config.projectileWallHitLoaded && config.projectileWallHitSound != nullptr) {
                if (PlaySpatialSound(*config.projectileWallHitSound, listener, event.position)) {
                    gAudioRouteWindowStats.projectileHitWallSounds += 1;
                    gAudioRouteWindowStats.soundsPlayedTotal += 1;
                    soundsPlayedThisStep += 1;
                }
            }
            break;
        case GameplayEventType::StartModeStarted:
            gAudioRouteWindowStats.startModeStartedEvents += 1;
            if (config.powerUpLoaded && config.powerUpSound != nullptr) {
                if (PlaySpatialSound(*config.powerUpSound, listener, event.position)) {
                    gAudioRouteWindowStats.startModeStartedSounds += 1;
                    gAudioRouteWindowStats.soundsPlayedTotal += 1;
                    soundsPlayedThisStep += 1;
                }
            }
            break;
        case GameplayEventType::PlayerExplosion:
            gAudioRouteWindowStats.playerExplosionEvents += 1;
            if (config.playerExplosionLoaded && config.playerExplosionSound != nullptr) {
                if (PlaySpatialSound(*config.playerExplosionSound, listener, event.position)) {
                    gAudioRouteWindowStats.playerExplosionSounds += 1;
                    gAudioRouteWindowStats.soundsPlayedTotal += 1;
                    soundsPlayedThisStep += 1;
                }
            }
            break;
        }
    }
    gAudioRouteWindowStats.maxSoundsPerStep =
        std::max<std::uint64_t>(gAudioRouteWindowStats.maxSoundsPerStep, soundsPlayedThisStep);

    const auto& profiler = profiling::Profiler::Instance();
    const std::uint64_t frameIndex = profiler.FrameIndex();
    if (profiler.ShouldEmitPeriodicReport() && frameIndex != gLastAudioRouteWindowPrintedFrame) {
        gLastAudioRouteWindowPrintedFrame = frameIndex;
        const float avgEventsPerStep = gAudioRouteWindowStats.routeSteps > 0
            ? static_cast<float>(gAudioRouteWindowStats.eventsTotal) /
                static_cast<float>(gAudioRouteWindowStats.routeSteps)
            : 0.0F;
        const float avgSoundsPerStep = gAudioRouteWindowStats.routeSteps > 0
            ? static_cast<float>(gAudioRouteWindowStats.soundsPlayedTotal) /
                static_cast<float>(gAudioRouteWindowStats.routeSteps)
            : 0.0F;
        bolt::log::Profile(
            "[AUDIO_ROUTE_WINDOW] steps=%llu events=%llu sounds=%llu avg(step ev=%.2f snd=%.2f) max(step ev=%llu snd=%llu)\n"
            "  events{shotP=%llu shotE=%llu dead=%llu spawn=%llu base=%llu wall=%llu start=%llu playerExpl=%llu}\n"
            "  sounds{shotP=%llu shotE=%llu dead=%llu spawn=%llu base=%llu wall=%llu start=%llu playerExpl=%llu}\n",
            static_cast<unsigned long long>(gAudioRouteWindowStats.routeSteps),
            static_cast<unsigned long long>(gAudioRouteWindowStats.eventsTotal),
            static_cast<unsigned long long>(gAudioRouteWindowStats.soundsPlayedTotal),
            avgEventsPerStep,
            avgSoundsPerStep,
            static_cast<unsigned long long>(gAudioRouteWindowStats.maxEventsPerStep),
            static_cast<unsigned long long>(gAudioRouteWindowStats.maxSoundsPerStep),
            static_cast<unsigned long long>(gAudioRouteWindowStats.projectileFiredPlayerEvents),
            static_cast<unsigned long long>(gAudioRouteWindowStats.projectileFiredEnemyEvents),
            static_cast<unsigned long long>(gAudioRouteWindowStats.enemyDestroyedEvents),
            static_cast<unsigned long long>(gAudioRouteWindowStats.enemySpawnedEvents),
            static_cast<unsigned long long>(gAudioRouteWindowStats.baseDestroyedEvents),
            static_cast<unsigned long long>(gAudioRouteWindowStats.projectileHitWallEvents),
            static_cast<unsigned long long>(gAudioRouteWindowStats.startModeStartedEvents),
            static_cast<unsigned long long>(gAudioRouteWindowStats.playerExplosionEvents),
            static_cast<unsigned long long>(gAudioRouteWindowStats.playerShotSounds),
            static_cast<unsigned long long>(gAudioRouteWindowStats.enemyShotSounds),
            static_cast<unsigned long long>(gAudioRouteWindowStats.enemyDestroyedSounds),
            static_cast<unsigned long long>(gAudioRouteWindowStats.enemySpawnedSounds),
            static_cast<unsigned long long>(gAudioRouteWindowStats.baseDestroyedSounds),
            static_cast<unsigned long long>(gAudioRouteWindowStats.projectileHitWallSounds),
            static_cast<unsigned long long>(gAudioRouteWindowStats.startModeStartedSounds),
            static_cast<unsigned long long>(gAudioRouteWindowStats.playerExplosionSounds));
        gAudioRouteWindowStats = AudioRouteWindowStats{};
    }
    events.Clear();
}
