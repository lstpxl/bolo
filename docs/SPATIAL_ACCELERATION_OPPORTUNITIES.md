# Spatial acceleration opportunities

This document lists remaining places in the codebase where linear scans over enemies (or similar) could use **`game::spatial::EnemyCellOccupancy`** (60×60 maze cells, 6 world-units per cell) or related structures.

**Already in place:**

- **`EnemyCellOccupancy`** — used for enemy movement separation / overlap (`IsMovementBlockedByEnemies`, separation probe, `TrySeparationTurn`) via 3×3 `ForEachInCell` neighborhoods.
- **`SweepPruneBroadPhase`** — enemy–enemy frontal collision pairs in `EnemySystemCollision.cpp`.
- **`GetEnemiesAlongRay`** / **`rayQueryOccupancy`** — used where `FreeDistanceAheadWithEnemies` is called with occupancy (e.g. torpedo, parts of `EnemySystem.cpp`).

---

## Hot path — per physics step / every frame

### 1. `CollisionSystem.cpp` — `TryProjectileHitFullTierEnemy` (L62–97)

**What:** For each alive projectile, scans all enemies for full-tier + `DistanceSq` within `kProjectileHitRadius` (0.7 world-units).

**Fit:** `ForEachInCell` at the projectile’s maze cell (or 3×3 for boundary safety). Radius is well under one cell.

**Note:** `UpdateCollisionSystem` does not currently receive `EnemyCellOccupancy`; wiring `navigationCache.enemyCellOccupancy` (after it is built for the tick) is required.

---

### 2. `CollisionSystem.cpp` — player vs enemies (L211–234)

**What:** Linear scan of all enemies for full-tier + `DistanceSq` within `kPlayerEnemyCollisionRadius` (sum of radii, 1.0 world-units).

**Fit:** Same as above — `ForEachInCell` around the player cell (3×3 is conservative).

---

### 3. `Game.cpp` — `ApplyExplosionBlast` (L40–60)

**What:** Scans all enemies for full-tier + blast distance within `kExplosionBlastRadiusUnits` (0.5 world-units).

**Fit:** Enumerate only the cell containing `center` (or 3×3). Blast radius is smaller than a cell; avoids O(n) on every blast tick.

**Note:** Needs access to occupancy + `CellCoordCache` (or equivalent world→cell) at the call site; occupancy must reflect positions at blast time.

---

### 4. “Enemies in first cell” — Drone / Hunter / Torpedo

These loops already know `nextCellX` / `nextCellY` but still iterate **all** enemies to count matches on `other.cellCoord`.

| File | Approx. lines | Fix |
|------|----------------|-----|
| `EnemyDrone.cpp` | L199–210 | `occupancy.CountInCell(nextCellX, nextCellY)` |
| `EnemyHunter.cpp` | L283–291 | Same |
| `EnemyTorpedo.cpp` | L199–207 | Same |

**Priority:** High — trivial replacement; removes O(n) inside direction-scoring loops.

---

### 5. `EnemyDrone.cpp` — nearest enemy for watch escape (L444–457)

**What:** Full O(n) scan for nearest alive enemy to choose an “away” heading.

**Fit:** Expand neighborhood from self’s cell (1×1 → 3×3 → 5×5 …) until a non-empty ring is found, then narrow-phase nearest among those indices. Typical case stops at 3×3.

---

### 6. `EnemyDrone.cpp` — pursuit / separation candidate checks (~L502–512, ~L597–611)

**What:** Nested loops: multiple candidate headings × distance to **all** enemies.

**Fit:** Same pattern as movement overlap — 3×3 `ForEachInCell` around each `candidatePosition` (with `CellCoordCache::WorldToCell`).

---

### 7. `EnemySystemUncouple.cpp` — `SelectUncoupleHeading` (L149–188)

**What:** O(n) over enemies within `kUncoupleEnemyForceRangeUnits` (4.0 world-units = 2× preferred separation) for separation and obstacle-avoidance forces.

**Fit:** 3×3 neighborhood around self (range &lt; one cell diameter).

---

### 8. `EnemySystemUncouple.cpp` — `ComputeUncoupleEscapeScore` (L386–397)

**What:** O(n) count of uncouple-mode enemies within `kUncouplePriorityCrowdingRangeUnits` (3.0 world-units).

**Fit:** 3×3 `ForEachInCell`; filter `other.aiMode == Uncouple` in the callback.

---

### 9. `WorldGeometry.cpp` — ray enemy candidates when occupancy is null (L452–469)

**What:** If `rayQueryOccupancy == nullptr`, linear scan of enemies with a distance filter before ray tests.

