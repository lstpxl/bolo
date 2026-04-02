# Extensive Code Review — Final Report

## Baseline Invariants (confirmed at review start)

All core contracts hold as documented: fixed-step dt = 1/60, no mid-frame heap allocations (rule enforced, see violations below), `IRenderer` boundary (partially violated, see below), simulation update order Input → AI → Movement → Collision → Combat → Spawning, 60×60 cell maze at 6 units/cell, performance budgets from `PERFORMANCE_OPTIMIZATION_STRATEGY.md`.

---

## Findings — Severity Sorted

### 🔴 HIGH

---

**H-1 · Hot-path vector allocations in `UpdateEnemySystem` every fixed step**

- **Files:** `src/game/systems/EnemySystem.cpp` lines 478–482, 530–531, 1442
- **Evidence:**
```cpp
std::vector<Vec2f> frameStartPositions{};      // L478 – allocated & filled every step
std::vector<std::uint8_t> reenteredFullTierMask(...);  // L530
std::vector<float> uncoupleEscapeScores(...);  // L531
std::vector<std::uint8_t> fullTierMask(...);   // L1442
```
- **Impact:** Violates `ARCHITECTURE.md` "Never allocate memory in the mid-game hot path." Each fixed step (60/s) triggers 4+ heap allocations proportional to enemy count. At 144 enemies, profiling already shows `enemy.movement.overlap_check` at ~1.3 ms and `separation_probe` at ~0.5 ms; the alloc pressure is a contributing factor.
- **Recommended fix:** Pre-allocate all four as member vectors or static fixed-size arrays (max enemy count is `GameplayConstants::kMaxAliveEnemies = 999`; in practice ≤144). Reserve once at init, resize/clear each step.

---

**H-2 · `SweepPruneBroadPhase::ForEachCandidatePair` allocates O(n²) per call**

- **File:** `src/game/spatial/SweepPruneBroadPhase.cpp` line 140
- **Evidence:**
```cpp
std::vector<bool> pairVisited(static_cast<std::size_t>(maxId_ * maxId_), false);
```
With 144 enemies: 144² = 20 736 `bool` bits (~2.6 KB), allocated every fixed step that runs the broad phase. `std::vector<bool>` still requires a heap allocation.
- **Impact:** Hot-path allocation inside `ResolveEnemyCollisionsSinglePass`, called every step when `fullTierCount >= 2`. Profile 35 shows `enemy.physics.frontal_collisions` + `frontal_grid_build` at ~0.5–0.7 ms each.
- **Recommended fix:** Promote `pairVisited` to a member `std::vector<bool>` or a bitset member of `SweepPruneBroadPhase` and clear it via `std::fill` at the start of `ForEachCandidatePair`. Alternatively use a generation counter to avoid the fill.

---

### 🟠 MEDIUM

---

**M-1 · Chain-killed enemies produce no explosion VFX (ordering bug in `RunPlayingWorldTick`)**

- **File:** `src/game/Game.cpp` lines 398–462
- **Evidence:** Explosion slot spawn (lines 398–426) iterates `world.enemies` checking `!enemy.alive`. This runs **before** `ApplyExplosionBlast` (lines 444–462). Enemies killed by blast in the same tick (chain reactions) are dead but missed by the spawn loop. They are erased at lines 508–513 before the next tick, so the explosion slot is never created.
- **Impact:** Contradicts `GAME_DESIGN.md`: "Chain reactions: a full-tier enemy killed by blast spawns its own explosion that contributes overlapping blast intervals." The blast damage (secondary kill) IS applied correctly; only the visual explosion is missing.
- **Recommended fix:** Move the explosion-slot spawn loop for enemies to **after** all blast passes (after line 479), or spawn a slot directly inside `ApplyExplosionBlast` when an enemy is killed there.

---

**M-2 · `IsWallDistributionValid` referenced in `GAME_DESIGN.md` is not implemented**

- **File:** `src/game/systems/MazeSystem.cpp` line 441–445
- **Evidence:** The do-while validation loop only checks `IsMazeFullyAccessible` and `IsMazeWallTopologyValid`. `GAME_DESIGN.md` also requires `IsWallDistributionValid` (both horizontal and vertical walls present across all quadrants). Neither the function nor its call exists anywhere in the codebase.
- **Impact:** Mazes may occasionally have poor structural quality (all openings in one area/quadrant), which could degrade gameplay variety and navigation fairness. The check is listed as a hard acceptance criterion in the design doc.
- **Recommended fix:** Either implement `IsWallDistributionValid` and add it to the validation loop, or remove the requirement from `GAME_DESIGN.md` if intentionally dropped.

