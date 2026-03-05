#pragma once

#include <array>
#include <cstdint>
#include "app/AppConfig.h"
#include "game/GameState.h"
#include "platform/Input.h"

class HudPanel {
public:
    void Render(const GameState& state, const AppConfig& config, const FrameInput& input) const;

private:
    static constexpr double kEnemySnapshotIntervalSeconds = 0.5;
    static constexpr double kFuelSnapshotIntervalSeconds = 0.5;
    static constexpr double kBaseRadarSnapshotIntervalSeconds = 1.0;
    static constexpr std::uint64_t kJoystickSnapshotIntervalFrames = 4;

    mutable bool cacheInitialized_ = false;
    mutable double lastEnemySnapshotSeconds_ = 0.0;
    mutable double lastFuelSnapshotSeconds_ = 0.0;
    mutable double lastBaseRadarSnapshotSeconds_ = 0.0;
    mutable std::uint64_t lastJoystickSnapshotFrame_ = 0;

    mutable float cachedFuel_ = GameplayConstants::kFuelMax;
    mutable int cachedEnemyCount_ = 0;
    mutable std::array<Vec2f, GameplayConstants::kMaxAliveEnemies> cachedEnemyPositions_{};
    mutable std::array<EnemyType, GameplayConstants::kMaxAliveEnemies> cachedEnemyTypes_{};
    mutable int cachedHighlightedQuadrant_ = -1;

    mutable float cachedLeftJoystickDirX_ = 0.0F;
    mutable float cachedLeftJoystickDirY_ = 0.0F;
    mutable float cachedLeftJoystickAmplitude_ = 0.0F;
    mutable float cachedRightJoystickDirX_ = 0.0F;
    mutable float cachedRightJoystickDirY_ = 0.0F;
    mutable float cachedRightJoystickAmplitude_ = 0.0F;
};
