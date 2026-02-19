# BOLO Architecture

This file defines architecture expectations for both current and future code.
`docs/GAME_DESIGN.md` is the gameplay source of truth; this file is about structure and engineering style.

## Required Engineering Principles

- Prefer static memory over dynamic allocation during gameplay.
- Prefer data-oriented design over OOP-heavy patterns (`struct`-first, plain data + systems).
- Never allocate memory in the mid-game hot path (update/render loops).
- Avoid heavy STL container usage in core runtime loops.
- Keep rendering separate from gameplay simulation logic.
- Use a viewport/camera system to decouple world coordinates from screen coordinates.

## High-Level Runtime Architecture

```text
App
 ├─ Game
 │   ├─ World
 │   ├─ Systems
 │   ├─ Entities
 │   ├─ Resources
 │   └─ State machine
 └─ Platform (raylib)
```

## Data-Oriented World Model (Target Shape)

```cpp
struct World {
    Maze maze;
    Player player;
    Enemy enemies[MAX_ENEMIES];
    Base bases[6];
};
```

```cpp
struct Player {
    Vec2 pos;
    float angle;
    float turretAngle;
    float fuel;
    int lives;
};
```

```cpp
enum EnemyType { Drone, Torpedo, Hunter, Assassin };

struct Enemy {
    Vec2 pos;
    Vec2 vel;
    EnemyType type;
    bool active;
};
```

```cpp
struct Base {
    Vec2 pos;
    float spawnTimer;
    bool alive;
};
```

Notes:

- Use fixed-size arrays (or preallocated pools) for entities where practical.
- If dynamic containers are needed, reserve capacity during initialization and avoid growth at runtime.

## Simulation Update Order (Important)

Preferred fixed-step system order:

1. Input
2. AI
3. Movement
4. Collision
5. Combat
6. Spawning
7. Fuel/Rules
8. Cleanup

Current implementation may differ in places; migrate toward this order as systems are expanded.

## Fixed Time Step Requirement

Simulation must use fixed `dt = 1/60` to keep gameplay deterministic and stable across hardware (including handheld targets).

Reference loop pattern:

```cpp
const float FIXED_DT = 1.0f / 60.0f;

float accumulator = 0;

while (!WindowShouldClose())
{
    float frameTime = GetFrameTime();
    accumulator += frameTime;

    while (accumulator >= FIXED_DT)
    {
        update(FIXED_DT);   // simulation step
        accumulator -= FIXED_DT;
    }

    render();
}
```

## Renderer Boundary

Game systems should target an engine-facing renderer interface, not raw raylib calls.

Example interface:

```cpp
// renderer.h (NO raylib here)
struct IRenderer
{
    virtual void drawSprite(int spriteId, Vec2 pos, float rot) = 0;
    virtual void drawRect(Rect r) = 0;
    virtual void drawText(const char* text, Vec2 pos) = 0;
};
```

Raylib-side implementation example:

```cpp
// raylib_renderer.cpp
class RaylibRenderer : public IRenderer
{
public:
    void drawSprite(int id, Vec2 pos, float rot) override
    {
        DrawTextureEx(textures[id], {pos.x, pos.y}, rot, 1.0f, WHITE);
    }

    void drawRect(Rect r) override
    {
        DrawRectangle(r.x, r.y, r.w, r.h, GRAY);
    }

    void drawText(const char* text, Vec2 pos) override
    {
        DrawText(text, pos.x, pos.y, 12, WHITE);
    }
};
```

Game-side rendering example:

```cpp
void Game::render(IRenderer& r)
{
    for (auto& e : world.enemies)
        r.drawSprite(SPRITE_ENEMY, e.pos, 0);

    r.drawSprite(SPRITE_PLAYER, world.player.pos, world.player.angle);
}
```

App wiring example:

```cpp
RaylibRenderer renderer;

while (!WindowShouldClose())
{
    game.update(FIXED_DT);

    BeginDrawing();
    ClearBackground(BLACK);

    game.render(renderer);

    EndDrawing();
}
```

Rule: only App/Platform layer touches raylib directly.
Game layer must stay framework-agnostic and render through `IRenderer`.

## Suggested Folder Structure (Target)

```text
/src
  /core
    app.cpp
    platform.cpp

  /game
    game.cpp
    world.h

    /entities
      player.h
      enemy.h
      base.h

    /systems
      ai_system.cpp
      movement_system.cpp
      collision_system.cpp
      render_system.cpp

    /states
      menu_state.cpp
      play_state.cpp

    /resources
      textures.h
```

This structure is directional guidance, not a requirement to rename all files immediately.
