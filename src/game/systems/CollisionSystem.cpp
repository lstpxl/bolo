#include "game/systems/CollisionSystem.h"

void UpdateCollisionSystem(GameState& state, float deltaSeconds) {
    (void)deltaSeconds;
    if (!state.world.player.alive) {
        return;
    }

    const float mazeWidthUnits =
        static_cast<float>(state.world.maze.widthCells * state.world.maze.cellSizeUnits);
    const float mazeHeightUnits =
        static_cast<float>(state.world.maze.heightCells * state.world.maze.cellSizeUnits);
    const bool outOfBounds = state.world.player.position.x < 0.0F ||
        state.world.player.position.x > mazeWidthUnits ||
        state.world.player.position.y < 0.0F ||
        state.world.player.position.y > mazeHeightUnits;

    if (outOfBounds) {
        state.world.player.alive = false;
    }
}
