#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include "core/Random.h"
#include "core/Types.h"
#include "game/model/EntityTypes.h"
#include "game/model/WorldState.h"
#include "game/navigation/CellCoordCache.h"
#include "game/spatial/SweepPruneBroadPhase.h"
#include "game/systems/EnemySystem.h"

class MenuBackgroundSimulation {
public:
    void Initialize();
    void Reset();
    void Update(float deltaSeconds);

    bool IsInitialized() const;
    const MazeState& Maze() const;
    const std::vector<EnemyTank>& Enemies() const;
    Vec2f CameraTarget() const;

private:
    struct EnemyRuntimeState {
        int destinationCellHash = -1;
        std::vector<int> pathCellHashes{};
        std::size_t pathCellIndex = 0;
        int segmentTargetCellHash = -1;
        std::array<Vec2f, 2> segmentPoints{};
        int segmentCount = 0;
        int segmentIndex = 0;
    };
    struct CameraRuntimeState {
        int destinationCellHash = -1;
        std::vector<int> pathCellHashes{};
        std::size_t pathCellIndex = 0;
    };

    static constexpr int kMazeWidthCells = 30;
    static constexpr int kMazeHeightCells = 30;
    static constexpr int kMazeDensity = 3;
    static constexpr int kTargetEnemyCount = 30;

    void GenerateMaze();
    void SpawnInitialEnemies();
    void EnsureEnemyCount();
    void UpdateEnemy(std::size_t selfIndex, float deltaSeconds);
    [[nodiscard]] Vec2f ComputeMenuSteeringBias(std::size_t selfIndex, const Vec2f& pathForwardUnit) const;
    [[nodiscard]] int CountEnemiesNearProbe(
        std::size_t selfIndex,
        const Vec2f& probeCenter,
        float radiusSq) const;
    [[nodiscard]] float WallFreeAheadFrom(const Vec2f& from, float headingRadians) const;
    void PickNewDestinationAndPath(const EnemyTank& enemy, EnemyRuntimeState& runtime);
    bool BuildStepSegment(
        const EnemyTank& enemy,
        EnemyRuntimeState& runtime,
        int fromCellHash,
        int targetCellHash);
    bool BuildPathAStar(int startHash, int goalHash, std::vector<int>& outPath) const;
    void ResolveEnemyCollisionsFromGameplay();
    bool CanMoveCardinal(int cellX, int cellY, int dx, int dy) const;
    bool CanMoveToNeighbor(int cellX, int cellY, int dx, int dy) const;
    int CellHash(int cellX, int cellY) const;
    Vec2f CellCenterFromHash(int hash) const;
    EnemyType RandomEnemyType();
    float EnemySpeedForType(EnemyType type) const;
    void UpdateCameraMover(float deltaSeconds);
    void PickNewDestinationAndPathFromPosition(
        const Vec2f& position,
        int& destinationCellHash,
        std::vector<int>& pathCellHashes,
        std::size_t& pathCellIndex);

    bool initialized_ = false;
    MazeState maze_{};
    WorldState plannerWorld_{};
    game::navigation::CellCoordCache plannerCellCache_{};
    std::vector<EnemyTank> enemies_{};
    std::vector<EnemyRuntimeState> enemyRuntime_{};
    game::spatial::SweepPruneBroadPhase collisionBroadPhase_{};
    EnemyRuntimeStats collisionRuntimeStats_{};
    std::vector<Vec2f> frameStartPositions_{};
    std::vector<std::uint8_t> collisionIncludeMask_{};
    std::vector<std::uint8_t> collisionReenteredMask_{};
    std::vector<std::uint32_t> pairVisitedScratch_{};
    std::uint32_t pairVisitedEpoch_ = 1U;
    Vec2f cameraMoverPosition_{.x = 0.0F, .y = 0.0F};
    CameraRuntimeState cameraRuntime_{};
    Random random_{0xC0FFEE12U};
};
