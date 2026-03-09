#include "game/systems/EnemyDrone.h"

#include <cmath>
#include <limits>
#include "core/AngleMath.h"
#include "game/geometry/WorldGeometry.h"
#include "game/model/GameplayConstants.h"
#include "game/systems/EnemySystemHelpers.h"

namespace {

constexpr float kEightDirectionStep = 3.14159265358979323846F / 4.0F;
constexpr float kDroneReturnRequiredClearRunUnits = 6.0F;
constexpr float kEnemyPlanningClearanceScale = 1.5F;

}  // namespace

void EnterDroneWatchMode(WorldState& world, EnemyTank& enemy, Random& random) {
    enemy.aiMode = EnemyAiMode::Watch;
    enemy.aiModeElapsedSeconds = 0.0F;
    enemy.watchRotateDirection = RandomRotateDirection(random);
    const float nearestBaseDist = NearestBaseDistance(world, enemy.position);
    enemy.returnToBase = nearestBaseDist >= 36.0F;
}

bool SelectDroneReturnToBaseHeading(
    const WorldState& world,
    const EnemyTank& enemy,
    Random& random,
    float& selectedHeading) {
    const Vec2f nearestBase = NearestBasePosition(world, enemy.position);
    const Vec2f toBase{
        .x = nearestBase.x - enemy.position.x,
        .y = nearestBase.y - enemy.position.y,
    };
    if (std::fabs(toBase.x) <= 0.001F && std::fabs(toBase.y) <= 0.001F) {
        return false;
    }

    const float desiredHeading = core::angle::QuantizeToEightDirections(
        std::atan2(toBase.x, -toBase.y));
    const std::array<float, 8> offsets{
        0.0F,
        kEightDirectionStep,
        -kEightDirectionStep,
        kEightDirectionStep * 2.0F,
        -kEightDirectionStep * 2.0F,
        kEightDirectionStep * 3.0F,
        -kEightDirectionStep * 3.0F,
        kEightDirectionStep * 4.0F};

    std::array<float, 8> candidateHeadings{};
    int candidateCount = 0;
    int bestCandidateIndex = -1;
    float bestAlignment = std::numeric_limits<float>::infinity();
    for (float offset : offsets) {
        const float candidate = core::angle::QuantizeToEightDirections(desiredHeading + offset);
        const float clearDistance = game::geometry::FreeDistanceAhead(
            world,
            enemy.position,
            candidate,
            kDroneReturnRequiredClearRunUnits,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
            kEnemyPlanningClearanceScale);
        if (clearDistance < kDroneReturnRequiredClearRunUnits) {
            continue;
        }
        const float alignment = core::angle::AngleDistance(candidate, desiredHeading);
        if (candidateCount < static_cast<int>(candidateHeadings.size())) {
            candidateHeadings[static_cast<std::size_t>(candidateCount)] = candidate;
            if (alignment < bestAlignment) {
                bestAlignment = alignment;
                bestCandidateIndex = candidateCount;
            }
            ++candidateCount;
        }
    }

    if (candidateCount <= 0 || bestCandidateIndex < 0) {
        return false;
    }
    if (candidateCount == 1) {
        selectedHeading = candidateHeadings[0];
        return true;
    }

    constexpr float kBestHeadingWeight = 0.6F;
    constexpr float kOtherHeadingsTotalWeight = 0.4F;
    const float otherWeightEach = kOtherHeadingsTotalWeight / static_cast<float>(candidateCount - 1);
    const float pick = random.NextFloat(0.0F, 1.0F);
    float cumulative = 0.0F;
    for (int i = 0; i < candidateCount; ++i) {
        const float weight = (i == bestCandidateIndex) ? kBestHeadingWeight : otherWeightEach;
        cumulative += weight;
        if (pick <= cumulative || i == candidateCount - 1) {
            selectedHeading = candidateHeadings[static_cast<std::size_t>(i)];
            return true;
        }
    }

    selectedHeading = candidateHeadings[static_cast<std::size_t>(bestCandidateIndex)];
    return true;
}

bool SelectDroneWatchEscapeHeading(
    const WorldState& world,
    const std::vector<EnemyTank>& enemies,
    int selfIndex,
    float deltaSeconds,
    float& selectedHeading) {
    const EnemyTank& self = enemies[static_cast<std::size_t>(selfIndex)];

    float currentNearestDistance = std::numeric_limits<float>::infinity();
    int nearestEnemyIndex = -1;
    for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
        if (i == selfIndex) {
            continue;
        }
        const EnemyTank& other = enemies[static_cast<std::size_t>(i)];
        if (!other.alive) {
            continue;
        }
        const float dist = Distance(self.position, other.position);
        if (dist < currentNearestDistance) {
            currentNearestDistance = dist;
            nearestEnemyIndex = i;
        }
    }

    float awayHeading = self.headingRadians;
    if (nearestEnemyIndex >= 0) {
        const EnemyTank& nearestEnemy = enemies[static_cast<std::size_t>(nearestEnemyIndex)];
        awayHeading = std::atan2(
            self.position.x - nearestEnemy.position.x,
            -(self.position.y - nearestEnemy.position.y));
    }

    const std::array<float, 8> offsets{
        0.0F,
        kEightDirectionStep,
        -kEightDirectionStep,
        kEightDirectionStep * 2.0F,
        -kEightDirectionStep * 2.0F,
        kEightDirectionStep * 3.0F,
        -kEightDirectionStep * 3.0F,
        kEightDirectionStep * 4.0F};

    const float stepDistance = GameplayConstants::kEnemyDroneSpeed * deltaSeconds;
    bool found = false;
    float bestNearestDistance = -1.0F;
    float bestAwayAlignment = std::numeric_limits<float>::infinity();
    float bestHeading = self.headingRadians;
    for (float offset : offsets) {
        const float candidateHeading =
            core::angle::QuantizeToEightDirections(self.headingRadians + offset);
        const float clearDistance = game::geometry::FreeDistanceAhead(
            world,
            self.position,
            candidateHeading,
            GameplayConstants::kEnemyRequiredClearRunUnits + 0.5F,
            GameplayConstants::kEnemyWallAvoidanceRadiusUnits,
            kEnemyPlanningClearanceScale);
        if (clearDistance <= GameplayConstants::kEnemyRequiredClearRunUnits) {
            continue;
        }

        const Vec2f dir = core::angle::DirectionFromHeading(candidateHeading);
        const Vec2f candidatePosition{
            .x = self.position.x + dir.x * stepDistance,
            .y = self.position.y + dir.y * stepDistance,
        };

        float nearestDistance = std::numeric_limits<float>::infinity();
        for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
            if (i == selfIndex) {
                continue;
            }
            const EnemyTank& other = enemies[static_cast<std::size_t>(i)];
            if (!other.alive) {
                continue;
            }
            nearestDistance = std::min(nearestDistance, Distance(candidatePosition, other.position));
        }
        const float awayAlignment =
            core::angle::AngleDistance(candidateHeading, awayHeading);

        if (!found ||
            nearestDistance > bestNearestDistance + 0.001F ||
            (std::fabs(nearestDistance - bestNearestDistance) <= 0.001F &&
                awayAlignment < bestAwayAlignment)) {
            found = true;
            bestNearestDistance = nearestDistance;
            bestAwayAlignment = awayAlignment;
            bestHeading = candidateHeading;
        }
    }

    if (!found) {
        return false;
    }

    if (currentNearestDistance < GameplayConstants::kEnemyPreferredSeparationUnits &&
        bestNearestDistance <= currentNearestDistance + 0.001F) {
        return false;
    }

    selectedHeading = bestHeading;
    return true;
}
