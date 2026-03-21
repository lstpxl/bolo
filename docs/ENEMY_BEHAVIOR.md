# Enemy Behavior (Code-Verified)

This document describes the current enemy runtime behavior as implemented in code.

- Runtime truth is taken from `src/game/systems/EnemySystem.cpp` and related enemy/system modules.
- This file is intentionally code-first and may differ from older prose in `docs/GAME_DESIGN.md`.
- Scope: AI processing stages, supporting systems, shared behavior, and enemy-specific behavior.

## 1) Enemy Processing Stages

Enemy logic runs inside the fixed-step `Game::Update(...)` pipeline in this order:

1. `UpdateEnemySystem(...)`
2. `UpdatePlayerSystem(...)`
3. `UpdateProjectileSystem(...)`
4. `UpdateCollisionSystem(...)`
5. `UpdateSpawnerSystem(...)`
6. `UpdateMazeSystem(...)`

Within `UpdateEnemySystem(...)`, processing is staged as follows.

### Stage 1 - Cache Prep + Tier Selection

For each frame:

- Build/refresh enemy occupancy caches:
  - `navigationCache.enemyCellOccupancy` (cell occupancy checks)
  - `navigationCache.enemyRayQueryOccupancy` (ray + enemy-aware clearance queries)
- Update player cell in `CellCoordCache`.
- Update flow-field cache with `PlayerFlowField::Update(...)` when flow guidance is enabled.
- Precompute uncouple priority scores via `ComputeUncoupleEscapeScore(...)`.
- Determine simulation tier per enemy with `DetermineEnemySimTier(...)`:
  - `Full` if enemy is within 3 cells of player (`Chebyshev` cell distance).
  - `Full` for torpedoes in any non-`Fly` mode.
  - `Cheap` otherwise.
- On `Cheap -> Full`, reset cheap-tier cached movement state, especially assassin segment/fail caches.

### Stage 2 - Perception

`RunPerceptionPhase(...)` performs shared perception:

- Update self-awareness timer:
  - Drone interval random `5..8` seconds; re-rolled after each expiry; drone self-awareness action (`TryDroneSelfAwarenessReset` → optional `DroneReset`) runs on expiry in **both** full tier (here) and cheap tier (`AdvanceCheapTierTimers`).
  - Other enemy types random `4..8` seconds
- Compute vector and distance to player.
- Compute obscurity:
  - `playerInvisible || IsSegmentObscuredByWall(...)`
- Set `enemy.seesPlayer = player.alive && !playerObscured`.
- Set assassin LOS speed gate flag:
  - `assassinHasLineOfSight = (assassin && in aggro range && !obscured)`

### Stage 3 - Decision / Mode Transitions

Per full-tier enemy:

- If currently in `Uncouple`, run uncouple heading selection first (`SelectUncoupleHeading(...)`).
- Else branch by type:
  - Drone: `Wander`/`Watch` logic, pursuit trigger, base-return and watch escape logic.
  - Torpedo: `Fly`/`Ram`/`Retreat`/`Targeting`/`Rotate` state machine.
  - Hunter: `Scout`/`Chase`/`Rotate` selection and transitions.
  - Assassin: `Pursuit` with flow-field-first steering (A* backup branch disabled by flags).

### Stage 4 - Movement Execution

#### Full Tier

- Movement heading is generally quantized to 8 directions.
- Continuous heading is preserved for rotating/slewing modes (watch rotation, torpedo ram/rotate).
- Candidate move is evaluated with:
  - Separation/overlap pre-checks against other enemies
  - Optional separation turn attempt (`TrySeparationTurn(...)`)
  - Wall/edge-on contact checks
- On wall contact:
  - Torpedo can die on hard wall impact while moving.
  - Others enter `Uncouple` with recovery reposition (`ResolveWallContactRecoveryPosition(...)`).
- On successful move, update velocity/position/heading and occupancy cell.

#### Cheap Tier

- Shared timer updates run in `AdvanceCheapTierTimers(...)` (self-awareness timer matches perception; drones call `TryDroneSelfAwarenessReset` when the timer expires).
- Movement runs in `ApplyCheapTierMovement(...)`.
- Cheap-tier behavior is type-specialized:
  - Drone/Torpedo: offscreen segment movement with sparse recompute.
  - Hunter Scout: reuses scout planner path selection.
  - Assassin: flow-driven cheap segments with staged recovery and anti-stacking slow mode.