---

**M-3 · Undocumented firing gate: off-screen enemies never fire**

- **File:** `src/game/systems/EnemySystemCombatPhase.cpp` lines 139, 148–154
- **Evidence:**
```cpp
const bool enemyVisibleInViewport = IsInPlayerViewport(enemy.position, state, view);
// ...
if (state.world.player.alive && enemy.seesPlayer && enemy.fireCooldownSeconds <= 0.0F &&
    enemyVisibleInViewport && !perception.playerObscured && ...
```
`GAME_DESIGN.md` states: "Enemy projectile firing is gated by `enemy.seesPlayer` … plus per-type firing constraints." There is no mention of `enemyVisibleInViewport` as a gating condition.
- **Impact:** Enemies just outside the viewport cannot fire even if they theoretically see the player. This is likely an intentional fairness rule, but the design doc is silent on it. If removed accidentally, projectiles would appear from nowhere.
- **Recommended fix:** Document this rule explicitly in `GAME_DESIGN.md`: "Enemy projectile firing additionally requires the enemy to be within the viewport bounds."

---

**M-4 · Drone spawn self-awareness interval is wrong (6..12 s instead of documented 5..8 s)**

- **File:** `src/game/systems/SpawnerSystem.cpp` lines 255–257
- **Evidence:**
```cpp
const float selfAwarenessInterval = (spawnedEnemy.type == EnemyType::Drone)
    ? random.NextFloat(6.0F, 12.0F)   // <-- actual spawn value
    : random.NextFloat(4.0F, 8.0F);
```
`GAME_DESIGN.md` says: "Drone: random in `5..8` seconds (`kDroneSelfAwarenessIntervalMinSeconds` / `MaxSeconds`)." The constants exist but are not used at spawn. Subsequent resets in `RunPerceptionPhase` correctly use 5..8.
- **Impact:** The first self-awareness interval for every drone is 6..12 s instead of 5..8. Drones are sluggish/confused for longer on first spawn. Not gamebreaking, but a doc/code disagreement.
- **Recommended fix:** Replace `random.NextFloat(6.0F, 12.0F)` with `random.NextFloat(GameplayConstants::kDroneSelfAwarenessIntervalMinSeconds, GameplayConstants::kDroneSelfAwarenessIntervalMaxSeconds)`.

---

**M-5 · `DecrementOriginBaseAliveCount` duplicated across `Game.cpp` and `CollisionSystem.cpp`**

- **Files:** `src/game/Game.cpp` lines 30–39, `src/game/systems/CollisionSystem.cpp` lines 56–64
- **Evidence:** Both anonymous-namespace implementations are byte-for-byte identical.
- **Impact:** DRY violation; if base-count bookkeeping needs adjusting (e.g., bounds-check change), both copies must be updated.
- **Recommended fix:** Move to `EnemySystemHelpers.h/.cpp` or a shared game-utility header and remove the duplicates.

---

**M-6 · `Renderer2D::LoadResources` leaks intermediate CPU image data**

- **File:** `src/platform/Renderer2D.cpp` lines 540–586
- **Evidence:** `playerBodyUp`, `playerBody45`, `playerBarrelUp`, `playerBarrel45`, `playerFrame0`..`playerFrame7`, and `playerSheet` are allocated via `ExtractSpriteCell`, `CombineCellsXor`, `ImageCopy`, `GenImageColor` but never passed to `UnloadImage` after GPU upload. Compare the enemy path (lines 641–648) which does call `UnloadImage` for all intermediaries.
- **Impact:** ~15–20 small CPU images (~2–5 KB total) leaked at startup. No runtime consequence since it happens once, but it's inconsistent with the enemy path and would trigger under sanitizers.
- **Recommended fix:** Add `UnloadImage` calls for all player intermediate images and `playerSheet` after GPU upload, matching the pattern already used for enemy sprite frames.

---

**M-7 · Fixed-step accumulator cap (8×) mismatches consume cap (4×) — undocumented**

- **Files:** `src/core/Time.cpp` line 11, `src/app/GameApp.cpp` line 160
- **Evidence:** `Accumulate` clamps accumulator to `stepSeconds * 8.0F`. `kMaxFixedStepsPerFrame = 4` only consumes 4 steps per frame. After a 4-step spike, 4 pending steps queue up and take 2 full frames to drain, causing slow-motion catch-up.
- **Impact:** Not a bug per se (the clamping is deliberate defence-in-depth), but the 4 vs. 8 relationship is undocumented. If `kMaxFixedStepsPerFrame` is raised to 8, the cap would need matching.
- **Recommended fix:** Document the intent: "accumulator cap is 2× the per-frame step cap so a back-to-back spike pair drains in 2 frames, not indefinitely." Or align the cap: `stepSeconds_ * static_cast<float>(kMaxFixedStepsPerFrame)`.

