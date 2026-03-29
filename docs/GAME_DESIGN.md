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
6. Any transition from gameplay back to main menu resets active world runtime state (enemies, projectiles, bases, minimap markers, and navigation/collision runtime caches) so the next run starts from a clean world.

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
- Invariant: `kEnemyBaseSizeUnits < cell size`; each base always fits in one cell.

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
5. Place player on cell centers using `BaseDistanceField` (maze cardinal distance to nearest alive base):
   - initial spawn distance bounds: `8..20` cells from nearest base
   - no overlap with any base footprint
   - no base visible in initial camera rectangle
   - deterministic fallback picks the farthest valid cell by base-distance, then `(0,0)` if none

## Entity Model

Defined in `src/game/GameState.h`.

- `PlayerTank`
  - position, velocity, hull heading, turret heading
  - `throttleNormalized` in range `0..1`
  - alive flag
- `EnemyTank`
  - position, heading, type, subtype, alive flag
- `EnemyBase`
  - position, `destroyed` flag (core destroyed), active enemy count
  - segmented outer layer health: `top/right/bottom/left`, each `0..4` (`4` at spawn)
  - repair countdown timer (`4.0s`) used for automatic +1 healing ticks
- `MazeState`
  - grid dimensions and `MazeCell` wall flags (`north/east/south/west`)

Enemy spawn table behavior (`src/game/systems/SpawnerSystem.cpp`):

- Spawn selection uses `game::EnemyTypesForLevel(level)` in `src/game/EnemyAppearance.h`, which returns a hardcoded list of spawnable types per level. Caller picks one at random.
- Per-level mapping: `1–2` Drone, `3–4` Drone+Torpedo, `5–6` Drone+Torpedo+Hunter, `7` Hunter, `8` Hunter+Assassin, `9` Assassin only.
- Global alive-enemy cap is `999`.
- Per-base simultaneous alive cap is `24` enemies.
- **Level 9 (debug):** Assassins only, 6 per base max, assassin speed ×4. Intended for flow-field debugging.
- On spawn, enemy position is initialized inside the base with heading-aligned symmetry through base center:
  - cardinal heading: tank nose points at the middle of the matching base side.
  - diagonal heading: tank nose points at the matching base corner, then spawn center is shifted `0.5` world-units toward base core.
- Spawn safety gate: before creating an enemy, spawn path from the initial spawn position must be clear for at least `6` world-units in its spawn heading (walls/bases/enemies considered). If blocked, base skips this attempt and retries next generation interval.
- Failed spawn attempts (`no valid spawn heading`, `spawn point occupied`, or `6`-unit path gate blocked) reset that base's generation timer and wait one full generation interval before retry.
- Spawn direction filter from damaged armor segments:
  - damaged `top` blocks heading candidates toward top and top-diagonals (`N`, `NE`, `NW`)
  - damaged `right` blocks (`E`, `NE`, `SE`)
  - damaged `bottom` blocks (`S`, `SE`, `SW`)
  - damaged `left` blocks (`W`, `NW`, `SW`)
- Damaged segment definition: any segment with health `< 4`. Segment at `0` is breached and no longer protects the core from that side.
- Base self-repair: when any segment is damaged, base repair countdown runs for `4.0s`; on expiry, the most damaged segment heals by `+1` (deterministic tie break `top`, `right`, `bottom`, `left`), then the timer repeats while any segment remains damaged.
- Enemies inside or near any undestroyed base (base footprint + 1 unit clearance) are excluded from enemy–enemy mutual-kill and separation; they never die from base-proximity collisions.
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
  - forward: Up arrow / D-pad up
  - decelerate: Down arrow / D-pad down
  - These keys affect throttle only, not the move joystick target (prevents brake acceleration artifact).
- **Mac keyboard:** Cursor keys (Up/Down/Left/Right) control tank movement and turn; WASD control camera pan when pan mode is active (P toggles)—camera moves one cell per frame while each direction key is held. Invisibility is toggled by I key during gameplay (default off).
- Return to menu while playing:
  - Enter (keyboard) or Start (gamepad)
- Exit app:
  - gamepad combo Start + Select
- If fire joystick is inclined past deadzone, player fires repeatedly on cooldown in fire-joystick direction.

### Menu

- Navigate: Up/Down (vertical order: `Start` → `Level` → `Density` → `Debug info` → `Quit`; wrap at ends)
- Level slider: Left/Right when the Level control is focused
- Density: by default only the centered `Density` + digit row is shown inside the same focus frame size as `Debug`/`Start`/`Quit` (`unifiedMenuFocusW` × `kMenuQuitButtonHeight`). Left/Right when Density is focused changes the value and, each press, hides the text for `1` second while the five hatch sprites appear on the **same vertical band** as that text (centered horizontally); the selected sprite gets the focus ring while Density stays focused. Another Left/Right during that second updates the value and restarts the `1` s timer. Mouse applies only while sprites are visible (click picks a value and extends the timer). If you move focus to another menu row while the preview is active, sprites stay until the `1` s timer ends (then the text row returns); sprite focus rings only show while Density is focused or a sprite is hovered.
- Select: Enter/Space or gamepad south/east face button
- `Debug info: On` / `Debug info: Off` centered text toggles via Left/Right or Select when focused, or click when the mouse is over the line.
- Default menu values at app start are `Level = 9`, `Density = 1`, `Debug info = Off`. Invisibility defaults to off and is toggled during gameplay by I key.

## Movement Model

Player movement is handled in `src/game/systems/PlayerSystem.cpp`.

