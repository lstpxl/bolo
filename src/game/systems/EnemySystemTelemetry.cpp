#include "game/systems/EnemySystemTelemetry.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include "core/Log.h"
#include "game/systems/EnemySystemInternal.h"

const char* EnemyTypeTelemetryLabel(int idx) {
    switch (idx) {
    case 0:
        return "D";
    case 1:
        return "T";
    case 2:
        return "H";
    case 3:
        return "A";
    default:
        return "?";
    }
}

void AccumulateEnemyWindowStats(int aliveCount, int visibleCount, int fullTierCount) {
    EnemyRuntimeWindowStats& stats = gEnemyRuntimeWindowStats;
    stats.fixedSteps += 1;
    stats.minAliveCount = std::min(stats.minAliveCount, aliveCount);
    stats.maxAliveCount = std::max(stats.maxAliveCount, aliveCount);
    stats.minVisibleCount = std::min(stats.minVisibleCount, visibleCount);
    stats.maxVisibleCount = std::max(stats.maxVisibleCount, visibleCount);
    stats.minFullTierCount = std::min(stats.minFullTierCount, fullTierCount);
    stats.maxFullTierCount = std::max(stats.maxFullTierCount, fullTierCount);
}

void AccumulateEnemyWindowTime(float deltaSeconds) {
    gEnemyRuntimeWindowStats.windowSeconds += std::max(0.0F, deltaSeconds);
}

void AccumulateEnemyWindowPairStats(const EnemyRuntimeStats& frameStats) {
    gEnemyRuntimeWindowStats.frontalPairsVisited += static_cast<std::uint64_t>(std::max(0, frameStats.frontalPairsVisited));
    gEnemyRuntimeWindowStats.frontalPairsDistanceChecks +=
        static_cast<std::uint64_t>(std::max(0, frameStats.frontalPairsDistanceChecks));
    gEnemyRuntimeWindowStats.separationPairsVisited +=
        static_cast<std::uint64_t>(std::max(0, frameStats.separationPairsVisited));
    gEnemyRuntimeWindowStats.separationPairsResolved +=
        static_cast<std::uint64_t>(std::max(0, frameStats.separationPairsResolved));
}

void ResetEnemyWindowStats() {
    gEnemyRuntimeWindowStats = EnemyRuntimeWindowStats{};
}