---

### 🟡 LOW

---

**L-1 · `AppConfig::fixedDeltaSeconds` is never consumed by the loop**

- **Files:** `src/app/AppConfig.cpp` line 8, `src/app/GameApp.h`
- **Evidence:** `fixedDeltaSeconds = 1.0F / 60.0F` is set but `FixedStepTimer` is constructed with a hardcoded `1.0F / 60.0F` literal. Changing the config field has no effect.
- **Recommended fix:** Pass `config_.fixedDeltaSeconds` into `FixedStepTimer`, or remove the field from `AppConfig` and add a comment that `1/60` is intentional.

---

**L-2 · `AudioEventRouter` uses octile approximation for spatial distance**

- **File:** `src/app/AudioEventRouter.cpp` line 10
- **Evidence:** `ComputeSpatialVolume` uses `ApproximateEuclideanDistanceOctile` which overestimates diagonal distances by up to ~4%. Attenuation thresholds (18/60 world-units) are based on exact distances in the design doc.
- **Impact:** Negligible gameplay impact; some diagonal sounds are slightly quieter than specified. Document if intentional.

---

**L-3 · `static bool previousPauseDown` in `Input.cpp` persists across game sessions**

- **File:** `src/platform/Input.cpp` line 7
- **Evidence:** The static persists the entire process lifetime. In theory, a pause-key held at end of previous session could trigger a spurious `pausePressed = true` at the start of the next. The `suppressMenuInteractionUntilRelease_` flag in `GameApp` mitigates this.
- **Recommended fix:** Pull `previousPauseDown` into a persistent state struct that gets reset on each game start, for clarity.

---

**L-4 · `PlayerFlowField::CanTraverse` called with reversed direction arguments**

- **File:** `src/game/navigation/PlayerFlowField.cpp` line 118
- **Evidence:** `CanTraverse(maze, neighborX, neighborY, currentX, currentY)` checks whether a neighbor can traverse *to* current, while BFS intent is *current to neighbor*. Correct due to wall symmetry, but misleading.
- **Recommended fix:** Either reverse the call site arguments to match intent, or add a comment explaining the reversal exploits wall symmetry.

---

**L-5 · `static` debug log counters in `PlayerFlowField` never reset between games**

- **File:** `src/game/navigation/PlayerFlowField.cpp` lines 251, 329
- **Evidence:** `static int skipCount = 0` and `static int noBuildCount = 0` saturate after the first few flow transitions and permanently suppress subsequent debug messages.
- **Recommended fix:** These are clearly "rate-limited first-N" guards. Replace with a `[[maybe_unused]]` comment explaining why the limit is intentional, or reset them when `Invalidate()` is called.

---

**L-6 · `IsInPlayerViewport` duplicated in `EnemySystem.cpp` and `EnemySystemCombatPhase.cpp`**

- **Evidence:** Identical function body in two files, both in anonymous namespaces.
- **Recommended fix:** Promote to a shared helper (e.g., `EnemySystemHelpers.h`).

---

**L-7 · `PlacePlayerAtSafeSpawn` returns `false` when third fallback succeeds**

- **File:** `src/game/systems/MazeSystem.cpp` lines 581–590
- **Evidence:** `if (PlacePlayerDeterministic(...)) { return false; }` — player IS placed but function signals failure. Call sites currently ignore the return value, so no gameplay bug.
- **Recommended fix:** Either `return true` to signal success, or document the intent ("return false = non-ideal placement").

---

**L-8 · `(void)deltaSeconds` in `UpdateGameOverPhase` is misleading**

- **File:** `src/game/Game.cpp` line 181
- **Evidence:** `(void)deltaSeconds` appears as the first statement, but `deltaSeconds` IS passed to `RunPlayingWorldTick` two lines later.
- **Recommended fix:** Remove the spurious `(void)deltaSeconds;` line.

---

**L-9 · `PickSpawnEnemyForLevel` allocates a vector per call**

- **File:** `src/game/systems/SpawnerSystem.cpp` line 20
- **Evidence:** `std::vector<game::EnemySpawnChoice> candidates = game::EnemyTypesForLevel(level);` — hot-ish path (called when each base timer fires).
- **Recommended fix:** Return `std::array` or use a static lookup table since the level-to-types mapping is hardcoded.