- "Full velocity" is `20.0` units/second.
- Throttle ramp time is 1.5 seconds from 0 to full (2× responsive):
  - rate = `2.0 / kPlayerSecondsToFullVelocity` per second.
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
  - each update, `V` moves toward `J` at constant rate `kJoystickAcceleration * 2`:
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
- Assassin advanced speed has two modes: `1.5` world-units/second when not in aggro mode, and `3.0` world-units/second when `assassinInAggroMode` is true (assassin, `seesPlayer`, and distance `<= kEnemyAggroRangeUnits`).
- Enemy projectile firing heading is quantized to the same 8-way (45-degree) directions.
- Enemy projectile firing is gated by `enemy.seesPlayer` (alive + unobstructed + in-range + inside forward cone), plus per-type firing constraints.
- Enemy projectile spawn range uses per-type fire ranges (`Drone`: `kDroneDetectRangeUnits`; `Torpedo` `Ram`: `kTorpedoRamDetectRangeUnits`; `Torpedo` non-`Ram`: `kTorpedoDetectRangeUnits`; `Hunter`: `kHunterDetectRangeUnits`; `Assassin`: `kEnemyAggroRangeUnits`), not a single global range.
- Player and enemy collision shape is treated as a disc with `9px` diameter.
- **Enemy dual-radius model:** agents use two radii (universal for all enemy types):
  - **hardRadius** (`kEnemyCollisionRadiusUnits`): collision radius for overlap and hit checks.
  - **softRadius** (`kEnemyAvoidanceRadiusUnits`): avoidance radius for steering (wall clearance, path planning, separation). Soft radius is larger than hard radius.
- Enemy wall movement keeps additional margin: enemy disc edge stays at least `2px` away from maze walls.
- Start mode: player enters a `2.5s` refuel lock where fuel fills from `0` to max on HUD. During this lock (new game, respawn, and level restart), hull/turret rotation remains enabled while movement/fire stay disabled.
- Death mode: when player dies, player enters a `3s` lock with movement/fire disabled and a simple explosion animation before life loss + respawn resolution.
- Respawn safety uses `BaseDistanceField` with bounds `12..24` cells from nearest alive base.
- Respawn additionally requires no alive enemies within Manhattan distance `<= 3` cells.
- If random respawn placement fails, fallback first picks the farthest strict-valid cell by base-distance; if no strict candidate exists, fallback relaxes base-distance bounds but still enforces enemy-clearance.

### Collision System and Radius Usage

`CollisionSystem.cpp` uses:

- **`kWallClearanceForHard`** – wall checks (IsPointInWall) for player and enemy; projectile-kill debug (expansion beyond wall half-thickness = `kEntityRadiusUnits`).
- **`kPlayerEnemyCollisionRadius`** – player–enemy overlap (`2 × kEntityRadiusUnits`).
- **`kProjectileHitRadius`** – projectile vs enemy/player hit detection (0.7 units).
- **`kPlayerBaseHardCollisionUnits`** – player death when inside base (halfBase + entityRadius).
- Player projectile vs base:
  - Impact side is resolved by dominant axis from base center (`abs(dx) >= abs(dy)` => left/right, else top/bottom).
  - If impact segment health is `> 0`, that segment takes `1` damage and projectile is consumed.
  - Core is killed only by a projectile inside the core radius that approaches through a breached (`0` health) segment.
  - Base destruction occurs only on core hit; outer-layer hits alone cannot destroy a base.

Enemy movement/steering code uses **`kWallClearanceForAvoidance`** (passed into geometry) and **`kEnemyWallAvoidanceUnits`** / **`kEnemyAvoidanceRadiusUnits`** for clearance queries. Wall model: finite line extruded as pill by half-thickness; bases use the same formula (halfSize + entityRadius + optional clearance).

### Enemy Separation and Mutual Collision

- Terminology convention:
  - `distance` = center-to-center distance between two entities.
  - `clearance` = disc-edge-to-disc-edge spacing between two entities (`clearance = distance - (r1 + r2)`).

- Enemies continuously try to keep at least `1.0` world-units of `clearance` (equivalent to center `distance >= 2.0` with `r1=r2=0.5`).
- If a planned move violates spacing, AI attempts a `45°` turn first; if not possible, enemy stops for that frame.
- Enemy-vs-enemy overlap does **not** kill enemies.
- On overlap/collision detection (frontal and near-separation paths), both enemies enter `Uncouple` mode for `1.0s`.
- `Uncouple` mode is available to all enemy types and temporarily overrides their normal AI mode to move away from nearby enemies, then restores the previous mode when the timer expires.
- On near-separation conflicts (`distance < preferred separation`), enemies are no longer position-pushed directly in the collision pass; both enemies enter `Uncouple` mode instead.
- Re-entering `Uncouple` while already in `Uncouple` does not reset the remaining uncouple timer.
- Re-entering `Uncouple` from enemy-pair collision paths while already in `Uncouple` also preserves the current uncouple heading target (no heading retarget on pair re-entry); wall-contact re-entry can still refresh heading.
- For near-separation re-entry, if both enemies are already in `Uncouple`, re-triggering is gated by a stricter center-distance threshold (`<= 1.5` units); otherwise, the regular preferred-separation threshold (`< 2.0`) is used.
- Wall-contact uncouple (segment or edge-on wall hit during movement) likewise does not reset the timer on re-entry when already in `Uncouple`; the pre-`EnterUncoupleMode` timer zero is skipped.
- `Uncouple` per-step steering combines force components as `pathFollowing + (separation + obstacleAvoidance + wallAvoidance) + randomNoise`.
- `pathFollowing` is always clamped so its magnitude is not greater than `separation` magnitude for that step.
- Uncouple priority/yield (used to break deadlocks between two uncouple agents) is restricted to overlap recovery only: it cannot create a new preferred-separation violation, cannot reduce spacing while already under preferred separation, and cannot cross the hard overlap distance.
- Uncouple heading candidate scoring and uncouple-priority clear-ahead scoring use static-obstacle plus enemy-aware clearance (not walls-only), so selected uncouple headings avoid dense enemy lanes when alternatives exist.
- `obstacleAvoidance` is computed from nearby enemies ahead of the desired uncouple direction (short-range, direction-aware).
- `wallAvoidance` uses short-range clearance probes around the enemy and pushes away from blocked directions.
- While in `Uncouple`, each enemy also receives a tiny per-update random force component to break deadlocks/equilibrium (for example exact-overlap pairs or groups contesting narrow passages).
- Full-tier movement wall handling additionally checks side-clearance while moving (edge-on/parallel-to-wall case); if side contact risk is detected, enemy enters `Uncouple` mode instead of sliding into wall-stick behavior.
- On full-tier wall-contact handling (`segmentWallHit` or edge-on wall contact), the enemy also runs a short-range recovery search to reposition its center to the nearest valid non-wall/non-base point (using avoidance clearance) before continuing uncouple behavior. This prevents persistent "stuck at wall endpoint" states.
- On death, an enemy explosion animation plays at the enemy's position: `resources/textures/explosion-1.png` is a 6-frame horizontal spritesheet (each frame `32×32` px), played at `0.15s` per frame (total `0.9s`). Up to `64` simultaneous explosions are tracked in `WorldState::enemyExplosions`. The explosion is rendered at `32×32` screen pixels (1× source scale), centered on the death position. The enemy entity is removed from the simulation immediately on death; only the explosion visual persists.
- When a base is destroyed, a one-shot explosion animation plays at the base center: `resources/textures/explosion-3-large.png` is a 6-frame horizontal spritesheet (each frame `64×64` px), played at `0.15s` per frame (total `0.9s`). Up to `6` simultaneous base explosions are tracked in `WorldState::baseExplosions`. The explosion is rendered as `4×4` world units (`64` screen pixels at `16px/unit`), centered on the base. Spawning is gated by `EnemyBase::explosionPlayed` so each base triggers at most one explosion.
- Player death uses `resources/textures/explosion-2.png` (6-frame `32×32` px horizontal spritesheet, `0.15s` per frame). The animation plays once at `kPlayerExplosionRenderWorldUnits = 2` world units (`32` screen pixels), centered at the player's death position. Rendered in world space inside the `BeginMode2D` camera block. The animation does not loop; after `0.9s` (6 frames) nothing is drawn for the remaining death-mode duration.
- Bases are one-way obstacles for enemy movement: enemies outside a base footprint cannot enter/touch it, while enemies that spawn inside can leave freely.
- Enemy path-planning clearance checks use an inflated collision margin (`+50%` over the base enemy wall-avoidance radius) to reduce corner-side wall sticking.
- Clearance query methods:
  - `FreeDistanceAheadGrid`: default static-obstacle clearance for gameplay AI; traverses maze grid cells/edges (DDA-style) and returns first hit distance against walls/bases.
  - `FreeDistanceAheadContinuous`: retained as sampled legacy implementation for offline comparison only; gameplay runtime does not call it.
