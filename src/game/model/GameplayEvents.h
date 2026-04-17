#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "core/Types.h"
#include "game/model/EntityTypes.h"

enum class GameplayEventType : std::uint8_t {
    ProjectileFired = 0,
    EnemyDestroyed,
    EnemySpawned,
    BaseDestroyed,
    ProjectileHitWall,
    StartModeStarted,
    PlayerExplosion,
    EvacZoneCompleted,
};

enum class StartModeReason : std::uint8_t {
    Unknown = 0,
    NewGame = 1,
    Respawn = 2,
    LevelComplete = 3,
};

struct GameplayEvent {
    GameplayEventType type = GameplayEventType::ProjectileFired;
    Vec2f position{.x = 0.0F, .y = 0.0F};
    ProjectileOwner projectileOwner = ProjectileOwner::Enemy;
    EnemyType enemyType = EnemyType::Drone;
    EnemySubtype enemySubtype = EnemySubtype::Basic;
    int baseIndex = -1;
    StartModeReason startModeReason = StartModeReason::Unknown;
};

struct GameplayEventQueue {
    static constexpr std::size_t kCapacity = 256;

    std::array<GameplayEvent, kCapacity> events{};
    std::size_t count = 0;

    bool Push(const GameplayEvent& event) {
        if (count >= events.size()) {
            return false;
        }
        events[count] = event;
        ++count;
        return true;
    }

    void Clear() {
        count = 0;
    }
};
