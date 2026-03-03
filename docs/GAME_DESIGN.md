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
   - `Invisibility` on/off
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
  - position, heading, type, subtype, alive flag
- `EnemyBase`
  - position, destroyed flag, active enemy count
- `MazeState`
  - grid dimensions and `MazeCell` wall flags (`north/east/south/west`)

Enemy spawn table behavior (`src/game/systems/SpawnerSystem.cpp`):

- Spawn selection uses a fixed 9-entry table of `(EnemyType, EnemySubtype)`, one entry per level slot.
- On spawn at level `L`, choose one random entry from slots `1..L` (inclusive) and use that entry's type/subtype.
- Current table entries are all `Advanced` subtypes:
  - `1..2` Drone, `3..4` Torpedo, `5..7` Hunter, `8..9` Assassin.
- Global alive-enemy cap is `72`.
- Per-base simultaneous alive cap is `12` enemies.
- Each base has its own enemy generation interval assigned at base creation as random `±50%` of `kBaseSpawnCooldownSeconds`.
- Each base also has an enemy generation timer initialized from that interval; timer counts down and is reset to the same interval after a successful spawn.

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
- `Invisibility` checkbox can be toggled via Left/Right or Select when focused.

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
- Enemy subtype controls movement speed multiplier:
  - `Basic`: `75%` of advanced speed
  - `Advanced`: `100%` baseline speed
  - `Hunter Lord`: `125%` of hunter advanced speed
- Assassin advanced speed has two modes: `3.0` world-units/second when player line-of-sight is blocked or out of aggro range, and `6.0` world-units/second when the assassin has line-of-sight to a player in aggro range.
- Enemy projectile firing heading is quantized to the same 8-way (45-degree) directions.
- Player and enemy collision shape is treated as a disc with `9px` diameter.
- Enemy wall movement keeps additional margin: enemy disc edge stays at least `2px` away from maze walls.
- Start mode: at game start (and level restart after all bases are destroyed), player enters a `2s` lock where movement/fire are disabled and fuel fills from `0` to max on HUD.
- Death mode: when player dies, player enters a `3s` lock with movement/fire disabled and a simple explosion animation before life loss + respawn resolution.

### Enemy Separation and Mutual Collision

- Enemies continuously try to keep at least `1.0` world-units between each other.
- If a planned move violates spacing, AI attempts a `45°` turn first; if not possible, enemy stops for that frame.
- If two enemies occupy nearly the same position (`~0.12` world-units), both are destroyed.
- Enemy deaths decrement the origin base `activeEnemies` counter used for per-base spawn capping.
- Bases are one-way obstacles for enemy movement: enemies outside a base footprint cannot enter/touch it, while enemies that spawn inside can leave freely.
- Enemy path-planning clearance checks use an inflated collision margin (`+50%` over the base enemy wall-avoidance radius) to reduce corner-side wall sticking.
- Every spawned enemy gets a per-enemy self-awareness interval and timer:
  - Drone: random in `6..12` seconds.
  - Other enemy types: random in `4..8` seconds.
  - Timer is initialized from the interval and restarts from the same interval when it reaches `0`.

### Enemy Type Behavior

- Drone:
  - Modes: `Wander` and `Watch`.
  - Wander: move straight; if obstacle is within `1` unit ahead, test `±45°` and pick longer free route.
  - If neither side offers `>=3` units of clear run, switch to Watch.
  - Watch: stop and rotate in a random direction (`clockwise` or `counter-clockwise`) chosen when entering Watch.
  - On entering Watch, drone computes distance to nearest base; if distance is `>=36` units, `return-to-base` is enabled.
  - After one full turn (`4s`), if `return-to-base` is enabled, evaluate candidate return headings toward nearest base and discard any heading with less than `6` units of obstacle-free route.
  - If candidates remain, choose return heading by weighted random: best base-aligned candidate has `60%` weight, remaining `40%` is evenly distributed across the other candidates; if no candidates remain, stay in Watch and keep rotating.
  - If `return-to-base` is not enabled, Watch exits only when clear run ahead `>3` and a heading can be selected that improves separation from nearby enemies.
  - On self-awareness timer restart, drone re-checks nearest-base distance; if distance is `>=36`, it first checks relative bearing to nearest base.
  - If relative bearing is `<80°`, drone keeps moving (already generally pointed toward base) and does not enter Watch.
  - If relative bearing is `>=80°`, drone stops and enters Watch.
