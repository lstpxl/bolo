#pragma once

#include <vector>
#include "core/Types.h"

enum class GameMode {
    Menu,
    Playing,
};

struct GameplayConstants {
    static constexpr int kPixelsPerUnit = 16;
    static constexpr int kMazeWidthCells = 60;
    static constexpr int kMazeHeightCells = 60;
    static constexpr int kMazeCellSizeUnits = 6;
    static constexpr float kWallThicknessUnits = 0.125F;  // 2px at 16px/unit
    static constexpr float kEntitySizeUnits = 1.0F;
    static constexpr float kEnemyBaseSizeUnits = 3.0F;
    static constexpr int kEnemyBaseCount = 6;
};

struct MazeCell {
    bool northWall = true;
    bool eastWall = true;
    bool southWall = true;
    bool westWall = true;
};

struct MazeState {
    int widthCells = GameplayConstants::kMazeWidthCells;
    int heightCells = GameplayConstants::kMazeHeightCells;
    int cellSizeUnits = GameplayConstants::kMazeCellSizeUnits;
    std::vector<MazeCell> cells{};
};

struct PlayerTank {
    Vec2f position;
    Vec2f velocity;
    float hullHeadingRadians;
    float turretHeadingRadians;
    float throttleNormalized = 0.0F;
    bool alive = true;
};

struct EnemyTank {
    Vec2f position;
    float headingRadians;
    bool alive = true;
};

struct EnemyBase {
    Vec2f position;
    bool destroyed = false;
    int activeEnemies = 0;
};

struct WorldState {
    MazeState maze{};
    PlayerTank player{
        .position = Vec2f{.x = 0.0F, .y = 0.0F},
        .velocity = Vec2f{.x = 0.0F, .y = 0.0F},
        .hullHeadingRadians = 0.0F,
        .turretHeadingRadians = 0.0F,
        .throttleNormalized = 0.0F,
        .alive = true,
    };
    std::vector<EnemyTank> enemies{};
    std::vector<EnemyBase> enemyBases{};
    int score = 0;
};

struct GameState {
    MenuSettings menuSettings{
        .levelNumber = 1,
        .mazeDensity = 3,
    };
    WorldState world{};
};
