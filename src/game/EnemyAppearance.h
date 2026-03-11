#pragma once

#include <vector>
#include "game/model/EntityTypes.h"

namespace game {

struct EnemySpawnChoice {
    EnemyType type;
    EnemySubtype subtype;
};

/// Returns the list of enemy types (with subtypes) that can appear at the given level (1-based).
inline std::vector<EnemySpawnChoice> EnemyTypesForLevel(int level) {
    switch (level) {
        case 1:
            return {{EnemySpawnChoice{.type = EnemyType::Drone, .subtype = EnemySubtype::Basic}}};
        case 2:
            return {{EnemySpawnChoice{.type = EnemyType::Drone, .subtype = EnemySubtype::Advanced}}};
        case 3:
            return {
                {EnemySpawnChoice{.type = EnemyType::Drone, .subtype = EnemySubtype::Advanced}},
                {EnemySpawnChoice{.type = EnemyType::Torpedo, .subtype = EnemySubtype::Basic}},
            };
        case 4:
            return {
                {EnemySpawnChoice{.type = EnemyType::Drone, .subtype = EnemySubtype::Advanced}},
                {EnemySpawnChoice{.type = EnemyType::Torpedo, .subtype = EnemySubtype::Advanced}},
            };
        case 5:
            return {
                {EnemySpawnChoice{.type = EnemyType::Drone, .subtype = EnemySubtype::Advanced}},
                {EnemySpawnChoice{.type = EnemyType::Torpedo, .subtype = EnemySubtype::Advanced}},
                {EnemySpawnChoice{.type = EnemyType::Hunter, .subtype = EnemySubtype::Basic}},
            };
        case 6:
            return {
                {EnemySpawnChoice{.type = EnemyType::Drone, .subtype = EnemySubtype::Advanced}},
                {EnemySpawnChoice{.type = EnemyType::Torpedo, .subtype = EnemySubtype::Advanced}},
                {EnemySpawnChoice{.type = EnemyType::Hunter, .subtype = EnemySubtype::Advanced}},
            };
        case 7:
            return {{EnemySpawnChoice{.type = EnemyType::Torpedo, .subtype = EnemySubtype::Advanced}},
            {EnemySpawnChoice{.type = EnemyType::Hunter, .subtype = EnemySubtype::Lord}}};
        case 8:
            return {
                {EnemySpawnChoice{.type = EnemyType::Hunter, .subtype = EnemySubtype::Advanced}},
                {EnemySpawnChoice{.type = EnemyType::Assassin, .subtype = EnemySubtype::Basic}},
            };
        case 9:
            return {{EnemySpawnChoice{.type = EnemyType::Assassin, .subtype = EnemySubtype::Advanced}}};
        default:
            return {{EnemySpawnChoice{.type = EnemyType::Drone, .subtype = EnemySubtype::Advanced}}};
    }
}

/// Returns true if the level can spawn Assassin or Hunter (flow-field consumers).
inline bool LevelHasFlowConsumers(int level) {
    const std::vector<EnemySpawnChoice> types = EnemyTypesForLevel(level);
    for (const EnemySpawnChoice& c : types) {
        if (c.type == EnemyType::Assassin || c.type == EnemyType::Hunter) {
            return true;
        }
    }
    return false;
}

}  // namespace game
