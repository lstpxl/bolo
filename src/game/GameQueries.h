#pragma once

#include <cstddef>
#include "game/GameState.h"

namespace game::queries {
inline int CountProjectilesByOwner(const GameState& state, ProjectileOwner owner) {
    int count = 0;
    for (const Projectile& projectile : state.world.projectiles) {
        if (projectile.alive && projectile.owner == owner) {
            ++count;
        }
    }
    return count;
}

inline int CountAliveEnemies(const GameState& state) {
    int count = 0;
    for (const EnemyTank& enemy : state.world.enemies) {
        if (enemy.alive) {
            ++count;
        }
    }
    return count;
}

inline int CountAliveBases(const GameState& state) {
    int count = 0;
    for (const EnemyBase& base : state.world.enemyBases) {
        if (!base.destroyed) {
            ++count;
        }
    }
    return count;
}

inline int CountAliveEnemiesByType(const GameState& state, EnemyType type) {
    int count = 0;
    for (const EnemyTank& enemy : state.world.enemies) {
        if (enemy.alive && enemy.type == type) {
            ++count;
        }
    }
    return count;
}
}  // namespace game::queries