- Every spawned enemy gets a per-enemy self-awareness interval and timer:
  - Drone: random in `5..8` seconds (`GameplayConstants::kDroneSelfAwarenessIntervalMinSeconds` / `MaxSeconds`); after each expiry the next interval is re-rolled in the same range (full and cheap tier).
  - Other enemy types: random in `4..8` seconds.
  - Timer is initialized from the interval when missing; when it reaches `0`, non-drones restart from the same stored interval; drones pick a new random interval as above.

### AdjacentCellSegmentPlanner

Local planner for a **single step** to one of the **8 adjacent** maze cells (cardinal or diagonal). Code: `src/game/navigation/AdjacentCellSegmentPlanner.h`, `AdjacentCellSegmentPlanner.cpp`; entry point `game::navigation::AdjacentCellSegmentPlanner::Build`. Torpedo `Fly` uses it from `EnsureTorpedoFlyPath` to produce 1 or 2 waypoint positions per plan. Extraction allows reuse by other movement code without duplicating topology rules.

- **Input:** current cell `fromCell`, chosen neighbor `targetCell` (Chebyshev distance 1), current world position `startPosition`.
- **Cardinal:** one segment to `targetCell` center if the segment from `startPosition` to that center does not intersect walls at avoidance clearance **and** `IsValidSegmentEndpoint` accepts the center.
- **Diagonal:** no additional per-segment geometry validation beyond the rules below; routing uses **maze edge topology** only.
  - Let `B = (fromX, fromY + dy)` and `C = (fromX + dx, fromY)` for diagonal step `(dx, dy)`.
  - Evaluate the four cardinal edges: `from→B`, `B→target`, `from→C`, `C→target` via the same traversability rules as grid movement (walls + base-blocked cells).
  - **Hard fail** if ≥3 of those edges are blocked, or both edges leaving `from` are blocked, or both edges entering `target` are blocked.
  - If **both** `B`-route and `C`-route are usable (each pair of incident edges open): **one** segment to target cell center.
  - If **exactly one** bend route is usable: **two** segments — first waypoint at the usable bend cell center, shifted by `kAdjacentCellDiagonalMidpointShiftUnits` (`GameplayConstants`, default `1` world unit) along the unit vector toward the **opposite** bend cell; second waypoint at target cell center.
  - If **neither** route is usable: build fails.

### Enemy Type Behavior

#### Drone

- Modes: `Wander` and `Watch`.
- If drone has LOS to an alive player within `12` units, it enters player pursuit: speed is set to `50%` of its normal drone speed and heading is selected from 8-way directions toward the player.
- Pursuit heading selection prefers the nearest angular 8-way direction (typically straight or `45°` diagonal), while still enforcing wall and enemy clearance.
- During pursuit, drone applies player-specific spacing and will not choose a step that brings it within `4` units of the player center.
- Wander: move straight; if obstacle is within `1` unit ahead, test `±45°` and pick longer free route.
- If neither side offers `>=3` units of clear run, switch to Watch.
- Watch: stop and rotate in a random direction (`clockwise` or `counter-clockwise`) chosen when entering Watch.
- On entering Watch, drone uses `BaseDistanceField` maze graph distance (cells) to the nearest alive base; if that distance is at least `GameplayConstants::kDroneReturnToBaseMinBaseDistanceCells` (`36` world-units / `kMazeCellSizeUnits`), `return-to-base` is enabled. If the cache is not built yet, the game rebuilds `BaseDistanceField` and `BaseFlowField` first (same pipeline as maze init / base destruction).
- After one full turn (`4s`), if `return-to-base` is enabled, evaluate candidate return headings aligned with `BaseFlowField` toward the nearest base and discard any heading with less than `6` units of obstacle-free route.
- If candidates remain, choose return heading by weighted random: best base-aligned candidate has `60%` weight, remaining `40%` is evenly distributed across the other candidates; if no candidates remain, stay in Watch and keep rotating.
- If `return-to-base` is not enabled, Watch exits only when clear run ahead `>3` and a heading can be selected that improves separation from nearby enemies.
- On self-awareness timer restart, drone re-checks the same distance rule as Watch (`BaseDistanceField`, rebuilding caches if needed); if far enough, it checks relative bearing to `BaseFlowField`’s next step toward the nearest base (rebuilding caches if needed).
- If relative bearing is `<80°`, drone keeps moving (already generally pointed toward base) and does not trigger recovery.
- If relative bearing is `>=80°`, drone stops and runs **`DroneReset`**: score-weighted random choice among 8 directions (same structure as hunter scout scoring: exponential decay on maze run length and first-cell enemy count, `core::math::ExpDecayA1K02(turnSteps)` from current heading to candidate, `core::math::ExpDecayA1K07(flowTurnSteps)` vs `BaseFlowField`’s next cardinal step when flow exists). **Cheap tier:** snap heading to the chosen direction and `Wander`. **Full tier:** enter `Watch` with a target heading, rotate in place at the same rate as normal Watch (`4s` full turn) until aligned (within a small angle), then `Wander`.
- **Cheap-tier segment boxed-in:** when the forward / `±45°` probe cannot produce a segment (`bestClear - 4 < 2`), drone also runs **`DroneReset`** (same scoring) instead of picking a random heading.

