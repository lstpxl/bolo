#pragma once

#include "game/model/EntityTypes.h"

namespace game {

/// Returns true if the enemy type can appear at the given level (1-based).
inline bool EnemyTypeAppearsAtLevel(EnemyType type, int level) {
    switch (type) {
        case EnemyType::Drone:
            return level >= 1 && level <= 5;
        case EnemyType::Torpedo:
            return level >= 3 && level <= 6;
        case EnemyType::Hunter:
            return level >= 5 && level <= 8;
        case EnemyType::Assassin:
            return level >= 8;
        default:
            return false;
    }
}

}  // namespace game
