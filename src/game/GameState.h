#pragma once

#include "core/Types.h"

enum class GameMode {
    Menu,
    Playing,
};

struct PlayerState {
    Vec2f position;
    Vec2f velocity;
    float headingRadians;
    bool alive;
};

struct EnemyBaseState {
    Vec2f position;
    int activeEnemies;
};

struct WorldState {
    PlayerState player;
    EnemyBaseState enemyBase;
    int score;
};

struct GameState {
    MenuSettings menuSettings{
        .difficulty = DifficultyLevel::Normal,
        .mazeDensityPercent = 45,
    };
    WorldState world{
        .player =
            PlayerState{
                .position = Vec2f{.x = 0.0F, .y = 0.0F},
                .velocity = Vec2f{.x = 0.0F, .y = 0.0F},
                .headingRadians = 0.0F,
                .alive = true,
            },
        .enemyBase =
            EnemyBaseState{
                .position = Vec2f{.x = 240.0F, .y = 140.0F},
                .activeEnemies = 0,
            },
        .score = 0,
    };
};
