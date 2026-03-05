#include "ui/HudPanel.h"

#include <algorithm>
#include <cmath>
#include "core/Profiling.h"
#include "platform/PlayerFigure.h"
#include "raylib.h"

namespace {
constexpr Color kDroneMapColor{138, 43, 226, 255};      // #8A2BE2
constexpr Color kTorpedoMapColor{255, 255, 0, 255};     // #FFFF00
constexpr Color kHunterMapColor{255, 165, 0, 255};      // #FFA500
constexpr Color kAssassinMapColor{255, 0, 0, 255};      // #FF0000
constexpr Color kPlayerMapColor{0, 255, 255, 255};      // #00FFFF
constexpr Color kBaseMapColor{255, 0, 255, 255};        // #FF00FF
constexpr Color kDestroyedBaseMapColor{96, 96, 96, 255};    // #606060

Color EnemyMapColor(EnemyType type) {
    if (type == EnemyType::Drone) {
        return kDroneMapColor;
    }
    if (type == EnemyType::Torpedo) {
        return kTorpedoMapColor;
    }
    if (type == EnemyType::Hunter) {
        return kHunterMapColor;
    }
    return kAssassinMapColor;
}
}  // namespace

void HudPanel::Render(const GameState& state, const AppConfig& config, const FrameInput& input) const {
    const double nowSeconds = GetTime();
    const std::uint64_t frameIndex = profiling::Profiler::Instance().FrameIndex();
    const bool needEnemySnapshot =
        !cacheInitialized_ || (nowSeconds - lastEnemySnapshotSeconds_) >= kEnemySnapshotIntervalSeconds;
    if (needEnemySnapshot) {
        cachedEnemyCount_ = 0;
        for (const EnemyTank& enemy : state.world.enemies) {
            if (!enemy.alive || cachedEnemyCount_ >= GameplayConstants::kMaxAliveEnemies) {
                continue;
            }
            cachedEnemyPositions_[static_cast<std::size_t>(cachedEnemyCount_)] = enemy.position;
            cachedEnemyTypes_[static_cast<std::size_t>(cachedEnemyCount_)] = enemy.type;
            cachedEnemyCount_ += 1;
        }
        lastEnemySnapshotSeconds_ = nowSeconds;
    }

    const bool needFuelSnapshot =
        !cacheInitialized_ || (nowSeconds - lastFuelSnapshotSeconds_) >= kFuelSnapshotIntervalSeconds;
    if (needFuelSnapshot) {
        cachedFuel_ = state.world.player.fuel;
        lastFuelSnapshotSeconds_ = nowSeconds;
    }

    const bool needBaseRadarSnapshot =
        !cacheInitialized_ || (nowSeconds - lastBaseRadarSnapshotSeconds_) >= kBaseRadarSnapshotIntervalSeconds;
    if (needBaseRadarSnapshot) {
        cachedHighlightedQuadrant_ = -1;
        float nearestDistanceSq = 0.0F;
        for (const EnemyBase& base : state.world.enemyBases) {
            if (base.destroyed) {
                continue;
            }
            const float dx = base.position.x - state.world.player.position.x;
            const float dy = base.position.y - state.world.player.position.y;
            const float distanceSq = dx * dx + dy * dy;
            if (cachedHighlightedQuadrant_ == -1 || distanceSq < nearestDistanceSq) {
                nearestDistanceSq = distanceSq;
                const bool right = dx >= 0.0F;
                const bool down = dy >= 0.0F;
                if (!right && !down) {
                    cachedHighlightedQuadrant_ = 0;  // top-left
                } else if (right && !down) {
                    cachedHighlightedQuadrant_ = 1;  // top-right
                } else if (!right && down) {
                    cachedHighlightedQuadrant_ = 2;  // bottom-left
                } else {
                    cachedHighlightedQuadrant_ = 3;  // bottom-right
                }
            }
        }
        lastBaseRadarSnapshotSeconds_ = nowSeconds;
    }

    const bool needJoystickSnapshot =
        !cacheInitialized_ || frameIndex >= (lastJoystickSnapshotFrame_ + kJoystickSnapshotIntervalFrames);
    if (needJoystickSnapshot) {
        constexpr float joystickAxisRawMax = 32768.0F;
        const float leftRawAxisX = static_cast<float>(input.gamepadAxis0Raw);
        const float leftRawAxisY = static_cast<float>(input.gamepadAxis1Raw);
        const float leftRawMagnitude = std::sqrt(leftRawAxisX * leftRawAxisX + leftRawAxisY * leftRawAxisY);
        cachedLeftJoystickAmplitude_ = std::min(1.0F, leftRawMagnitude / joystickAxisRawMax);
        cachedLeftJoystickDirX_ = 0.0F;
        cachedLeftJoystickDirY_ = 0.0F;
        if (leftRawMagnitude > 0.0F) {
            cachedLeftJoystickDirX_ = leftRawAxisX / leftRawMagnitude;
            cachedLeftJoystickDirY_ = leftRawAxisY / leftRawMagnitude;
        }

        const float rightRawAxisX = static_cast<float>(input.gamepadAxis2Raw);
        const float rightRawAxisY = static_cast<float>(input.gamepadAxis3Raw);
        const float rightRawMagnitude = std::sqrt(rightRawAxisX * rightRawAxisX + rightRawAxisY * rightRawAxisY);
        cachedRightJoystickAmplitude_ = std::min(1.0F, rightRawMagnitude / joystickAxisRawMax);
        cachedRightJoystickDirX_ = 0.0F;
        cachedRightJoystickDirY_ = 0.0F;
        if (rightRawMagnitude > 0.0F) {
            cachedRightJoystickDirX_ = rightRawAxisX / rightRawMagnitude;
            cachedRightJoystickDirY_ = rightRawAxisY / rightRawMagnitude;
        }
        lastJoystickSnapshotFrame_ = frameIndex;
    }

    cacheInitialized_ = true;

    const int hudWidth = ComputeHudWidth(config);
    const Rectangle panel = {
        .x = static_cast<float>(config.screenWidth - hudWidth),
        .y = 0.0F,
        .width = static_cast<float>(hudWidth),
        .height = static_cast<float>(config.screenHeight),
    };

    DrawRectangleRec(panel, Color{27, 31, 39, 255});
    DrawLine(
        static_cast<int>(panel.x),
        0,
        static_cast<int>(panel.x),
        config.screenHeight,
        Color{58, 66, 80, 255});

    const int panelX = static_cast<int>(panel.x);
    const int contentPadding = 10;
    const int contentX = panelX + contentPadding;
    const int contentWidth = hudWidth - (contentPadding * 2);
    const int mapBottomPadding = 10;
    int cursorY = 8;

    // 1) BOLT text
    constexpr float boltFontSize = 50.0F;  // 1.25x over previous 40
    const int boltBaseWidth = MeasureText("BOLT", static_cast<int>(boltFontSize));
    const float boltSpacing = std::max(
        1.0F,
        (static_cast<float>(contentWidth) - static_cast<float>(boltBaseWidth)) / 3.0F);
    DrawTextEx(
        GetFontDefault(),
        "BOLT",
        Vector2{static_cast<float>(contentX), static_cast<float>(cursorY)},
        boltFontSize,
        boltSpacing,
        PURPLE);
    cursorY += 56;

    // 2) SCORE 0000 display
    DrawRectangle(contentX, cursorY, contentWidth, 36, BLACK);
    DrawRectangleLines(contentX, cursorY, contentWidth, 36, RAYWHITE);
    DrawText(TextFormat("SCORE %04d", state.world.score), contentX + 8, cursorY + 9, 20, RAYWHITE);
    cursorY += 44;

    // 3) lives icons
    const int livesY = cursorY;
    const int iconGap = 6;
    const int iconSize = (contentWidth - (iconGap * 3)) / 4;
    const int livesToRender = std::max(0, std::min(4, state.world.player.lives));
    for (int i = 0; i < livesToRender; ++i) {
        const int iconX = contentX + (i * (iconSize + iconGap));
        DrawPlayerFigure(
            Vector2{
                static_cast<float>(iconX) + static_cast<float>(iconSize) * 0.5F,
                static_cast<float>(livesY) + static_cast<float>(iconSize) * 0.5F,
            },
            static_cast<float>(iconSize),
            0.0F,
            RAYWHITE);
    }
    cursorY += iconSize + 12;

    // 4) Fuel bar
    const float fuelClamped = std::max(0.0F, std::min(100.0F, cachedFuel_));
    const int fuelWidth = static_cast<int>((fuelClamped / 100.0F) * static_cast<float>(contentWidth));
    DrawRectangle(contentX, cursorY, contentWidth, 12, DARKGRAY);
    DrawRectangle(contentX, cursorY, fuelWidth, 12, ORANGE);
    DrawRectangleLines(contentX, cursorY, contentWidth, 12, RAYWHITE);
    cursorY += 20;

    // 5) Velocity bar
    const float speed = std::sqrt(
        state.world.player.velocity.x * state.world.player.velocity.x +
        state.world.player.velocity.y * state.world.player.velocity.y);
    const float speedNormalized =
        std::max(0.0F, std::min(1.0F, speed / GameplayConstants::kPlayerFullVelocity));
    const int speedWidth = static_cast<int>(speedNormalized * static_cast<float>(contentWidth));
    DrawRectangle(contentX, cursorY, contentWidth, 12, DARKGRAY);
    DrawRectangle(contentX, cursorY, speedWidth, 12, SKYBLUE);
    DrawRectangleLines(contentX, cursorY, contentWidth, 12, RAYWHITE);
    cursorY += 20;

    // 6) Live square map (player dot) aligned to HUD bottom
    const int mapSize = contentWidth;
    const int mapY = static_cast<int>(panel.y + panel.height) - mapBottomPadding - mapSize;
    DrawRectangle(contentX, mapY, mapSize, mapSize, BLACK);
    DrawRectangleLines(contentX, mapY, mapSize, mapSize, RAYWHITE);
    const float mazeWidth = static_cast<float>(state.world.maze.widthCells * state.world.maze.cellSizeUnits);
    const float mazeHeight = static_cast<float>(state.world.maze.heightCells * state.world.maze.cellSizeUnits);
    const auto mapPixelX = [&](float worldX) {
        const float normalized = mazeWidth > 0.0F ? worldX / mazeWidth : 0.5F;
        return contentX + static_cast<int>(
            std::max(0.0F, std::min(1.0F, normalized)) * static_cast<float>(mapSize - 1));
    };
    const auto mapPixelY = [&](float worldY) {
        const float normalized = mazeHeight > 0.0F ? worldY / mazeHeight : 0.5F;
        return mapY + static_cast<int>(
            std::max(0.0F, std::min(1.0F, normalized)) * static_cast<float>(mapSize - 1));
    };

    for (const EnemyBase& base : state.world.enemyBases) {
        const int px = mapPixelX(base.position.x);
        const int py = mapPixelY(base.position.y);
        DrawRectangle(px - 1, py - 1, 3, 3, base.destroyed ? kDestroyedBaseMapColor : kBaseMapColor);
    }
    for (int i = 0; i < cachedEnemyCount_; ++i) {
        const Vec2f& position = cachedEnemyPositions_[static_cast<std::size_t>(i)];
        const EnemyType type = cachedEnemyTypes_[static_cast<std::size_t>(i)];
        const int px = mapPixelX(position.x);
        const int py = mapPixelY(position.y);
        DrawPixel(px, py, EnemyMapColor(type));
    }

    const float normalizedX = mazeWidth > 0.0F ? state.world.player.position.x / mazeWidth : 0.5F;
    const float normalizedY = mazeHeight > 0.0F ? state.world.player.position.y / mazeHeight : 0.5F;
    const int dotX = contentX + static_cast<int>(
        std::max(0.0F, std::min(1.0F, normalizedX)) * static_cast<float>(mapSize - 1));
    const int dotY = mapY + static_cast<int>(
        std::max(0.0F, std::min(1.0F, normalizedY)) * static_cast<float>(mapSize - 1));
    DrawCircle(dotX, dotY, 2.0F, kPlayerMapColor);

    // 7) Base direction quadrants + player heading indicator (moved near map)
    const int blockGap = 8;
    const int leftBlockSize = (contentWidth - blockGap) / 2;
    const int blocksY = mapY - leftBlockSize - 10;
    DrawRectangle(contentX, blocksY, leftBlockSize, leftBlockSize, BLACK);
    DrawRectangleLines(contentX, blocksY, leftBlockSize, leftBlockSize, RAYWHITE);
    const int quadrantCell = leftBlockSize / 2;
    const Color dimQuadrant = Color{18, 60, 26, 255};
    const Color brightQuadrant = Color{160, 255, 120, 255};
    DrawRectangle(
        contentX + 2,
        blocksY + 2,
        quadrantCell - 3,
        quadrantCell - 3,
        cachedHighlightedQuadrant_ == 0 ? brightQuadrant : dimQuadrant);
    DrawRectangle(
        contentX + quadrantCell + 1,
        blocksY + 2,
        quadrantCell - 3,
        quadrantCell - 3,
        cachedHighlightedQuadrant_ == 1 ? brightQuadrant : dimQuadrant);
    DrawRectangle(
        contentX + 2,
        blocksY + quadrantCell + 1,
        quadrantCell - 3,
        quadrantCell - 3,
        cachedHighlightedQuadrant_ == 2 ? brightQuadrant : dimQuadrant);
    DrawRectangle(
        contentX + quadrantCell + 1,
        blocksY + quadrantCell + 1,
        quadrantCell - 3,
        quadrantCell - 3,
        cachedHighlightedQuadrant_ == 3 ? brightQuadrant : dimQuadrant);

    const int compassX = contentX + leftBlockSize + blockGap;
    DrawRectangle(compassX, blocksY, leftBlockSize, leftBlockSize, Color{237, 126, 188, 255});
    DrawRectangleLines(compassX, blocksY, leftBlockSize, leftBlockSize, RAYWHITE);
    const int compassPadding = 4;
    DrawRectangle(
        compassX + compassPadding,
        blocksY + compassPadding,
        leftBlockSize - compassPadding * 2,
        leftBlockSize - compassPadding * 2,
        Color{237, 126, 188, 255});
    DrawCircle(
        compassX + (leftBlockSize / 2),
        blocksY + (leftBlockSize / 2),
        (static_cast<float>(leftBlockSize) / 3.0F) * 1.15F,
        BLACK);
    const float headingX = std::sin(state.world.player.hullHeadingRadians);
    const float headingY = -std::cos(state.world.player.hullHeadingRadians);
    const int centerX = compassX + leftBlockSize / 2;
    const int centerY = blocksY + leftBlockSize / 2;
    const float armLength = static_cast<float>(leftBlockSize) * 0.28F;
    DrawLine(
        centerX,
        centerY,
        static_cast<int>(static_cast<float>(centerX) + headingX * armLength),
        static_cast<int>(static_cast<float>(centerY) + headingY * armLength),
        RAYWHITE);
    DrawLine(
        centerX,
        centerY,
        static_cast<int>(static_cast<float>(centerX) + cachedLeftJoystickDirX_ * armLength * cachedLeftJoystickAmplitude_),
        static_cast<int>(static_cast<float>(centerY) + cachedLeftJoystickDirY_ * armLength * cachedLeftJoystickAmplitude_),
        SKYBLUE);
    DrawLine(
        centerX,
        centerY,
        static_cast<int>(static_cast<float>(centerX) + cachedRightJoystickDirX_ * armLength * cachedRightJoystickAmplitude_),
        static_cast<int>(static_cast<float>(centerY) + cachedRightJoystickDirY_ * armLength * cachedRightJoystickAmplitude_),
        RED);

    if (state.world.levelCleared || state.world.levelClearMessageSeconds > 0.0F) {
        DrawText("LEVEL CLEARED", contentX + 6, blocksY - 26, 20, LIME);
    }
}
