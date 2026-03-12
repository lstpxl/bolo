#pragma once

#include <array>
#include <cstdint>
#include <limits>

constexpr int kEnemyTypeTelemetryCount = 4;

struct EnemyRuntimeWindowStats {
    int minAliveCount = std::numeric_limits<int>::max();
    int maxAliveCount = 0;
    int minVisibleCount = std::numeric_limits<int>::max();
    int maxVisibleCount = 0;
    int minFullTierCount = std::numeric_limits<int>::max();
    int maxFullTierCount = 0;
    std::uint64_t fixedSteps = 0;
    float windowSeconds = 0.0F;
    std::uint64_t collisionPassRuns = 0;
    std::uint64_t collisionPassSkips = 0;
    std::uint64_t frontalGridCandidates = 0;
    std::uint64_t frontalGridCellTransitions = 0;
    std::uint64_t frontalGridInsertEstimate = 0;
    std::uint64_t separationGridCandidates = 0;
    std::uint64_t frontalPairsVisited = 0;
    std::uint64_t frontalPairsDistanceChecks = 0;
    std::uint64_t separationPairsVisited = 0;
    std::uint64_t separationPairsResolved = 0;
    std::uint64_t frontalPairsBaseSkipped = 0;
    std::uint64_t separationPairsBaseSkipped = 0;
    std::uint64_t frontalPairsMutualKills = 0;
    std::uint64_t separationPairsMutualKills = 0;
    std::uint64_t separationPairsWallBlockedPushes = 0;
    std::array<std::uint64_t, kEnemyTypeTelemetryCount * kEnemyTypeTelemetryCount> frontalPairsByType{};
    std::array<std::uint64_t, kEnemyTypeTelemetryCount * kEnemyTypeTelemetryCount> separationPairsByType{};
    std::array<std::uint64_t, kEnemyTypeTelemetryCount> segmentsBuiltByType{};
    std::array<float, kEnemyTypeTelemetryCount> segmentLengthSumByType{};
    std::array<std::uint64_t, kEnemyTypeTelemetryCount> segmentBuildFailsByType{};
    std::uint64_t torpedoHeadingEvalCalls = 0;
    std::uint64_t torpedoHeadingRetreatStarts = 0;
    std::uint64_t torpedoHeadingChosenStraight = 0;
    std::uint64_t torpedoHeadingChosenLeft = 0;
    std::uint64_t torpedoHeadingChosenRight = 0;
    double torpedoHeadingBestClearSum = 0.0;
    double torpedoHeadingChosenClearSum = 0.0;
    std::uint64_t navPlayerCellChanges = 0;
    std::uint64_t navFlowRebuilds = 0;
    std::uint64_t navFlowHeadingSelections = 0;
    std::uint64_t navFlowMisses = 0;
    std::uint64_t navPathBuildCalls = 0;
    std::uint64_t navPathBuildSuccesses = 0;
    std::array<std::uint64_t, 3> assassinCheapBuildFailsByStage{};
    std::array<std::uint64_t, 3> assassinCheapBuildRecoveriesByStage{};
    std::uint64_t assassinCheapEmergencyAttempts = 0;
    std::uint64_t assassinCheapEmergencySuccesses = 0;
    std::uint64_t assassinCheapNoFlowSkips = 0;
    std::array<std::uint64_t, 3> assassinWallAvoidEntriesByPhase{};
    std::uint64_t sapUpdateCalls = 0;
    std::uint64_t sapActiveItems = 0;
    std::uint64_t sapCandidatePairs = 0;
    std::uint64_t sapXRepairs = 0;
    std::uint64_t sapYRepairs = 0;
    std::uint64_t killDebugEnemyEnemyEvents = 0;
    std::uint64_t killDebugEnemyEnemyFrontalEvents = 0;
    std::uint64_t killDebugEnemyEnemySeparationEvents = 0;
    std::uint64_t killDebugEnemyEnemyReenterEither = 0;
    std::uint64_t killDebugEnemyEnemyReenterBoth = 0;
    std::uint64_t killDebugEnemyEnemyWallContact = 0;
    std::uint64_t killDebugEnemyEnemySamplesPrinted = 0;
    float killDebugEnemyEnemyMinDistance = std::numeric_limits<float>::max();
    float killDebugEnemyEnemyMaxDistance = 0.0F;
    double killDebugEnemyEnemyDistanceSum = 0.0;
    std::uint64_t uncoupleEntries = 0;
    std::uint64_t uncoupleReentryResets = 0;
    std::uint64_t uncoupleEntriesFrontal = 0;
    std::uint64_t uncoupleEntriesSeparation = 0;
    std::uint64_t uncoupleEntriesWallContact = 0;
    std::uint64_t uncoupleSamplesPrinted = 0;
};

extern EnemyRuntimeWindowStats gEnemyRuntimeWindowStats;