- Cheap tier does not run the full enemy-enemy collision post-pass.

### Stage 5 - Firing

`RunFiringPhase(...)` is shared and requires all gates:

- Player alive
- Cooldown <= 0
- Enemy inside player viewport
- Player not obscured (`!playerObscured`)
- Type-specific cone gate passes (torpedo stricter in `Ram`)
- Distance to player is within that type’s **detect range** (same radii as AI / debug LOS: Drone `kDroneDetectRangeUnits`, Hunter `kHunterDetectRangeUnits`, Assassin `kEnemyAggroRangeUnits`, Torpedo `kTorpedoDetectRangeUnits` or `kTorpedoRamDetectRangeUnits` in `Ram`)

When fired:

- Projectile heading is quantized to 8 directions toward player.
- Projectile is spawned by `SpawnProjectile(...)`.
- Cooldown resets by `EnemyFireInterval(type)`.

### Stage 6 - Enemy Pair Post-Pass (Full Tier Only)

After per-enemy movement, full-tier enemies run one collision/separation post-pass:

- Broad phase via `SweepPruneBroadPhase`.
- Narrow phase in `ResolveEnemyCollisionsSinglePass(...)`:
  - Frontal-overlap threshold (`kEnemyMutualKillDistanceUnits`) enters `Uncouple` for both.
  - Near-separation threshold (`kEnemyPreferredSeparationUnits`) can also enter `Uncouple`.
  - Base-interior pairs are skipped from these checks.
- Separation re-entry while both are already in `Uncouple` is gated by stricter distance (`<= 1.5` units).

## 2) Supporting Systems Used by Enemy Logic

### Flow-Field Lifecycle and Caching

`PlayerFlowField` provides BFS next-cell guidance toward player cell:

- Rebuild uses maze traversability and treats undestroyed base cells as blocked.
- Rebuild can run async via `FlowRebuildWorker`.
- Cache patching occurs on player cell transitions (`OverrideNextCellHash(prev, curr)`).
- Rebuild cadence is age-based: schedule when player-cell-change age exceeds 2.
- `Game::Update` toggles cache active state on invisibility toggle:
  - Invisibility ON: cache deactivated and invalidated.
  - Invisibility OFF: cache activated and invalidated (forces rebuild path).

### Spatial Caches

- `EnemyCellOccupancy`:
  - local occupancy checks (including assassin cheap-tier crowding/slow-mode)
  - fast "other enemy in cell" queries
- `EnemyRayQueryOccupancy`:
  - used by `FreeDistanceAheadWithEnemies(...)` for enemy-aware clearance
- `SweepPruneBroadPhase`:
  - full-tier post-pass candidate generation for pair checks

### Cheap-Tier Segment Caching and Recovery

- Cheap tier stores movement segments (`offscreenSegmentEnd`, cached heading).
- Rebuild occurs on segment completion or specific transitions.
- Assassin cheap-tier segment build includes staged methods:
  - Stage 0 (strict)
  - Stage 1 (relaxed tolerance/corner margin)
  - Stage 2 (deterministic emergency fallback with bounded embedded-wall escape)
- Assassin cheap-tier no-flow case explicitly idles and clears active segment.

### Invisibility Interactions

- Perception path marks player obscured while invisibility is enabled.
- Shared firing phase blocks enemy shots when player is obscured.
- Flow cache is deactivated while invisibility is enabled.

## 3) Shared Behavior Across Enemy Types

### Shared Movement and Geometry Contracts

- Headings are 8-direction quantized in most movement decisions.
- Clearances use geometry queries from `game/geometry/WorldGeometry.*`.
- Separation target is based on `kEnemyPreferredSeparationUnits`.
- Full-tier wall contact can push enemies into shared `Uncouple` recovery.

### Shared Uncouple Mode

`Uncouple` is available for all enemy types:

- Entered from frontal pair collisions, separation-proximity pairs, or self wall-contact.
- Stores pre-uncouple mode, restores on timeout.
- Re-entry does not reset timer for pair-based re-entry.
- Heading refresh on re-entry is limited to non-uncouple entries or wall-contact re-entry.
- Steering combines path-following, separation, obstacle avoidance, wall avoidance, and random jitter.

