#pragma once

#include <future>
#include "game/navigation/PlayerFlowField.h"

namespace game::navigation {

// Holds the in-flight background flow field rebuild state.
// Intentionally NOT part of GameState so GameState remains copyable.
struct FlowRebuildWorker {
    PlayerFlowField pendingFlowField{};
    std::future<void> future{};
    bool inFlight = false;
    int buildGeneration = 0;

    // Maze copy set after InitializeMazeWorld (maze never changes mid-game).
    // Cell coords are copied again in ScheduleRebuild so the async job sees the current
    // player cell / hash (the init-time snapshot never gets UpdatePlayerCell).
    MazeState stableMaze{};
    CellCoordCache stableCellCoords{};

    // Block until any in-flight rebuild finishes. Call before resetting level state.
    void Drain() {
        if (inFlight) {
            future.get();
            inFlight = false;
        }
    }
};

}  // namespace game::navigation