**Fit:** Ensure callers that are hot (e.g. uncouple paths using `FreeDistanceAheadWithEnemies` without occupancy) pass **`EnemyCellOccupancy`** / ray occupancy so this fallback is not taken.

---

## Warm / cold path

### 10. `SpawnerSystem.cpp` — `IsSpawnPositionFree` (L163–180)

**What:** Distance from spawn point to every alive enemy.

**Fit:** `ForEachInCell` in a small neighborhood around the spawn cell. Called only when a base actually spawns — lower priority than collision / AI hot paths.

---

### 11. `MazeSystem.cpp` — `HasEnemyInManhattanRange` (L289–302)

**What:** Manhattan range in maze cells over all enemies.

**Fit:** Cell occupancy or BFS on occupied cells. Cold — placement / generation logic.

---

### 12. `EnemySystemPathfinding.cpp` — `BuildEnemyOccupancy` (L119–137)

**What:** Marks maze cells occupied by enemies — O(n) refresh when pathfinding runs.

**Fit:** Reuse a snapshot from sim if kept in sync; incremental updates only if profiling shows this as a bottleneck.

---

### 13. Debug-only scans

- `EnemyAssassin.cpp` — nearest / overlap for emergency debug log (~L337–351).
- `EnemySystemCheapTier.cpp` — assassin wall-state debug (~L221–235).
- `EnemySystem.cpp` — `DebugLogEnemiesAtPosition` (~L1549+).

**Fit:** `EnemyCellOccupancy` if those code paths need to stay cheap; otherwise optional.

---

## Not primary targets (context)

- **`EnemySystemCombatPhase.cpp`** — no O(n) enemy spatial scan; LOS is wall-based to player.
- **`ProjectileSystem.cpp`** — updates projectiles only; hits are in `CollisionSystem`.
- **`AudioEventRouter.cpp`** — iterates gameplay events, not enemy positions for distance.
- **`Renderer2D.cpp` / `HudPanel.cpp`** — full enemy iteration for render/UI; optimize only if draw cost is profiled as a problem.

---

## Summary table

| # | File | Lines (approx.) | Query type | Radius / scope | Acceleration | Priority |
|---|------|-----------------|------------|----------------|--------------|----------|
| 1 | `CollisionSystem.cpp` | 62–97 | Projectile hit | 0.7 u | `ForEachInCell` | **High** |
| 2 | `CollisionSystem.cpp` | 211–234 | Player–enemy | 1.0 u | `ForEachInCell` (3×3) | **High** |
| 3 | `Game.cpp` | 40–60 | Blast | 0.5 u | `ForEachInCell` | **High** |
| 4 | `EnemyDrone.cpp` | 199–210 | Count in cell | exact cell | `CountInCell` | **High** (trivial) |
| 4b | `EnemyHunter.cpp` | 283–291 | Count in cell | exact cell | `CountInCell` | **High** (trivial) |
| 4c | `EnemyTorpedo.cpp` | 199–207 | Count in cell | exact cell | `CountInCell` | **High** (trivial) |
| 5 | `EnemyDrone.cpp` | 444–457 | Nearest enemy | global | Expanding cell rings | Medium |
| 6 | `EnemyDrone.cpp` | ~502–612 | Min dist / separation | per candidate | 3×3 `ForEachInCell` | Medium |
| 7 | `EnemySystemUncouple.cpp` | 149–188 | Force accumulation | 4.0 u | 3×3 `ForEachInCell` | Medium |
| 8 | `EnemySystemUncouple.cpp` | 386–397 | Crowding count | 3.0 u | 3×3 `ForEachInCell` | Medium |
| 9 | `WorldGeometry.cpp` | 452–469 | Ray candidates | fallback | Pass occupancy from callers | Medium |
| 10 | `SpawnerSystem.cpp` | 163–180 | Spawn clearance | varies | `ForEachInCell` | Low |
| 11 | `MazeSystem.cpp` | 289–302 | Manhattan range | cells | Occupancy / BFS | Low |
| 12 | `EnemySystemPathfinding.cpp` | 119–137 | Build occupancy | O(n) refresh | Reuse / incremental | Warm |
| 13 | Various | debug | Nearest / click | — | Optional | Cold |

---

## Pairing note

**`SweepPruneBroadPhase`** is for moving AABB pairs (enemy–enemy physics). It is not a drop-in for disk queries, projectile hits, or blast spheres — those map naturally to **`EnemyCellOccupancy`** (and possibly a dedicated projectile broad phase if projectile count grows large).

---

*Derived from codebase review; line numbers refer to the tree at authoring time and may drift.*
