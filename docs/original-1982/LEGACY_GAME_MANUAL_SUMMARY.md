# Legacy BOLO Manual Summary (1992)

Concise, implementation-oriented reference distilled from the original BOLO manual text.
Use this document as historical design guidance when recreating classic behavior.

## Source and Scope

- Source: original BOLO manual text provided in project discussion.
- Era: 1992 release (Evlyn Software / Synergistic Software presentation).
- This is a **design summary**, not a literal executable specification.

## Narrative Premise (Scenario)

- The player is a lone survivor behind enemy lines.
- Enemy "replicator factories" mass-produce robot tanks.
- Player finds and pilots an old BOLO (Mark XXV, Stupendous class).
- Mission goal is strategic: destroy factory infrastructure before mass deployment.

## Core Gameplay Objective

- Navigate a dangerous maze and destroy **six enemy tank bases**.
- Bases continuously manufacture enemy tanks.
- Clearing all six bases completes the maze and starts a new one.

## Run Setup (Options)

At start, player chooses:

- **Level**: `1..9` (difficulty scales by level)
- **Maze density**: `1..5`

Manual notes indicate new random maze generation each run and potentially long generation time on original hardware.

## Enemy and Base Design

- Six bases exist per maze.
- Enemy tank roster scales by level:
  - simple drones at low level
  - faster torpedo tanks
  - hunter/killer and assassin-style tanks at higher levels
- Some enemy types pursue via efficient routing.

## HUD / Control Panel (Right Side)

Original panel elements:

- score display
- tanks/lives remaining (manual says player starts with five)
- fuel gauge (red stripe)
- base locator (nearest base direction by quadrant indicators)
- radar/minimap for player position in maze

## Controls (Manual Layout)

Two mirrored key sets were supported (right-hand and left-hand centric).

Primary actions:

- accelerate forward / reduce backward
- accelerate backward / reduce forward
- hull rotation (45-degree steps)
- turret rotation (45-degree steps)
- stop movement
- fire (space)
- pause/resume (Escape)
- sound toggle (`Ctrl+S`)

Advanced combined controls:

- forward+left turn
- forward+right turn
- reverse+left turn
- reverse+right turn

## Scoring Model

- Enemy tank score = current level number (`1..9`) per tank kill.
- Base score = `100 * level`.
- After clearing all six bases in a maze, player earns one extra tank/life.

## Combat and Damage Notes

- To destroy a base, shots must hit the center target.
- Base perimeter walls are self-healing; player must break through first.
- Ricochet/shrapnel can damage player when firing near walls/bases.

## Implied Implementation Features (Legacy Behavior)

Likely/explicit from manual text:

- discrete heading increments (45-degree step turning)
- separate hull and turret orientation
- velocity state with acceleration/deceleration controls
- pause state toggle
- persistent per-run resources (fuel, lives/tanks)
- procedural maze generation with difficulty/density parameters
- directional locator + coarse radar abstractions

## Porting Notes for Modern Recreation

- Treat this as canonical historical intent; modern implementation may adapt details.
- Keep separations clear:
  - historical mechanics in this file
  - current implemented mechanics in `docs/GAME_DESIGN.md`
- Emulator keyboard-layout differences were historically noted in community notes.