#### Torpedo

- Modes: `Fly`, `Ram`, `Retreat`, `Targeting`, and `Rotate` (stored in `EnemyAiMode` alongside other type modes).
- Fast local-steering movement (no A* path planning). **Full-tier `Fly`** uses the same cell-based segment pipeline as cheap-tier (`EnsureTorpedoFlyPath` + **AdjacentCellSegmentPlanner**); hull heading slews toward the current segment waypoint bearing at `kTorpedoFullTierTurnSpeedRadiansPerSecond` (`45°/s`). Segment renewal is cell-based (not distance-threshold based): a new segment is planned when the torpedo enters the segment planner target cell while following the final segment leg of the active fly path. Diagonal segment rules are defined under **AdjacentCellSegmentPlanner** above. If building or following the path fails, full-tier falls back to the legacy three-way probe (`SelectTorpedoMoveHeading`: forward / ±45°, clearance to `15` units) with **no** random straight-hold distance.
- Spawn heading lock: after spawn, torpedo keeps initial heading while inside base footprint plus `1.0` world-unit clearance; turning decisions are disabled until this clearance is exited.
- Player detection for ram transition is throttled to every `0.25s` (cached between checks), using LOS and distance `<=12` units.
- **Enemy forward vision (`seesPlayer`):** `seesPlayer` is true only when all three conditions hold: unobstructed geometric LOS to an alive player (no invisibility, no wall obscurity on the segment), player is within per-type visual detect range (`Drone`: `kDroneDetectRangeUnits`; `Torpedo`: `kTorpedoRamDetectRangeUnits`; `Hunter`: `kHunterDetectRangeUnits`; `Assassin`: `kEnemyAggroRangeUnits`), and player is inside the forward arc from hull heading (default half-angle ±90° via `GameplayConstants::kEnemyForwardVisionHalfAngleRadiansDefault`; torpedo uses ±45° via `kTorpedoForwardVisionHalfAngleRadians`). Outside range or outside arc, `seesPlayer` is false.
- Torpedo enters `Ram` whenever an alive player is seen (LOS + `12` units + torpedo forward-arc rule), and returns to `Fly` when LOS/range is lost or the player dies.
- **Full-tier** torpedo heading changes: `Ram` toward player; `Fly` along fly path toward waypoint bearing (or fallback probe toward an 8-way target); `Retreat`/uncouple toward 8-way targets; `Rotate` toward chosen heading — all capped at `kTorpedoFullTierTurnSpeedRadiansPerSecond` (`45°/s`).
- In `Ram`, torpedo fires only when player is inside a tighter forward cone (`±20°`).
- If a `Ram` torpedo collides with player, both die (player death + torpedo explosion).
- Torpedo **cheap-tier** movement uses constant speed `kEnemyTorpedoSpeed * EnemySubtypeSpeedMultiplier` (`Basic` = `×0.75`), no acceleration ramp.
- Torpedo **full-tier** forward speed is not held to a fixed cruise each frame: per-step acceleration is capped by `kTorpedoFullTierAccelMaxUnitsPerSecondSq * EnemySubtypeSpeedMultiplier` (`Basic` = `×0.75`).
  - In `Ram` with LOS to player: along-hull acceleration depends on bearing to player (minimum angle, radians): if `< π/4` (`45°`), `acc = maxAcc * (π/4 - |Δ|) / (π/4)`; else `acc = -maxAcc * |Δ| / (3π/4)` (`135°` scale). Speed is clamped forward to `[0, kEnemyTorpedoSpeedMax * EnemySubtypeSpeedMultiplier]` for that frame.
  - In `Fly`: `torpedoCurrentSpeed` ramps toward `kEnemyTorpedoSpeed * EnemySubtypeSpeedMultiplier` with per-step delta capped by scaled `maxAcc` (same as cruise when `Ram` has no LOS to player).
  - In `Ram` without LOS to player: same cruise ramp as `Fly`.
  - Other full-tier modes (`Retreat`, `Uncouple`, `Targeting` stationary): `torpedoCurrentSpeed` tracks AI `targetSpeed` with the same `maxAcc` cap per step.
- Full-tier torpedo wall handling (segment `previousPosition → candidatePosition`):
  - **Hard** wall pill (`kEnemyWallHardCollisionUnits` from the segment; clearance passed to `SegmentIntersectsWall` is `kEnemyWallHardCollisionUnits - kWallHalfThicknessUnits`, i.e. `kWallClearanceForHard`) while hull speed magnitude is above a small epsilon: **explode** immediately. Enemy **separation / overlap stall and turn** are **skipped** when that intended step already has a hard hit, so high speed cannot be cancelled before the wall check.
  - **Avoidance** violation (segment hits the wall pill matching `kEnemyWallAvoidanceUnits`, i.e. `kWallClearanceForAvoidance`; or parallel **edge-on-wall** contact) without taking the hard crash branch above: still advance to `candidatePosition`, and **brake** signed `torpedoCurrentSpeedUnitsPerSecond` toward zero using the full per-step cap `kTorpedoFullTierAccelMaxUnitsPerSecondSq * EnemySubtypeSpeedMultiplier` (same magnitude as forward acceleration). **No** recovery-position slide and **no** uncouple.