void EmitEnemyPeriodicWindowLogs(const EnemyRuntimeStats& frameStats, std::uint64_t frameIndex) {
    bolt::log::Profile(
        "[ENEMY_STATS] frame=%llu alive=%d visible=%d full=%d cheap=%d fullInBase=%d pairs(frontal=%d checks=%d separation=%d resolved=%d)\n",
        static_cast<unsigned long long>(frameIndex),
        frameStats.aliveCount,
        frameStats.visibleInViewportCount,
        frameStats.fullTierCount,
        frameStats.cheapTierCount,
        frameStats.fullTierInBaseClearanceCount,
        frameStats.frontalPairsVisited,
        frameStats.frontalPairsDistanceChecks,
        frameStats.separationPairsVisited,
        frameStats.separationPairsResolved);
    if (gEnemyRuntimeWindowStats.fixedSteps <= 0) {
        return;
    }

    bolt::log::Profile(
        "[ENEMY_WINDOW] steps=%llu alive[min=%d max=%d] visible[min=%d max=%d] full[min=%d max=%d] collisionPasses[runs=%llu skips=%llu]\n",
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.fixedSteps),
        gEnemyRuntimeWindowStats.minAliveCount,
        gEnemyRuntimeWindowStats.maxAliveCount,
        gEnemyRuntimeWindowStats.minVisibleCount,
        gEnemyRuntimeWindowStats.maxVisibleCount,
        gEnemyRuntimeWindowStats.minFullTierCount,
        gEnemyRuntimeWindowStats.maxFullTierCount,
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.collisionPassRuns),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.collisionPassSkips));

    const float windowSeconds = std::max(0.0001F, gEnemyRuntimeWindowStats.windowSeconds);
    {
        std::array<char, 1024> buf{};
        int n = std::snprintf(
            buf.data(),
            buf.size(),
            "[ENEMY_SEGMENTS] window=%.3fs",
            windowSeconds);
        for (int typeIdx = 0; typeIdx < kEnemyTypeTelemetryCount && n > 0 && n < static_cast<int>(buf.size() - 128); ++typeIdx) {
            const std::uint64_t built =
                gEnemyRuntimeWindowStats.segmentsBuiltByType[static_cast<std::size_t>(typeIdx)];
            const std::uint64_t fails =
                gEnemyRuntimeWindowStats.segmentBuildFailsByType[static_cast<std::size_t>(typeIdx)];
            const float builtPerSec = static_cast<float>(built) / windowSeconds;
            const float avgLen = built > 0
                ? (gEnemyRuntimeWindowStats.segmentLengthSumByType[static_cast<std::size_t>(typeIdx)] /
                    static_cast<float>(built))
                : 0.0F;
            n += std::snprintf(
                buf.data() + n,
                static_cast<std::size_t>(buf.size()) - static_cast<std::size_t>(n),
                " %s{b/s=%.2f avgLen=%.2f fail=%llu}",
                EnemyTypeTelemetryLabel(typeIdx),
                builtPerSec,
                avgLen,
                static_cast<unsigned long long>(fails));
        }
        bolt::log::Profile("%s\n", buf.data());
    }

    const float frontalPairsPerSec =
        static_cast<float>(gEnemyRuntimeWindowStats.frontalPairsVisited) / windowSeconds;
    const float frontalChecksPerSec =
        static_cast<float>(gEnemyRuntimeWindowStats.frontalPairsDistanceChecks) / windowSeconds;
    const float separationPairsPerSec =
        static_cast<float>(gEnemyRuntimeWindowStats.separationPairsVisited) / windowSeconds;
    const float separationResolvePerSec =
        static_cast<float>(gEnemyRuntimeWindowStats.separationPairsResolved) / windowSeconds;
    const float frontalKillPct = gEnemyRuntimeWindowStats.frontalPairsDistanceChecks > 0
        ? (100.0F * static_cast<float>(gEnemyRuntimeWindowStats.frontalPairsMutualKills) /
            static_cast<float>(gEnemyRuntimeWindowStats.frontalPairsDistanceChecks))
        : 0.0F;
    const float separationResolvePct = gEnemyRuntimeWindowStats.separationPairsVisited > 0
        ? (100.0F * static_cast<float>(gEnemyRuntimeWindowStats.separationPairsResolved) /
            static_cast<float>(gEnemyRuntimeWindowStats.separationPairsVisited))
        : 0.0F;
    bolt::log::Profile(
        "[ENEMY_GRID] frontal{cand/s=%.1f xcell/s=%.1f ins/s=%.1f} separation{cand/s=%.1f}\n",
        static_cast<float>(gEnemyRuntimeWindowStats.frontalGridCandidates) / windowSeconds,
        static_cast<float>(gEnemyRuntimeWindowStats.frontalGridCellTransitions) / windowSeconds,
        static_cast<float>(gEnemyRuntimeWindowStats.frontalGridInsertEstimate) / windowSeconds,
        static_cast<float>(gEnemyRuntimeWindowStats.separationGridCandidates) / windowSeconds);
    bolt::log::Profile(
        "[ENEMY_COLLISION_WINDOW] frontal{pairs/s=%.1f checks/s=%.1f baseSkip=%llu kill=%llu(%.1f%%)} separation{pairs/s=%.1f resolved/s=%.1f(%.1f%%) baseSkip=%llu kill=%llu wallBlock=%llu}\n",
        frontalPairsPerSec,
        frontalChecksPerSec,
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.frontalPairsBaseSkipped),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.frontalPairsMutualKills),
        frontalKillPct,
        separationPairsPerSec,
        separationResolvePerSec,
        separationResolvePct,
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.separationPairsBaseSkipped),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.separationPairsMutualKills),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.separationPairsWallBlockedPushes));
    const float uncoupleReentryPct = gEnemyRuntimeWindowStats.uncoupleEntries > 0
        ? (100.0F * static_cast<float>(gEnemyRuntimeWindowStats.uncoupleReentryResets) /
            static_cast<float>(gEnemyRuntimeWindowStats.uncoupleEntries))
        : 0.0F;
    bolt::log::Profile(
        "[ENEMY_UNCOUPLE] entries=%llu reentryResets=%llu(%.1f%%) reason{frontal=%llu separation=%llu wallContact=%llu}\n",
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.uncoupleEntries),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.uncoupleReentryResets),
        uncoupleReentryPct,
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.uncoupleEntriesFrontal),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.uncoupleEntriesSeparation),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.uncoupleEntriesWallContact));
    if (gEnemyRuntimeWindowStats.killDebugEnemyEnemyEvents > 0) {
        const float killDistAvg = static_cast<float>(
            gEnemyRuntimeWindowStats.killDebugEnemyEnemyDistanceSum /
            static_cast<double>(gEnemyRuntimeWindowStats.killDebugEnemyEnemyEvents));
        bolt::log::Profile(
            "[ENEMY_KILL_DEBUG] enemyEnemy{events=%llu frontal=%llu separation=%llu reenterEither=%llu reenterBoth=%llu wallContact=%llu dist[min=%.3f avg=%.3f max=%.3f]}\n",
            static_cast<unsigned long long>(gEnemyRuntimeWindowStats.killDebugEnemyEnemyEvents),
            static_cast<unsigned long long>(gEnemyRuntimeWindowStats.killDebugEnemyEnemyFrontalEvents),
            static_cast<unsigned long long>(gEnemyRuntimeWindowStats.killDebugEnemyEnemySeparationEvents),
            static_cast<unsigned long long>(gEnemyRuntimeWindowStats.killDebugEnemyEnemyReenterEither),
            static_cast<unsigned long long>(gEnemyRuntimeWindowStats.killDebugEnemyEnemyReenterBoth),
            static_cast<unsigned long long>(gEnemyRuntimeWindowStats.killDebugEnemyEnemyWallContact),
            gEnemyRuntimeWindowStats.killDebugEnemyEnemyMinDistance,
            killDistAvg,
            gEnemyRuntimeWindowStats.killDebugEnemyEnemyMaxDistance);
    } else {
        bolt::log::Profile(
            "[ENEMY_KILL_DEBUG] enemyEnemy{events=0 frontal=0 separation=0 reenterEither=0 reenterBoth=0 wallContact=0 dist[min=0.000 avg=0.000 max=0.000]}\n");
    }
    const std::uint64_t flowTotalAttempts =
        gEnemyRuntimeWindowStats.navFlowHeadingSelections + gEnemyRuntimeWindowStats.navFlowMisses;
    const float flowHitPct = flowTotalAttempts > 0
        ? (100.0F * static_cast<float>(gEnemyRuntimeWindowStats.navFlowHeadingSelections) /
            static_cast<float>(flowTotalAttempts))
        : 0.0F;
    const float pathBuildSuccessPct = gEnemyRuntimeWindowStats.navPathBuildCalls > 0
        ? (100.0F * static_cast<float>(gEnemyRuntimeWindowStats.navPathBuildSuccesses) /
            static_cast<float>(gEnemyRuntimeWindowStats.navPathBuildCalls))
        : 0.0F;
    bolt::log::Profile(
        "[ENEMY_NAV_CACHE] playerCell{changes=%llu flowRebuilds=%llu} flow{hit=%llu miss=%llu hit%%=%.1f} pathFallback{calls=%llu ok=%llu ok%%=%.1f}\n",
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.navPlayerCellChanges),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.navFlowRebuilds),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.navFlowHeadingSelections),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.navFlowMisses),
        flowHitPct,
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.navPathBuildCalls),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.navPathBuildSuccesses),
        pathBuildSuccessPct);
    bolt::log::Profile(
        "[ENEMY_ASSASSIN_CHEAP_RECOVERY] failByStage{s0=%llu s1=%llu s2=%llu} "
        "recoverByStage{s0=%llu s1=%llu s2=%llu} emergency{attempt=%llu success=%llu} "
        "noFlowSkips=%llu\n",
        static_cast<unsigned long long>(
            gEnemyRuntimeWindowStats.assassinCheapBuildFailsByStage[0]),
        static_cast<unsigned long long>(
            gEnemyRuntimeWindowStats.assassinCheapBuildFailsByStage[1]),
        static_cast<unsigned long long>(
            gEnemyRuntimeWindowStats.assassinCheapBuildFailsByStage[2]),
        static_cast<unsigned long long>(
            gEnemyRuntimeWindowStats.assassinCheapBuildRecoveriesByStage[0]),
        static_cast<unsigned long long>(
            gEnemyRuntimeWindowStats.assassinCheapBuildRecoveriesByStage[1]),
        static_cast<unsigned long long>(
            gEnemyRuntimeWindowStats.assassinCheapBuildRecoveriesByStage[2]),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.assassinCheapEmergencyAttempts),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.assassinCheapEmergencySuccesses),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.assassinCheapNoFlowSkips));
    bolt::log::Profile(
        "[ENEMY_ASSASSIN_WALL_ENTRIES] byPhase{cheap=%llu uncouple=%llu full=%llu}\n",
        static_cast<unsigned long long>(
            gEnemyRuntimeWindowStats.assassinWallAvoidEntriesByPhase[0]),
        static_cast<unsigned long long>(
            gEnemyRuntimeWindowStats.assassinWallAvoidEntriesByPhase[1]),
        static_cast<unsigned long long>(
            gEnemyRuntimeWindowStats.assassinWallAvoidEntriesByPhase[2]));
    bolt::log::Profile(
        "[ENEMY_SAP] updates=%llu active=%llu pairs=%llu repairs{x=%llu y=%llu}\n",
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.sapUpdateCalls),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.sapActiveItems),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.sapCandidatePairs),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.sapXRepairs),
        static_cast<unsigned long long>(gEnemyRuntimeWindowStats.sapYRepairs));
    {
        std::array<char, 1024> buf{};
        int n = std::snprintf(buf.data(), buf.size(), "[ENEMY_PAIR_TYPES] frontal");
        for (int i = 0; i < kEnemyTypeTelemetryCount && n > 0 && n < static_cast<int>(buf.size() - 64); ++i) {
            for (int j = i; j < kEnemyTypeTelemetryCount; ++j) {
                const std::size_t bucket = static_cast<std::size_t>(i * kEnemyTypeTelemetryCount + j);
                n += std::snprintf(
                    buf.data() + n,
                    static_cast<std::size_t>(buf.size()) - static_cast<std::size_t>(n),
                    " %s%s=%llu",
                    EnemyTypeTelemetryLabel(i),
                    EnemyTypeTelemetryLabel(j),
                    static_cast<unsigned long long>(gEnemyRuntimeWindowStats.frontalPairsByType[bucket]));
            }
        }
        n += std::snprintf(buf.data() + n, static_cast<std::size_t>(buf.size()) - static_cast<std::size_t>(n), " separation");
        for (int i = 0; i < kEnemyTypeTelemetryCount && n > 0 && n < static_cast<int>(buf.size() - 64); ++i) {
            for (int j = i; j < kEnemyTypeTelemetryCount; ++j) {
                const std::size_t bucket = static_cast<std::size_t>(i * kEnemyTypeTelemetryCount + j);
                n += std::snprintf(
                    buf.data() + n,
                    static_cast<std::size_t>(buf.size()) - static_cast<std::size_t>(n),
                    " %s%s=%llu",
                    EnemyTypeTelemetryLabel(i),
                    EnemyTypeTelemetryLabel(j),
                    static_cast<unsigned long long>(gEnemyRuntimeWindowStats.separationPairsByType[bucket]));
            }
        }
        bolt::log::Profile("%s\n", buf.data());
    }

    if (gEnemyRuntimeWindowStats.torpedoHeadingEvalCalls > 0) {
        const float evalPerSec =
            static_cast<float>(gEnemyRuntimeWindowStats.torpedoHeadingEvalCalls) / windowSeconds;
        const float retreatPerSec =
            static_cast<float>(gEnemyRuntimeWindowStats.torpedoHeadingRetreatStarts) / windowSeconds;
        const float totalChosen = static_cast<float>(
            gEnemyRuntimeWindowStats.torpedoHeadingChosenStraight +
            gEnemyRuntimeWindowStats.torpedoHeadingChosenLeft +
            gEnemyRuntimeWindowStats.torpedoHeadingChosenRight);
        const float straightPct = totalChosen > 0.0F
            ? (100.0F * static_cast<float>(gEnemyRuntimeWindowStats.torpedoHeadingChosenStraight) / totalChosen)
            : 0.0F;
        const float leftPct = totalChosen > 0.0F
            ? (100.0F * static_cast<float>(gEnemyRuntimeWindowStats.torpedoHeadingChosenLeft) / totalChosen)
            : 0.0F;
        const float rightPct = totalChosen > 0.0F
            ? (100.0F * static_cast<float>(gEnemyRuntimeWindowStats.torpedoHeadingChosenRight) / totalChosen)
            : 0.0F;
        const float avgBestClear = static_cast<float>(
            gEnemyRuntimeWindowStats.torpedoHeadingBestClearSum /
            static_cast<double>(gEnemyRuntimeWindowStats.torpedoHeadingEvalCalls));
        const float avgChosenClear = static_cast<float>(
            gEnemyRuntimeWindowStats.torpedoHeadingChosenClearSum /
            static_cast<double>(gEnemyRuntimeWindowStats.torpedoHeadingEvalCalls));
        bolt::log::Profile(
            "[TORPEDO_HEADING] eval/s=%.2f retreat/s=%.2f pick[straight=%.1f%% left=%.1f%% right=%.1f%%] clear[best=%.2f chosen=%.2f]\n",
            evalPerSec,
            retreatPerSec,
            straightPct,
            leftPct,
            rightPct,
            avgBestClear,
            avgChosenClear);
    }

    ResetEnemyWindowStats();
}
