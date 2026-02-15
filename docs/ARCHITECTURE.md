# BOLO Architecture

## Runtime Layers

- `src/app`: app lifecycle and frame loop.
- `src/platform`: raylib input/render adapters.
- `src/game`: game mode orchestration and gameplay systems.
- `src/ui`: raygui menu and HUD rendering.
- `src/core`: shared utility modules like time and random.

## Frame Flow

1. `GameApp` polls input with `PollFrameInput()`.
2. Fixed-step timer accumulates frame time.
3. In fixed updates, game systems execute in order:
   - player
   - enemy
   - projectiles
   - spawner
   - maze
   - collisions
4. Render phase:
   - menu mode draws raygui settings screen
   - game mode draws world camera view + right-side HUD

## Current Scope

- This scaffold intentionally provides structure and compile-safe stubs.
- Maze generation, enemy AI, projectile combat, and full collision logic are not implemented yet.
- The camera is already attached to player state to support top-down navigation.