- **Full-tier `Fly` fallback** (when fly path is unavailable): three headings (`forward`, `-45°`, `+45°`), clearance to `15` units with enemy-aware probes; pick max clearance (random tie-break). If max forward run after safety margin is `< 2` units, or all three clears `< 1`, enter `Retreat`.
- In torpedo steering, "obstacle" checks include walls, undestroyed bases, and other alive enemies.
- Retreat mode: moves backward without turning at `10%` normal speed; it checks only retreat completion distance and near forward clearance to leave the mode.
- After retreat completion, torpedo enters `Targeting` mode: picks and stores the heading with the longest straight clear path.
- Rotate mode: rotates toward the stored chosen heading at `kTorpedoFullTierTurnSpeedRadiansPerSecond` and returns to regular move mode when aligned.
- Firing additionally requires player to be roughly ahead (`±30°`).

#### Hunter

- Modes: `Scout`, `Chase`, and rotate fallback.
- Enters Chase when player has LOS and distance `<12`.
- Chase keeps stand-off band `3..6` units (approach if farther, retreat if closer, stop inside band).
- Scout uses a two-level planner shared across Full/Cheap tiers:
  - **Cell-level choice:** all heading decisions use 8 integer direction indices (0..7). Current float heading is converted to dir index first; relative turn cost, scoring, and tie-break use integer math. Float heading is derived from dir index only at movement output.
  - Evaluate all 8 neighbor directions; score uses exponential-decay weighting:
      `score = (1 - core::math::ExpDecayA1K07(runCells)) * core::math::ExpDecayA1K07(enemiesInFirstCell) * (1 - 0.2 * turnSteps)`.
      Here `runCells` is traversable run length in the candidate direction, `enemiesInFirstCell` is alive enemies in the first cell of that direction, and `turnSteps` is relative heading turn cost in 45-degree steps.
  - Cells occupied by alive bases are treated as blocked.
  - **Segment-level execution:** build a persisted `1` or `2` segment path toward the chosen neighbor-cell center.
  - Direct segment is preferred; for diagonal half-block cases, endpoint adjustment within `2` units around target center is attempted, then a `2`-segment bend path is attempted.
  - Candidate scout segment endpoints are rejected if they lie inside wall-avoidance space (using `kWallClearanceForAvoidance`), preventing path endpoints from being placed too close to wall endpoints/corners.
  - If no valid Scout path can be built, hunter enters rotate fallback.

#### Assassin

- Modes: `Pursuit` (default) and temporary `Uncouple`.
- Uses a pure player-directed maze flow-field for pursuit steering (no waypoint A* path following in the active runtime branch).
- Each assassin computes and caches only the next flow-field step heading per current cell; cached heading is reused until the assassin leaves that cell.
- Avoids ramming by stopping/adjusting when player distance is under `3` units.

### Player-Directed Flow-field

- Flow-field build treats cells occupied by undestroyed bases as blocking (non-traversable).
- Flow-field is *active* when invisibility is off and either (a) the level can spawn assassins/hunters (levels 5+) or (b) menu `Debug info` is on. This keeps player-directed flow overlays available in debug mode on lower levels. Toggling invisibility (I key) always invalidates the current flow cache: invisibility on clears stale flow immediately; invisibility off activates flow rebuild toward the player.
- Flow-field initial build is requested at level init (InitializeMazeWorld), not during enemy processing. This ensures assassins never wait for a build; the field is ready when the first assassin spawns.
- Player respawn invalidates the flow field so it is rebuilt for the new player position; any in-flight background rebuild for the old position is discarded.
- Enemy steering does not require player-cell-version freshness; cached flow-field data remains valid until scheduled cache refresh.
- Flow-field cache refresh runs only while cache is active and player crosses cell borders: cache `age` starts at `0`, increments on each refresh attempt, early-exits while `age <= 2`, and rebuilds when `age > 2` (effective rebuild cadence: once per 3 player-cell changes). On each early-exit transition `A -> B`, flow direction for cell `A` is patched to point to cell `B`.

### A* waypoint path builder

- A* waypoint path builder remains in code as a disabled backup branch and is not wired into active assassin pursuit.

### Enemy Collision Broad Phase

- Enemy-vs-enemy frontal collision and separation candidate generation uses cached Sweep-and-Prune arrays on X and Y axes.
- Runtime keeps sorted `{id, sortField}` arrays with `id -> index` lookup and local insertion-repair updates after movement, then runs narrow-phase checks only on broad-phase candidates.
- `EnemySpatialGrid` remains in use for ray/proximity filtering (`FreeDistanceAheadWithEnemies`) and is not used as the frontal/separation broad phase.

### Offscreen Enemy Simulation LOD

- Enemy simulation uses two runtime tiers:
  - `Full`: enemy cell is within Chebyshev distance `<= 3` from the player cell.
  - `Cheap`: enemy is farther than that (`> 3` cells).
  - Tier selection uses this unified distance rule for all enemy types; AI mode does not override tier.
- In `Cheap` tier, enemies use cached segment movement:
  - heading is quantized to 8-way direction
  - drones choose next segment heading from `forward/-45/+45` by longest wall-only clearance (capped at `15`), with random tie-break
  - segment length is randomized in `[2, (bestClear-4)]` (clamped by per-type segment cap)
  - movement advances toward cached endpoint and endpoint is recomputed only when current endpoint is reached
  - no wall/enemy/base collision checks are executed while traversing the current cheap-tier segment
  - on cheap-tier segment-build failure (`bestClear - 4 < 2`):
    - drone runs **`DroneReset`** (score-weighted 8-way direction, then snap heading and `Wander`); next update retries segment build from the new heading
- Hunter exception:
  - cheap-tier Hunter `Scout` uses the same two-level planner as full-tier `Scout` (8-direction cell scoring + persisted 1/2 segment path), so Scout steering behavior is tier-consistent.
  - cheap-tier Hunter scout segment traversal uses continuous heading toward current segment target point (no 8-way heading snap during traversal), preventing drift away from validated segment geometry.
  - tier difference remains collision handling: cheap-tier hunters skip full-tier enemy-collision post-passes.