- Torpedo:
  - Fast local-steering movement (no A* path planning), with turns constrained to `45°` increments only.
  - Detects player with direct line-of-sight and distance `<9` units.
  - Obstacle-anticipation: if forward obstacle is within `16` units, scans `-45°/+45°`; if either side has longer clear path than straight, turns to the longer side.
  - In torpedo steering, "obstacle" checks include walls, undestroyed bases, and other alive enemies.
  - Turn cooldown: must move straight at least `3` units before another turn.
  - If obstacle is directly ahead and straight/left/right are all effectively blocked, enters `Retreat` mode.
  - Retreat mode: moves backward without turning at `10%` normal speed; it must retreat at least `2` units and have forward clearance of at least `2` units before switching to `Rotate` mode.
  - Rotate mode: rotates slowly in random clockwise/counterclockwise direction toward the heading with the longest straight clear path, then returns to regular move mode.
  - Firing additionally requires player to be roughly ahead (`±30°`).
- Hunter:
  - Modes: `Scout`, `Chase`, and rotate fallback.
  - Enters Chase when player has LOS and distance `<12`.
  - Chase keeps stand-off band `3..6` units (approach if farther, retreat if closer, stop inside band).
  - Scout obstacle handling: try `±45°`, then `±90°`; if no option yields `>=3` clear units, rotate clockwise until clear.
- Assassin:
  - Uses hybrid routing with maze-cell A* shortest path (with dynamic enemy occupancy avoidance), not straight-line steering.
  - Repaths when blocked within `2` units or when advancing through path turn points.
  - Avoids ramming by stopping/adjusting when player distance is under `3` units.

### Invisibility Mode

- When menu `Invisibility` is enabled, enemies treat player as non-detectable:
  - no aggro/chase transition based on player visibility/range
  - no enemy projectile firing at player
- Assassin behavior override in invisibility mode:
  - target is a random maze point anywhere in the maze (with distance bias so it is not near current assassin position)
  - target is repicked when the current target is reached.

### Direction Terms

- Relative bearing = angle from your current heading to the target object.

## Rendering and Camera

World rendering is in `src/platform/Renderer2D.cpp`.

- Game world uses full screen area except HUD region.
- Camera target follows player and snaps to pixel grid.
- Maze walls are rendered in screen space at fixed 2px thickness for handheld stability.
- Current gameplay palette (hex): background `#000000`, walls `#CCCCCC`, player `#00FFFF`, drone `#8A2BE2`, torpedo `#FFFF00`, hunter `#FFA500`, assassin `#FF0000`, enemy base shell `#CC66CC`, enemy base core `#FF00FF`, destroyed base `#606060`, player shell `#FFFFFF`, enemy shell `#FFB000`.
- Visible maze cell range is culled for rendering performance.
- Enemy tanks and bases are rendered in pixel-snapped screen space (derived from world positions) to match wall stability on handheld displays.
- Base visuals use a `3x3` unit shell with an empty center square sized as `(1 unit + 8 px)`; a centered "core" disc is drawn inside the hole with diameter `(center hole - 10 px)`.
- Enemy tank visuals load from `resources/textures/sprites.png` (`2x7` grid, `9x9` cells). Rows `4..7` map to `Drone`, `Torpedo`, `Hunter`, `Assassin` (matching `docs/original-1982/ENEMY_TYPES.md` order). Column 1 is facing 12 o'clock, column 2 is 45 degrees clockwise; the renderer precomputes all 8 directions at load time and uses the matching directional frame at draw time. Non-transparent source pixels are normalized to white during load, then tinted by enemy type color at draw time.
- Player tank visuals load from `resources/textures/sprites.png` (`2x7` grid, `9x9` cells). Row `1` is body and row `2` is barrel; each direction frame is prebuilt by XOR-combining body+barrel cells and rendered in cyan (`#00FFFF`), with 8 directions precomputed from the two source columns.
- Enemy sprite rendering uses pixel-snapped screen-space placement derived from world positions with integer sprite scaling (`9x9` source cells rendered at `18x18`, i.e. exact `2x`). Player gameplay footprint remains `kEntitySizeUnits = 1.0`, and the player sprite is rendered in pixel-snapped screen space at fixed `18x18` with per-frame pivot correction to avoid heading-frame jitter.
- HUD direction radar draws three lines: hull heading (white), move joystick vector from gamepad axes `0/1` (sky blue), and fire joystick vector from gamepad axes `2/3` (red). Joystick direction uses `(axisX, axisY)` and amplitude is normalized by raw max magnitude `32768`.
- Gameplay view draws a top-left input debug line at font size `10`: `Axes:  0:...  1:...  2:...  3:...` using gamepad raw axis values (approximate signed 16-bit range).
- Gameplay view also draws a bottom-left single-line counter at font size `10`: alive bases and alive enemies by type (`B/D/T/H/A`).
- HUD minimap plots alive enemies as single-pixel markers in their corresponding colors, bases as `3x3` pixel squares, and player as a larger cyan marker.

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

- original BOLT combat and fuel/lives progression details

When implementing these features, update this document so it remains the single source of truth.
