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

    // Block until any in-flight rebuild finishes. Call before resetting level state.
    void Drain() {
        if (inFlight) {
            future.get();
            inFlight = false;
        }
    }
};

}  // namespace game::navigation