- Cheap-tier enemies do not participate in enemy-enemy frontal-collision and separation post-passes.
- Cheap-tier enemies do not run enemy-enemy or enemy-base collision checks at all; wall clearance is evaluated only when selecting the next cheap-tier segment.
- Torpedo cheap-tier behavior:
  - cheap-tier torpedoes are normalized to `Fly` mode (non-`Fly` modes are dropped on the cheap-tier path).
  - cheap-tier `Fly` uses the same planner shape as hunter `Scout` (8-direction cell scoring + **AdjacentCellSegmentPlanner** to an adjacent cell), but with torpedo-owned state/functions.
  - cheap-tier torpedo `Fly` direction scoring uses:
      `score = (1 - core::math::ExpDecayA1K07(runCells)) * core::math::ExpDecayA1K07(enemiesInFirstCell) * core::math::ExpDecayA1K07(turnSteps)` (`turnSteps` 0 = straight, full weight; more turn steps decay the multiplier. Using `(1 - ExpDecayA1K09(turnSteps))` would invert that because `ExpDecayA1K09(0) = 0.9`.)
  - cheap-tier torpedo fly traversal uses continuous heading toward current segment target point (no 8-way heading snap during traversal); for 2-segment diagonals, segment index advances when the midpoint is reached and path renewal happens when entering the target maze cell on the final segment leg.
  - offscreen torpedo player-detection checks run at a lower frequency than full simulation.
- Assassin exception:
  - while cheap-tier, assassin segment selection is flow-field-driven and recomputed on cell transitions (or when no active segment exists).
  - if no flow build exists (for example while invisibility is on), cheap-tier assassins do not traverse cached segments; they stay idle until a valid flow build is available again.
  - cheap-tier segment build recovery uses staged methods based on consecutive build failures: stage `0` for fail count `0..2` (strict edge-constrained solve), stage `1` for `3..5` (relaxed edge tolerance and corner margin), stage `2` for `>=6` (deterministic emergency target solve).
  - stage `2` includes an embedded-wall escape: if assassin starts inside wall-avoidance space, it first picks the nearest local safe target (`current cell center`, `next cell center`, or emergency target) and allows one bounded short crossing (`<= 1.5 * cellSize`) only when start is inside wall and target is outside wall-avoidance space.
  - successful segment build resets cheap-tier segment fail counters and method stage to `0`.
  - each cheap-tier assassin segment must leave the current cell and continue `1` world-unit beyond that exit.
  - cheap-tier assassin segment exits are constrained to the flow-target edge with corner avoidance: edge crossing must be at least `1.0` world-unit from either corner.
  - if current flow direction matches previous flow direction, the segment is axis-aligned (`horizontal`/`vertical`) through the cell.
  - if flow direction turns by `90°` (`CW`/`CCW`), the segment uses a `45°` turn toward the destination-edge midpoint and continues `1` unit beyond.
  - segment validity is checked against wall blocking at segment-build time.
  - **Crowd anti-stacking:** when a cheap-tier assassin enters a maze cell, it checks `EnemyCellOccupancy` for any other enemy in that cell. If another enemy is present, the assassin switches to *slow mode* (velocity ×0.5); if no other enemy and slow mode was on, it turns slow mode off. This reduces cheap-tier assassin crowding in the same cell.
  - on `Cheap -> Full` transition, assassin flow-step cache is invalidated and resumed by regular full logic.

### Invisibility Mode

- When invisibility is enabled (I key during gameplay), enemies treat player as non-detectable:
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
- Current gameplay palette (hex): background `#000000`, walls `#CCCCCC`, player `#00C030`, drone `#A0FF00`, torpedo `#FFFF00`, hunter `#FFA500`, assassin `#FF6500`, enemy base shell `#CC66CC`, enemy base core `#FF00FF`, destroyed base `#404040`, player shell `#FFFFFF`, enemy shell `#FFB000`.
- Visible maze cell range is culled for rendering performance.
- Enemy and projectile rendering is culled to camera-visible world bounds with a small safety margin.
- Projectiles render as pixel-snapped `3x3` px rectangles in screen space (player shell `#FFFFFF`, enemy shell `#FFB000`).
- Enemy tanks and bases are rendered in pixel-snapped screen space (derived from world positions) to match wall stability on handheld displays.
- Base visuals use a `3x3` unit shell with a centered core disc. Each shell side (`top/right/bottom/left`) renders with independent thickness proportional to segment health (`4` = full original thickness, `0` = no shell on that side).
- Enemy tank visuals load from `resources/textures/sprites.png` (`2x7` grid, `9x9` cells). Rows `4..7` map to `Drone`, `Torpedo`, `Hunter`, `Assassin` (matching `docs/original-1982/ENEMY_TYPES.md` order). Column 1 is facing 12 o'clock, column 2 is 45 degrees clockwise; the renderer precomputes all 8 directions at load time and uses the matching directional frame at draw time. Non-transparent source pixels are normalized to white during load, then tinted by enemy type color at draw time.
- Player tank visuals load from `resources/textures/sprites.png` (`2x7` grid, `9x9` cells). Row `1` is body and row `2` is barrel; each direction frame is prebuilt by XOR-combining body+barrel cells and rendered in green (`#00C030`), with 8 directions precomputed from the two source columns.
- Enemy sprite rendering uses pixel-snapped screen-space placement derived from world positions with integer sprite scaling (`9x9` source cells rendered at `18x18`, i.e. exact `2x`). Player gameplay footprint remains `kEntitySizeUnits = 1.0`, and the player sprite is rendered in pixel-snapped screen space at fixed `18x18` with per-frame pivot correction to avoid heading-frame jitter.
- Compile-time presentation scaling: macOS builds use `2x` point-scaled presentation (logical `640x480` framebuffer upscaled to `1280x960` in the window). Handheld and other non-macOS builds keep `1x` (`640x480` window).
- Default logical resolution is `640x480` on all platforms; macOS matches the handheld visible maze area (`1 unit = 16 px`) at doubled pixel size.
- HUD direction radar draws three lines: hull heading (white), move joystick vector from gamepad axes `0/1` (sky blue), and fire joystick vector from gamepad axes `2/3` (red). Joystick direction uses `(axisX, axisY)` and amplitude is normalized by raw max magnitude `32768`.
- HUD lives indicators use the same sprite source and color as the in-world player tank sprite, rendered at `36x36` (4x of the `9x9` source cell).
- HUD lives indicators are left-aligned in the lives row; as lives decrease, icons disappear from the right.
- Gameplay view draws top-left debug text (axes/perf/profiling) only when menu `Debug info` is enabled.
- With `Debug info`, visible enemies also draw overlay diagnostics: **LOS** to player as an always-on **red** line whenever `enemy.seesPlayer` is true (no additional per-type range color/range gating in renderer); green line to current hunter scout waypoint or (for non-torpedo cheap-tier movement) cached segment end; yellow line to current torpedo `Fly` path waypoint when `torpedoFlyPathActive` (cheap- and full-tier); plus hard/avoidance radius circles and nearest-wall distance label.
- Flow-field guidance arrows are drawn at maze-cell centers for visible cells when a player flow-field build exists and menu `Debug info` is on.
- With `Debug info`, visible maze cells also draw `BaseDistanceField` values as cell-center numbers (unreachable cells are skipped).
- HUD draws an icon counter strip above the radar blocks at font size `10`: base rectangle icon plus enemy type sprites (`Drone/Torpedo/Hunter/Assassin`) with per-type alive counts, tinted by corresponding minimap colors.
- Debug-overlay text content is refreshed every `4` frames and cached between refreshes to reduce per-frame formatting/query overhead.
- HUD minimap uses a persistent `60x60` logical render texture (maze-cell aligned) and blits it scaled `2x` to `120x120` in the HUD. The minimap is horizontally centered in HUD content and uses matching vertical margins above/below. Enemy/base markers are single-pixel points in that logical texture; player marker is drawn dynamically on top.
- HUD runtime sampling cadence:
  - enemy/base minimap texture updates incrementally (`1` entity index per frame): enemy indices `0..N-1`, base indices `-6..-1`
  - minimap texture update erases previous cached cell coordinate (draw black), then draws current colored point and stores new cached cell coordinate
  - fuel bar value refreshes every `0.5s`
  - nearest-base radar uses a persistent texture layer; content is rebuilt every frame, then the layer is blitted every frame
  - joystick direction vectors on compass refresh every frame