### Shared Perception and Shooting

- One shared perception phase (`RunPerceptionPhase(...)`) for all types.
- One shared firing phase (`RunFiringPhase(...)`) for all types.
- Torpedo only adds a type-specific cone gate during firing.

### Shared Speed Framework

- Base speed by enemy type + subtype multiplier.
- Subtypes:
  - `Basic`: `0.75x`
  - `Advanced`: `1.0x`
  - `Lord`: `1.25x` (Hunter only)
- Assassin base speed in runtime path is LOS-conditioned (`1.5` or `3.0`), then subtype multiplier.
- Level 9 applies additional `4x` multiplier for assassin speed (debug behavior).

## 4) Enemy-Specific Behavior

### Drone

Modes: `Wander`, `Watch`

- Pursuit trigger: sees player and distance `<= kDroneDetectRangeUnits` (`12`).
- Pursuit speed: normal drone speed scaled by `kDronePursuitSpeedFactor` (`0.5`).
- Pursuit heading:
  - choose nearest valid 8-way heading toward player
  - require wall clearance for step
  - reject steps that get within `kDronePlayerAvoidanceDistanceUnits` (`4`) of player
  - reject steps violating preferred separation with other enemies
- Wander fallback uses scout-style heading fallback; if blocked, enters `Watch`.
- Watch:
  - speed zero
  - **Alignment Watch** (`droneWatchAlignToHeading`): after `DroneReset` on full tier, rotate toward the scored target heading at the same rate as normal Watch (full turn over `kSlowRotateFullTurnSeconds`); when aligned, return to `Wander`.
  - **Normal Watch** otherwise: continuous rotation (full turn over `kSlowRotateFullTurnSeconds`) in a random spin direction chosen at Watch entry
  - after a full turn (normal Watch only), either:
    - attempt weighted return-to-base heading with required clear run >= 6 units, or
    - attempt escape heading that improves spacing if clear run is sufficient
- **`DroneReset`:** score-weighted random direction (8-way), hunter-style factors: maze run length, enemies in first cell along the ray, `core::math::ExpDecayA1K02(turnSteps)` from current heading to candidate, `core::math::ExpDecayA1K07(flowTurnSteps)` vs `BaseFlowField` next cardinal step toward base. Cheap tier: set heading and `Wander`. Full tier: `Watch` until aligned to target heading, then `Wander`.
- Cheap-tier segment build failure (max segment length `< 2` after safety margin): **`DroneReset`** instead of a random heading.
- Self-awareness restart when far from base and bearing `≥80°` to base-flow step: **`DroneReset`** (replaces unconditional Watch entry).

### Torpedo

Modes: `Fly`, `Ram`, `Retreat`, `Targeting`, `Rotate`

- Detection cadence: throttled every `0.25` seconds.
- Ram trigger:
  - sees player and distance `<= kTorpedoRamDetectRangeUnits` (`12`)
  - exits Ram when no longer detected
- Full-tier steering turn cap:
  - `Ram` turns toward the player; `Fly` with an active fly path turns toward the current segment waypoint bearing (same builder as cheap-tier); `Fly` fallback probe and `Retreat`/uncouple use an 8-way target; `Rotate` turns toward the chosen heading — all at most `kTorpedoFullTierTurnSpeedRadiansPerSecond` (`45 deg/s`)
- Ram steering:
  - turn toward player at the full-tier turn rate above
  - forward speed integrated with heading-based acceleration; clamped to `kEnemyTorpedoSpeedMax * EnemySubtypeSpeedMultiplier` (`2×` baseline torpedo speed)
- Fly movement (**full tier**): primary navigation matches cheap-tier — `EnsureTorpedoFlyPath` / cell scoring / **`AdjacentCellSegmentPlanner::Build`**, then `SelectTorpedoFlyMotion` (continuous bearing to waypoint); hull slews toward that bearing at the full-tier turn rate; speed ramps toward scaled cruise with full-tier accel cap. Segment geometry rules: see **`AdjacentCellSegmentPlanner`** in `docs/GAME_DESIGN.md`. **Segment renewal:** when `WorldToCell(position)` matches the target maze cell for the active path and torpedo is on the final segment leg, the path is invalidated and replanned, replacing the old distance-to-waypoint completion rule. **Fallback** when path cannot be built or followed: same three-way probe as before (`forward` / ±45°, clearance to 15), max-clear pick, **no** random straight-hold segment length; boxed-in or invalid window → `Retreat`.
- Fly movement (**cheap tier**): same segment builder and target-cell renewal semantics; fixed cruise speed; continuous heading toward segment target (including midpoint-to-target progression for 2-segment diagonal paths).
- Spawn/base exit behavior:
  - while inside base footprint + 1.0 clearance, keep straight heading lock
