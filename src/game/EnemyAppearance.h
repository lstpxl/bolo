#pragma once

#include <array>
#include <optional>
#include <vector>
#include "game/model/EntityTypes.h"

namespace game {

/// Returns the subtype for the enemy type at the given level (1-based), or nullopt if it cannot appear.
inline std::optional<EnemySubtype> EnemyTypeAppearsAtLevel(EnemyType type, int level) {
    switch (type) {
        case EnemyType::Drone:
            return (level >= 1 && level <= 5) ? std::optional{EnemySubtype::Advanced} : std::nullopt;
        case EnemyType::Torpedo:
            return (level >= 3 && level <= 6) ? std::optional{EnemySubtype::Advanced} : std::nullopt;
        case EnemyType::Hunter:
            return (level >= 5 && level <= 8) ? std::optional{EnemySubtype::Advanced} : std::nullopt;
        case EnemyType::Assassin:
            return (level >= 8) ? std::optional{EnemySubtype::Advanced} : std::nullopt;
        default:
            return std::nullopt;
    }
}

struct EnemySpawnChoice {
    EnemyType type;
    EnemySubtype subtype;
};

/// Returns the list of enemy types (with subtypes) that can appear at the given level (1-based).
/// Empty if none apply.
inline std::vector<EnemySpawnChoice> EnemyTypesForLevel(int level) {
    std::vector<EnemySpawnChoice> result;
    constexpr std::array<EnemyType, 4> kTypes{
        EnemyType::Drone,
        EnemyType::Torpedo,
        EnemyType::Hunter,
        EnemyType::Assassin,
    };
    for (EnemyType type : kTypes) {
        if (auto subtype = EnemyTypeAppearsAtLevel(type, level)) {
            result.push_back(EnemySpawnChoice{.type = type, .subtype = *subtype});
        }
    }
    return result;
}

}  // namespace game
