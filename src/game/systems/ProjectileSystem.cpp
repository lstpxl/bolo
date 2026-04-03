#include "game/systems/ProjectileSystem.h"

#include <cmath>
#include <algorithm>
#include "game/model/WorldState.h"

namespace {
Vec2f ViewportCenterWorldPosition(const WorldState& world) {
    return world.panModeActive ? world.panTarget : world.player.position;
}

int FullTierRadiusCellsFromView(const GameplayView& view, int cellSizeUnits) {
    if (cellSizeUnits <= 0) {
        return 0;
    }
    const float cs = static_cast<float>(cellSizeUnits);
    const int dvw = static_cast<int>(std::ceil(view.viewportWidthUnits / cs));
    const int dvh = static_cast<int>(std::ceil(view.viewportHeightUnits / cs));
    return static_cast<int>(std::ceil(static_cast<double>(std::max(dvw, dvh)) + 0.5));
}

bool IsInsideFullTierZone(
    const Projectile& projectile,
    const WorldState& world,
    const game::navigation::MazeCellCoord& fullTierReferenceCell,
    int fullTierRadiusCells) {
    const game::navigation::MazeCellCoord projectileCell =
        world.navigationCache.cellCoords.WorldToCell(projectile.position);
    const int dx = std::abs(projectileCell.x - fullTierReferenceCell.x);
    const int dy = std::abs(projectileCell.y - fullTierReferenceCell.y);
    return std::max(dx, dy) <= fullTierRadiusCells;
}
}  // namespace

void SpawnProjectile(
    GameState& state,
    ProjectileOwner owner,
    const Vec2f& position,
    float headingRadians,
    float speedUnitsPerSecond,
    std::uint32_t shooterEnemySessionId) {
    state.world.projectiles.push_back(Projectile{
        .previousPosition = position,
        .position = position,
        .velocity =
            Vec2f{
                .x = std::sin(headingRadians) * speedUnitsPerSecond,
                .y = -std::cos(headingRadians) * speedUnitsPerSecond,
            },
        .owner = owner,
        .shooterEnemySessionId = shooterEnemySessionId,
        .remainingLifeSeconds = GameplayConstants::kProjectileLifetimeSeconds,
        .alive = true,
    });
    state.world.gameplayEvents.Push(GameplayEvent{
        .type = GameplayEventType::ProjectileFired,
        .position = position,
        .projectileOwner = owner,
    });
}

void UpdateProjectileSystem(GameState& state, float deltaSeconds, const GameplayView& view) {
    const Vec2f viewportCenter = ViewportCenterWorldPosition(state.world);
    const int fullTierRadiusCells = FullTierRadiusCellsFromView(
        view,
        state.world.navigationCache.cellCoords.CellSizeUnits());
    const game::navigation::MazeCellCoord fullTierReferenceCell =
        state.world.navigationCache.cellCoords.WorldToCell(viewportCenter);

    for (Projectile& projectile : state.world.projectiles) {
        if (!projectile.alive) {
            continue;
        }
        projectile.previousPosition = projectile.position;
        projectile.position.x += projectile.velocity.x * deltaSeconds;
        projectile.position.y += projectile.velocity.y * deltaSeconds;

        const bool keepAliveWithoutLifetime = IsInsideFullTierZone(
            projectile,
            state.world,
            fullTierReferenceCell,
            fullTierRadiusCells);

        if (!keepAliveWithoutLifetime) {
            projectile.remainingLifeSeconds -= deltaSeconds;
        }
        if (projectile.remainingLifeSeconds <= 0.0F) {
            projectile.alive = false;
        }
    }
}