- Retreat:
  - reverse at 10% target speed
  - after retreat distance + forward-clear conditions, enter `Targeting`
- Targeting/Rotate:
  - choose best long straight heading
  - rotate to it then return to `Fly`
- Speed / acceleration:
  - **Cheap tier:** constant `kEnemyTorpedoSpeed * EnemySubtypeSpeedMultiplier` (`Basic` = `0.75`), no ramp.
  - **Full tier:** `kTorpedoFullTierAccelMaxUnitsPerSecondSq * EnemySubtypeSpeedMultiplier` caps per-step speed change (`Ram` heading-based accel; `Fly` cruise ramp; `Retreat`/uncouple track `targetSpeed`).
- Wall contact (full tier):
  - **Hard** segment hit (wall pill matching `kEnemyWallHardCollisionUnits`, via `SegmentIntersectsWall` with clearance `kEnemyWallHardCollisionUnits - kWallHalfThicknessUnits`, same as `kWallClearanceForHard`) while moving (speed magnitude above epsilon): destroy torpedo; separation/overlap logic is skipped when the **intended** step already has that hard hit so speed is not zeroed before the wall check
  - **Avoidance** violation (segment reaches `kEnemyWallAvoidanceUnits` / `kWallClearanceForAvoidance`, or edge-on-wall contact) without hard death: apply **`candidatePosition`**, brake signed hull speed toward `0` using full `kTorpedoFullTierAccelMaxUnitsPerSecondSq * EnemySubtypeSpeedMultiplier` per step — **no** `ResolveWallContactRecoveryPosition` and **no** uncouple
- Firing gate:
  - shared gates plus forward cone:
    - normal torpedo: approx +/-30 deg
    - in Ram: stricter approx +/-20 deg

### Hunter

Modes: `Scout`, `Chase`, `Rotate`

- Chase trigger: sees player and distance `<= kHunterDetectRangeUnits` (`12`).
- Chase behavior:
  - if distance < `kHunterMinDistanceUnits` (`3`): back away
  - if distance > `kHunterMaxDistanceUnits` (`6`): move toward player
  - else hold (`speed = 0`)
- Scout pathing:
  - two-level planning in `SelectHunterScoutMotion(...)`
  - direction scoring over 8 directions based on:
    - directional run length
    - first-cell enemy count
    - turn cost from current heading
  - uses exponential decay weighting (`core::math::ExpDecayA1K07`)
  - base-occupied cells are blocked
  - builds 1-2 segment path to target cell center (or adjusted endpoint)
- Rotate fallback:
  - if scout path fails, rotate until enough clear run exists, then return to scout
- Cheap tier:
  - scout path logic is reused; traversal heading can remain continuous for path fidelity

### Assassin

Modes: `Pursuit`, `Uncouple`

- Runtime navigation flags in `EnemySystem.cpp` are:
  - `kUseFlowFieldPathGuidance = true`
  - `kUseAssassinFlowFieldOnlyNavigation = true`
  - `kUseAssassinAStarBackupNavigation = false`
- Full-tier pursuit:
  - if player visible and too close (`distance < kAssassinMinDistanceUnits`), stop and clear path/flow caches
  - otherwise try flow step (`TrySelectAssassinFlowNextStep(...)`)
  - if flow step unavailable, fall back to quantized heading toward predicted player position
- Flow-step caching:
  - cache heading per source cell (`cachedFlowFromCellHash`)
  - reuse cached heading until leaving cell
- Cheap-tier pursuit:
  - flow-driven segment building (`BuildAssassinCheapFlowSegment(...)`)
  - staged build recovery and emergency fallback
  - no-flow case idles (no stale drift)
  - crowd anti-stacking: entering crowded cell enables 0.5x speed mode
