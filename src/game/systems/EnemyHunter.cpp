#include "game/systems/EnemyHunter.h"

#include <cmath>

#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"
#include "game/systems/EnemySystemHelpers.h"

namespace
{

constexpr float kEightDirectionStep = 3.14159265358979323846F / 4.0F;
constexpr float kEnemyPlanningClearanceScale = 1.5F;

}  // namespace

float SelectScoutHeadingWithFallback(
    const WorldState& world, const EnemyTank& enemy, bool allowNinetyTurns, bool& shouldRotate)
{
    const float lookahead = game::geometry::FreeDistanceAhead(
        world, enemy.position, enemy.headingRadians,
        GameplayConstants::kEnemyLookaheadObstacleUnits,
        GameplayConstants::kEnemyWallAvoidanceRadiusUnits, kEnemyPlanningClearanceScale);
    if (lookahead >= GameplayConstants::kEnemyLookaheadObstacleUnits)
    {
        shouldRotate = false;
        return enemy.headingRadians;
    }

    const std::array<float, 4> turns45{-kEightDirectionStep, kEightDirectionStep, 0.0F, 0.0F};
    const float turned45 = ChooseBestTurnHeading(
        world, enemy.position, enemy.headingRadians, turns45, 2,
        GameplayConstants::kEnemyRequiredClearRunUnits);
    if (!std::isnan(turned45))
    {
        shouldRotate = false;
        return turned45;
    }

    if (allowNinetyTurns)
    {
        const std::array<float, 4> turns90{
            -kEightDirectionStep * 2.0F, kEightDirectionStep * 2.0F, 0.0F, 0.0F};
        const float turned90 = ChooseBestTurnHeading(
            world, enemy.position, enemy.headingRadians, turns90, 2,
            GameplayConstants::kEnemyRequiredClearRunUnits);
        if (!std::isnan(turned90))
        {
            shouldRotate = false;
            return turned90;
        }
    }

    shouldRotate = true;
    return enemy.headingRadians;
}