### Runtime Profiling

- Profiling uses in-engine scope timers with a rolling `120`-frame window and per-scope `avg`, `p95`, `max`, and call counts.
- Instrumented fixed-step scopes include: frame total, fixed-step update, `Game::Update`, system updates (AI/player/projectile/collision/spawner/maze), enemy update, assassin pathfinding phases, and enemy separation/frontal-collision resolution.
- A periodic console report is emitted every `120` frames and sorted by average scope time, including fixed-step budget percentage and allocation snapshot deltas.
- Memory telemetry tracks global C++ allocations/deallocations (`new/delete`) with per-frame and per-scope deltas plus current live and peak bytes.
- macOS debug builds have a maze-click probe: left mouse click in gameplay maze area logs `[ENEMY_CLICK_DEBUG]`, then `[ENEMY_CLICK_DEBUG_ENEMY]`, `[ENEMY_CLICK_DEBUG_STATE]`, and `[ENEMY_CLICK_DEBUG_NAV]` lines for every alive enemy in the clicked cell (plus near-radius fallback), ending with `[ENEMY_CLICK_DEBUG_RESULT]`.

## Audio Events

- `resources/audio/power-up-digital.wav`: played when fueling start mode begins (new game, respawn after life loss, and level restart after all bases destroyed).
- `resources/audio/player-shot.wav`: played when player projectile count increases (player fires).
- `resources/audio/player-explosion.wav`: played when the player enters death mode (explosion at `deathExplosionPosition`: projectile hit, ram/collision, base collision, or fuel empty). Uses the same distance attenuation as other gameplay sounds.
- `resources/audio/enemy-shot.wav`: played when enemy projectile count increases (enemy fires).
- `resources/audio/enemy-spawning.wav`: played when alive enemy count increases (enemy spawned).
- `resources/audio/match-fire.wav`: played when alive enemy count decreases (enemy destroyed / explosion).
- `resources/audio/base-exploding.wav`: played when alive base count decreases (base destroyed).
- `resources/audio/metal-hit.wav`: played when any projectile’s motion segment is blocked by a wall (player or enemy shell). Uses the same distance attenuation as other gameplay sounds. Visual feedback reuses the enemy-death explosion (`explosion-1.png`, `kEnemyExplosionRenderSizePx`) at the segment midpoint.
- Distance attenuation for gameplay sounds:
  - If source distance `d > 10 * 6` world-units, sound is not played.
  - If `d <= 3 * 6`, full volume is used.
  - If `3 * 6 < d <= 10 * 6`, volume is `V = 1 - (d - r1) / (r2 - r1)` with `r1 = 3 * 6`, `r2 = 10 * 6`.
- Main menu background music is generated procedurally at runtime by a bytebeat-style synthesizer module (`src/app/MenuMusicGenerator.cpp`), not loaded from an audio asset file.
- Procedural menu music is enabled only while `GameMode::Menu` is active and is paused immediately when transitioning to gameplay.
- Gameplay procedural melody (`ResonantBeatMelody` via `MenuMusicPlayer`) has a dynamic tension gate: during `GameMode::Playing`, if any alive enemy has visual contact (`enemy.seesPlayer`), a music-contact timer is set to `4.0s`; when no enemies see the player, the timer counts down by fixed-step `dt`. Gameplay melody runs in `tense` mode while this timer is `> 0`.
- `resources/audio/ui-pass.wav`: played for menu and confirmation feedback that is not a menu Select/Enter on a button: moving focus (Up/Down on the main menu, Left/Right between dialog buttons), changing level/density/debug, mouse clicks on controls, and Escape to cancel a dialog.
- `resources/audio/positive-select.wav`: played when menu Select/Enter activates **Start** or **Quit** on the main menu, or **Confirm** / **Cancel** on a confirmation dialog (quit confirmation, gameplay pause), including when that choice exits the game or starts play.