- On uncouple or Cheap->Full transitions, assassin cheap-tier movement caches are reset.

## 5) Explicit Discrepancies vs `docs/GAME_DESIGN.md`

The list below is intentionally explicit: each item states what `GAME_DESIGN.md` says and what the current code does.

1. Full/Cheap sim-tier boundary
   - `GAME_DESIGN.md`: full-tier radius is described with a viewport-based distance formula.
   - Code: `DetermineEnemySimTier(...)` uses a fixed 3-cell Chebyshev distance from player cell; torpedoes are forced full-tier in non-`Fly` modes.

2. Assassin behavior while invisibility is enabled
   - `GAME_DESIGN.md`: assassin target becomes a random maze point (repicked on reach).
   - Code: runtime is configured to flow-field-only assassin navigation (`kUseAssassinFlowFieldOnlyNavigation = true`, A* backup disabled). With invisibility on, flow cache is deactivated/invalidated; assassin falls back to non-flow heading logic in full tier and idles on no-flow in cheap tier.

3. Torpedo firing cone wording
   - `GAME_DESIGN.md`: prose presents `+/-20 deg` and `+/-30 deg` constraints in a way that reads as overlapping.
   - Code: exactly one cone gate is used by mode:
     - `Ram`: `PlayerAheadForTorpedoRam(...)` (`~+/-20 deg`)
     - non-`Ram`: `PlayerAheadForTorpedo(...)` (`~+/-30 deg`)

4. Aggro-range terminology
   - `GAME_DESIGN.md`: enemy detection/aggro wording is often centered around 12-unit per-type detect thresholds.
   - Code: shared aggro constant `kEnemyAggroRangeUnits = 16` exists and is used for assassin LOS-speed gating, while per-type transitions still use dedicated detect ranges (for example drone/hunter/torpedo 12).

5. Hunter relation to player flow-field
   - `GAME_DESIGN.md`: flow-field activation text groups hunters and assassins as flow consumers.
   - Code: hunter decision/movement logic is scout/chase/rotate based and does not read flow-field headings for steering; flow-field remains primarily an assassin steering dependency.

6. Cheap-tier collision wording
   - `GAME_DESIGN.md`: cheap-tier text can be read as broadly skipping collision behavior.
   - Code: cheap-tier still does obstacle/validity checks during segment selection and type-specific safeguards, but it skips the full-tier enemy pair post-pass and full per-step overlap/wall handling used in full-tier movement.

## 6) Source Map

Primary implementation files:

- Pipeline: `src/game/Game.cpp`, `src/game/systems/EnemySystem.cpp`
- Shared phases: `src/game/systems/EnemySystemCombatPhase.cpp`
- Cheap-tier: `src/game/systems/EnemySystemCheapTier.cpp`
- Uncouple: `src/game/systems/EnemySystemUncouple.cpp`
- Pair post-pass: `src/game/systems/EnemySystemCollision.cpp`
- Drone: `src/game/systems/EnemyDrone.cpp`
- Torpedo: `src/game/systems/EnemyTorpedo.cpp`
- Hunter: `src/game/systems/EnemyHunter.cpp`
- Assassin: `src/game/systems/EnemyAssassin.cpp`
- Flow field: `src/game/navigation/PlayerFlowField.cpp`
- Enemy states/constants: `src/game/model/EntityTypes.h`, `src/game/model/GameplayConstants.h`

## 7) How to Maintain This Doc

Use this short checklist whenever enemy behavior changes:

- Confirm runtime flags first (`EnemySystem.cpp`): flow-field toggle, assassin navigation mode, A* backup toggle.
- Re-verify stage ordering in `Game.cpp` and `UpdateEnemySystem(...)` before editing stage descriptions.
- For any behavior change, update both:
  - the relevant enemy-specific subsection (`Drone`/`Torpedo`/`Hunter`/`Assassin`)
  - the shared/systems section if the change affects perception, firing, tiering, uncouple, flow, or cheap-tier.
- Re-check boundary operators and constants (`<` vs `<=`, detect/fire ranges, cone thresholds) against `GameplayConstants.h` and call sites.
- Keep Section 5 current: add/remove discrepancy items whenever `GAME_DESIGN.md` and runtime behavior diverge or are brought back into sync.