---

### ⚪ RESIDUAL RISKS / OPEN ITEMS

---

**R-1 · `enemy.movement.overlap_check` is ~65% of movement cost (Phase 3.1 still pending)**

Per profile 35: `IsMovementBlockedByEnemies` inner loop is O(n) per full-tier enemy, with no spatial acceleration. The performance strategy marks "repath throttling" (3.1) and "incremental occupancy" (3.3) as pending. With 144 enemies and 7 full-tier, this is ~1.3 ms out of ~2.0 ms movement budget. As enemy counts grow, this will breach the 9.5 ms fixed-step budget.

---

**R-2 · No automated test suite**

The project has zero automated tests (`tests/`, `gtest`, `CTest` all absent). All behavior verification is manual playtest. Any regression in maze generation, respawn safety, or AI correctness goes undetected until observed in play.

---

**R-3 · Maze generation infinite-loop risk**

`InitializeMazeWorld` has `do { GenerateConnectedMaze(...) } while (checks fail)`. If a bug in `IsMazeFullyAccessible` or `IsMazeWallTopologyValid` causes always-fail, the game hangs on game start. No iteration cap exists. Low probability but no safeguard.

---

**R-4 · Architecture boundary migration incomplete**

`ARCHITECTURE.md` requires "only App/Platform layer touches raylib directly; game layer renders through IRenderer." `Renderer2D` is in the platform layer but directly queries `WorldState`, `WorldGeometry`, and game-layer constants. `Game::Render` calls `renderer.RenderGameplay(state, ...)` — the `IRenderer` interface carries full game state rather than being agnostic. This is explicitly a "target/direction" in the docs but remains an active coupling risk if game state layout changes.

---

**R-5 · `ScheduleRebuild` copies entire `MazeState` on main thread**

`ScheduleRebuild` (PlayerFlowField.cpp L230) copies `world.maze` (3600 cells) for async rebuild. With `kProfilingWindowFrames = 120` and player-cell cadence triggering at most once per 3 cell changes, this is low-frequency. However, under fast player movement, the copy could stall the main thread's fixed step. Monitor in profiler when player speed is high.

---

## Must-Fix Before Next Milestone

| # | Finding | Why Must-Fix |
|---|---------|-------------|
| H-1 | Hot-path allocations in `UpdateEnemySystem` | Performance budget violation; compounds under enemy load |
| H-2 | SAP `pairVisited` O(n²) alloc per call | Same: hot path, heap alloc, grows with enemy count |
| M-1 | Chain-kill explosion VFX missing | Correctness — contradicts documented chain-reaction behavior |
| M-2 | `IsWallDistributionValid` not implemented | Design doc vs. code mismatch; maze quality not enforced |
| M-4 | Drone spawn interval wrong constant | Doc/code disagreement; fix is one-liner |

## Recommended Follow-Ups

- Document the off-screen firing gate (M-3) in `GAME_DESIGN.md`
- Fix image leaks in `LoadResources` (M-6) — trivial `UnloadImage` additions
- Deduplicate `DecrementOriginBaseAliveCount` (M-5) and `IsInPlayerViewport` (L-6)
- Track phase-3 optimization work (R-1) on the performance backlog
- Add even a minimal crash/smoke-test harness (R-2)

---

All review blocks complete. Here's a summary of what was found across all nine architecture areas:

**2 High findings** — both are hot-path heap allocation violations: per-frame vectors in `UpdateEnemySystem` and the O(n²) `pairVisited` allocation in the broad-phase. These are the most impactful items given the existing performance constraints.

**7 Medium findings** — ranging from a correctness bug (chain-kill explosions never spawning VFX, contradicting the design doc), a missing maze validation function (`IsWallDistributionValid`), a wrong drone spawn interval, an undocumented firing rule, a resource leak in sprite loading, and a duplicated utility function.

**9 Low findings** — mostly doc/code inconsistencies, misleading code patterns, or minor semantic issues with no runtime impact.

**5 Residual risks** — the most notable being the absent test suite, the still-pending Phase 3.1 enemy movement spatial optimization (overlap check is ~65% of movement cost at 144 enemies), and the incomplete IRenderer architecture migration.

The **5 must-fix items** before the next milestone are: the two hot-path allocation violations (H-1, H-2), the chain-reaction explosion correctness bug (M-1), the missing maze wall distribution validation (M-2), and the wrong drone interval constant (M-4 — a one-liner fix).