## Main Menu UX

Menu rendering is in `src/ui/MenuScreen.cpp`.

- No full-screen menu panel or dimmed dialog plate: controls sit directly on the menu background. Quit confirmation has no rounded panel behind the text (buttons and message only). The level slider uses an outer focus band **32** px tall; `GuiSliderBar` is given a **28** px-tall bounds rectangle inset **2** px from the top and bottom of that band (raygui is not patched). The control uses a **2** px border in `#0D8152` (same packed color as menu focusable text), **2** px top/bottom `SLIDER_PADDING`, and an explicit post-draw left/right mask in `MenuScreen.cpp` so the visible border-to-fill gap is also **2** px on both sides. Slider width is **75%** of the menu gauge (centered), and the focus ring keeps **2** px clear space outside the outer band (`DrawFocusRing` expand derived from raylib line geometry in `MenuScreen.cpp`). `Density`, `Debug info`, `Start`, and `Quit` label text uses `#0D8152` (matching packed raygui text color). `Start` and `Quit` have no button border or fill until keyboard focus (focus ring only); the quit confirmation dialog switches to bordered buttons for readability.
- Includes:
  - `Bolt` wordmark with `Beta Version` (left) and `Build #<n>` (right) on the line below the wordmark
  - **Start** `GuiButton` row first: at least **30** px below the old menu-section anchor (`betaLabelY + kLevelBlockOffsetFromBeta`), but if that would overlap **Quit**, vertical positions are derived from the fixed **Quit** row so the stack still fits (`std::max` in `MenuScreen.cpp`). **Quit** stays at the bottom (`screenHeight - 38 - 20`). Base spacing still uses double/single gaps (`56` / `28` px), then row tuning offsets are applied: `Level` shifts up **20** px, `Density` up **40** px, and `Debug info` up **60** px (relative to the original stacked positions). `GuiButton(Start)` runs before Level/Density drawing so the button stacks correctly when **Start** is above those rows.
  - level (1..9): centered `#0D8152` `Level` + digit in a `kMenuQuitButtonHeight` row (same layout as `Density`; the number column keeps the width of the `8` glyph). The raygui level slider is **not** shown initially; it replaces that text row for **1** second after Left/Right while Level is focused (timer resets on repeat L/R or while interacting with the slider). Defocusing Level does **not** clear that timer; the text row returns when the timer expires (quit dialog clears it). While the text row is shown, the Quit-sized unified focus frame rings the row; while the slider is shown and Level is focused, the focus ring tracks the slider bounds with the expanded margin above.
  - density: centered `#0D8152` `Density` + digit (same layout as `Level`) in a `kMenuQuitButtonHeight` row; unified-width focus frame like Quit; hatch sprites occupy the same row when previewing
  - debug info line (`Debug info: On` / `Off`), centered
  - **Quit** `GuiButton` row (unchanged bottom position)
- The `Bolt` wordmark is centered horizontally with its top edge `30` px below the top of the viewport, in `rgb(224, 206, 4)`, using the bitmap atlas `resources/fonts/absolute_10.png` plus metadata `resources/fonts/absolute_10.txt`; glyphs use point filtering (anti-aliasing disabled) at 128px screen size (8x). If that font fails to load, fallback uses the default font at `20` px.
- `Beta Version` is aligned to the left edge of `Bolt`, directly below it, in gray using `resources/fonts/pixuf.ttf` at 16px (Pixuf native size) with point filtering; if that font fails, fallback default font at `20` px. On the same horizontal line, `Build #<n>` uses the same font and color; its right edge aligns with the right edge of `Bolt`.
- Level slider reveal and density hatch preview both use a **1** s wall-clock window (`kMenuRevealRowDurationSeconds` in `MenuScreen.cpp`, backed by `levelSliderRevealUntilTime_` and `densitySpritesRevealUntilTime_`).
- Five `16x16` hatch sprites are loaded from `resources/textures/density-hatch.png` as a single row (`80x16`) or column (`16x80`) strip; every pixel with non-zero alpha is recolored to `#0D8152` (transparency preserved). They are drawn centered on the same horizontal band as the `Density` text (vertically centered in `kMenuQuitButtonHeight` from `densityLabelY`), at `2x` scale (`32x32` on screen) with `16` px gaps, only while `GetTime() < densitySpritesRevealUntilTime_`. Defocusing Density does **not** clear that timer; text returns only when the timer expires (quit dialog still clears it). If the texture is missing, placeholder tiles are used during that same window. When sprites are hidden, the Quit-sized frame rings the row; when shown and Density is focused (or a sprite is hovered), per-sprite rings apply.
- While the menu is visible, a low-volume generated 8-bit/chiptune loop plays in the background.
- Quit opens confirmation dialog.
- Keyboard/mouse focus selection frames use `#A28905` (`DrawFocusRing` in `src/ui/UiPrimitives.cpp`, also used for quit confirmation button focus). Level uses the same unified frame as `Debug`/`Start`/`Quit` while the text row is shown; during slider preview, the ring tracks the slider. Density uses the same unified frame as `Debug`/`Start`/`Quit` while the text row is shown; during sprite preview, per-sprite rings when Density is focused or a sprite is hovered. For `Debug info`, `Start`, and `Quit`, the focus rectangle shares one size: width = `MeasureText("Debug info: Off", 20) + 40` px, height = `kMenuQuitButtonHeight` (same as the Start/Quit `GuiButton` row height in `MenuScreen.cpp`), centered in the viewport on each row (button hit areas stay full-width for raygui). The debug line text is drawn with the same vertical centering as raygui uses for button labels: `debugInfoY + (kMenuQuitButtonHeight - 20) / 2`.

## Build Number

Build number is shown on the main menu subtitle row as `Build #<n>` (right-aligned under the `Bolt` wordmark), same styling as `Beta Version`.

- Current source API: `src/app/BuildInfo.h`
- Generated define: `BuildNumber.h`
- Shared counter file: `build/build_number.txt`
- Increment occurs automatically on each CMake build.

## Known Current Gaps

The following systems are placeholders or minimal:

- original BOLT combat and fuel/lives progression details

When implementing these features, update this document so it remains the single source of truth.
