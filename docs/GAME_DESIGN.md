# BOLT Game Design and Mechanics

This document is the canonical gameplay and mechanics reference for both developers and coding agents.
Update it whenever gameplay behavior changes.

## Scope

This file describes the current BOLT implementation in this repository:

- main menu behavior
- world scale and units
- maze generation rules
- player and enemy/base entity rules
- control mapping and movement model
- rendering/camera behavior

## Core Gameplay Loop

1. Player starts in the main menu.
2. Player selects:
   - `Level` in range `1..9`
   - `Density` in range `1..5`
3. Player starts a generated maze run.
4. Player navigates the maze and interacts with enemy bases/tanks (expanded combat rules are still in progress).
5. Player can return to menu during gameplay with START/Enter.

## World and Unit System

Gameplay constants (see `src/game/GameState.h`):

- `kPixelsPerUnit = 16`
- `kMazeWidthCells = 60`
- `kMazeHeightCells = 60`
- `kMazeCellSizeUnits = 6`
- `kWallThicknessUnits = 0.125` (2px at 16 px/unit)
- `kEntitySizeUnits = 1.0` (player and enemy tank footprint)
- `kEnemyBaseSizeUnits = 3.0`
- `kEnemyBaseCount = 6`

Derived world size:

- maze width: `60 * 6 = 360` world units
- maze height: `60 * 6 = 360` world units

## Maze Generation Rules

Maze generation is handled in `src/game/systems/MazeSystem.cpp`.

Algorithm and constraints:

1. Build a connected base maze using DFS backtracking.
2. Apply density openings:
   - lower density means more extra openings
   - higher density means fewer extra openings
3. Validate maze before acceptance:
   - all cells reachable (`IsMazeFullyAccessible`)
   - neighbor wall consistency (`IsMazeWallTopologyValid`)
   - both horizontal and vertical walls present across all quadrants (`IsWallDistributionValid`)
4. Place exactly 6 enemy bases on unique cells.
5. Place player randomly on cell centers with constraints:
   - no overlap with any base footprint
   - no base visible in initial camera rectangle
   - fallback spawn at cell `(0,0)` if random attempts fail

## Entity Model

Defined in `src/game/GameState.h`.

- `PlayerTank`
  - position, velocity, hull heading, turret heading
  - `throttleNormalized` in range `0..1`
  - alive flag
- `EnemyTank`
  - position, heading, alive flag
- `EnemyBase`
  - position, destroyed flag, active enemy count
- `MazeState`
  - grid dimensions and `MazeCell` wall flags (`north/east/south/west`)

## Controls and Input

Input is collected in `src/platform/Input.cpp`.

### Gameplay

- Turn:
  - keyboard: Left/Right arrows
  - gamepad: D-pad left/right
- Move joystick: gamepad axes `0` and `1` (left stick).
- Fire joystick: gamepad axes `2` and `3` (right stick).
- Throttle:
  - forward: W or Up / D-pad up
  - decelerate: S or Down / D-pad down
- Return to menu while playing:
  - Enter (keyboard) or Start (gamepad)
- Exit app:
  - gamepad combo Start + Select
- If fire joystick is inclined past deadzone, player fires repeatedly on cooldown in fire-joystick direction.

### Menu

- Navigate: Up/Down
- Change slider: Left/Right
- Select: Enter/Space or gamepad south/east face button

## Movement Model

Player movement is handled in `src/game/systems/PlayerSystem.cpp`.

- "Full velocity" is `20.0` units/second.
- Throttle ramp time is 3 seconds from 0 to full:
  - rate = `1.0 / 3.0` per second.
- Forward button increases throttle.
- Reverse/down button decreases throttle.
- Throttle is clamped to `0..1` (no negative velocity, no reverse movement).
- Current velocity is integrated over time (not overwritten each frame).
- Throttle contributes forward acceleration along hull heading.
- Move joystick (`axes 0/1`) is processed in polar coordinates:
  - joystick provides normalized target velocity vector `J`
  - UP/DOWN throttle contributes an additional forward target component (`throttleNormalized` along hull heading)
  - combined target is clamped to normalized magnitude `<= 1`
  - current velocity is normalized as `V` (`velocity / kPlayerFullVelocity`)
  - transform vector is `T = J - V`
  - each update, `V` moves toward `J` at constant rate `kJoystickAcceleration`:
    - `V += normalize(T) * min(|T|, kJoystickAcceleration * dt)`
  - this gives transition time `|T| / kJoystickAcceleration` (e.g. `|T|=1`, `a=1` => `1s`)
  - tank heading is quantized to 8-way facing (45-degree steps) and movement is projected onto that snapped heading
  - turn input behavior: on initial LEFT/RIGHT press, hull turns immediately by 45 degrees; while held, additional 45-degree turns repeat every `0.333` seconds; releasing clears partial repeat timing (no stored half-step momentum)
- There is no passive velocity damping/braking; when joystick returns to neutral, tank keeps its last velocity until another force (input/collision) changes it.
- Enemy movement heading is also quantized to the same 8-way (45-degree) directions.

## Rendering and Camera

World rendering is in `src/platform/Renderer2D.cpp`.

- Game world uses full screen area except HUD region.
- Camera target follows player and snaps to pixel grid.
- Maze walls are rendered in screen space at fixed 2px thickness for handheld stability.
- Visible maze cell range is culled for rendering performance.
- Enemy tanks and bases render in world units.
- Enemy tank visuals load from `resources/textures/sprites.png` (`2x7` grid, `9x9` cells). Rows `4..7` map to `Drone`, `Torpedo`, `Hunter`, `Assassin` (matching `docs/ENEMY_TYPES.md` order). Column 1 is facing 12 o'clock, column 2 is 45 degrees clockwise; the renderer precomputes all 8 directions at load time and uses the matching directional frame at draw time. Non-transparent source pixels are normalized to white during load.
- Player tank visuals load from `resources/textures/sprites.png` (`2x7` grid, `9x9` cells). Row `1` is body and row `2` is barrel; each direction frame is prebuilt by XOR-combining body+barrel cells and rendered in green (`#00E430`), with 8 directions precomputed from the two source columns.
- Enemy sprite rendering is in world space. Player gameplay footprint remains `kEntitySizeUnits = 1.0`, but the player sprite is rendered in pixel-snapped screen space at fixed `16x16` with per-frame pivot correction to avoid heading-frame jitter.
- HUD direction radar draws three lines: hull heading (white), move joystick vector from gamepad axes `0/1` (sky blue), and fire joystick vector from gamepad axes `2/3` (red). Joystick direction uses `(axisX, axisY)` and amplitude is normalized by raw max magnitude `32768`.
- Gameplay view draws a top-left input debug line at font size `10`: `Axes:  0:...  1:...  2:...  3:...` using gamepad raw axis values (approximate signed 16-bit range).

## Main Menu UX

Menu rendering is in `src/ui/MenuScreen.cpp`.

- Menu panel occupies nearly full viewport height (with small margins).
- Includes:
  - title
  - level slider (1..9)
  - density slider (1..5)
  - Start and Quit buttons
  - bottom-aligned build number text
- Quit opens confirmation dialog.

## Build Number

Build number is shown on menu as `Build #<n>`.

- Current source API: `src/app/BuildInfo.h`
- Generated define: `BuildNumber.h`
- Shared counter file: `build/build_number.txt`
- Increment occurs automatically on each CMake build.

## Known Current Gaps

The following systems are placeholders or minimal:

- projectile logic
- enemy AI behavior
- full collision response against maze walls/entities
- original BOLT combat and fuel/lives progression details

When implementing these features, update this document so it remains the single source of truth.
