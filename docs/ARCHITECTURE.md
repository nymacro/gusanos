# Architecture

## Layer Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│                     Application Layer                             │
│                     gusanos.cpp (main)                            │
├──────────────────────────────────────────────────────────────────┤
│    Game Logic           │  Rendering          │  Network          │
│                          │                     │                   │
│  Game                   │  Gfx                │  Network          │
│  Level                  │  Viewport            │  Client/Server    │
│  BaseObject/Particle    │  Sprite             │  Updater          │
│  BaseWorm/BasePlayer    │  Blitters (SW)      │  BitStream        │
│  Weapon                 │  SDLRenderer (GUI)   │  Replicators      │
│  Explosion              │  Font               │  PosSpdReplicator │
│  NinjaRope              │  Distortion         │                   │
│  PlayerInput/AI         │                     │                   │
├─────────────────────────┼─────────────────────┼───────────────────┤
│  Console (dev CLI)      │  Menu/GUI (OmfgGUI) │  Lua Scripting    │
│                         │                     │  (embedded 5.1)   │
├─────────────────────────┴─────────────────────┴───────────────────┤
│                   Abstraction Layer                                 │
│                                                                     │
│  allegro_compat.h/cpp  — BITMAP, blit, drawing, color, timer       │
│  network_compat.h       — ZoidCom type stubs (DISABLE_ZOIDCOM)      │
│  fmod_compat.h/cpp      — FMOD stub (stubbed out)                    │
├──────────────────────────────────────────────────────────────────────┤
│                     Platform Layer                                    │
│                                                                       │
│  SDL3 (video, events, input, timer)                                   │
│  ENet (UDP networking)  │  SDL3_mixer (audio)  │  libpng/zlib         │
│  Boost (filesystem, pool, random, crc)          │  Lua 5.1 (bundled) │
└──────────────────────────────────────────────────────────────────────┘
```

## Dependency Graph (include-level)

Key file inclusion relationships (pragmatic, not exhaustive):

```
gusanos.cpp
  ├── allegro_compat.h         (SDL3 → BITMAP, pixel ops, drawing)
  ├── gusanos.h                (quit global, exit())
  ├── game.h                   (Game singleton, Level, Options)
  ├── level.h                  (Level, Material, WaterParticle)
  ├── gfx.h                    (Gfx singleton, Blenders)
  ├── network.h                (Network singleton, LuaEventDef)
  ├── sfx.h                    (Sfx singleton, Listener)
  ├── menu.h                   (OmfgGUI::GContext, SDLRenderer)
  ├── console.h → gconsole.h   (Console with game extensions)
  ├── keyboard.h / mouse.h     (input state)
  ├── viewport.h               (Viewport, camera)
  ├── particle.h               (Particle → BaseObject)
  ├── sprite.h / sprite_set.h  (visual assets)
  ├── font.h                   (bitmap font rendering)
  ├── updater.h                (file transfer manager)
  ├── script.h / glua.h        (Lua integration)
  ├── luaapi/context.h         (LuaContext wrapper)
  └── http/                    (HTTP for server list)
```

## Object Ownership & Lifetimes

```
main()
  ├── game (Game)              — owns Level, objects Grid, players list, weapon list
  ├── gfx (Gfx)                — owns window, renderer, screenTexture, buffer BITMAP
  ├── network (Network)        — static stubs (DISABLE_ZOIDCOM)
  ├── sfx (Sfx)                — owns sound channels, listeners
  ├── console (GConsole)       — owns bindings, variables, commands
  ├── updater (Updater)        — file transfer state
  ├── lua (LuaContext)         — Lua state
  ├── luaCallbacks (LuaCallbacks) — callback vectors
  └── spriteList (SpriteSet)   — global sprite set list
```

All major subsystems are global singletons declared `extern` in their headers
and defined in their `.cpp` files.

## Main Game Loop (gusanos.cpp:201-451)

```
while (!quit || !network.isDisconnected())
  ── Logic tick (capped at 100 Hz via timer) ──
    1. Delete flagged objects (deleteMe == true)
    2. Think: every object → every player → Game::think()
    3. Updater::think() — file transfer progress
    4. Sfx::think() — channel management (MUST precede player delete)
    5. Delete flagged players + cleanup
    6. Network::update()
    7. Console input + mouse poll + console think
    8. Sprite animation (spriteList.think())
    9. Lua afterUpdate callbacks
  ── Render (uncapped) ──
    For each player:
      player->render() → viewport->render(player)
        - Level::draw() (terrain)
        - Draw all objects in viewport range
        - Draw worm + hud
    Console::render()
    OmfgGUI::menu.render()
    Lua afterRender callbacks
    Gfx::updateScreen() (buffer → texture → SDL_Render)
```

## Threading Model

Single-threaded. Everything runs on the main thread:
- SDL3 events are polled in `keyboard.cpp` / `mouse.cpp`
- ENet is serviced in `Network::update()`
- Audio is handled by SDL3_mixer's internal thread (transparent)
- No mutexes or atomics in game code

## Build Targets

| `build=` | Binary | Use Case |
|----------|--------|----------|
| `release` | `bin/posix/gusanos` | Full game, optimized |
| `debug` | `bin/posix/gusanos` | Full game, debug symbols + asserts |
| `dedserv` | `bin/posix/gusanos-ded` | Headless server, optimised |
| `dedserv-debug` | `bin/posix/gusanos-ded` | Headless server, debug |

Dedicated server builds exclude all rendering/audio/input code via `#ifndef DEDSERV`
guards.

## DEDSERV Guards

The `DEDSERV` preprocessor define gates all GUI code. Key pattern:

```cpp
#ifndef DEDSERV
    // rendering, input, audio, UI
#endif
```

This is used in nearly every component — graphics (`gfx.cpp`), sound (`sfx.cpp`),
input (`keyboard.cpp`, `mouse.cpp`), fonts, menus, and viewports.

## Network Architecture (DISABLE_ZOIDCOM)

The current build defines `DISABLE_ZOIDCOM`, which activates stub
implementations in `network_compat.h`. The `Network` class becomes a set of
static no-op methods. Key stubs:

- `ZCom_BitStream` — bit-level serialization (all methods no-op)
- `ZCom_Node` — network node with replication (all methods no-op)
- `ZCom_Control` — connection manager (all methods no-op)
- `ZoidCom` — library init (returns true)

The `DISABLE_ZOIDCOM` ifdef also gates:
- `Game::assignNetworkRole()` / `removeNode()` in `game.cpp`
- `Updater::assignNetworkRole()` / `removeNode()` in `updater.cpp`
- `Particle::assignNetworkRole()` in `particle.cpp`
- `LuaEventDef::call()` / destructor in `network.h`

## Resource Lifecycle

```
Game::reloadModWithoutMap()
  ├── refreshResources()  — scans mod path for levels, scripts, sprites, etc.
  │     ResourceLocator<Level>::refresh()
  │     ResourceLocator<Script>::refresh()
  │     spriteSet.reset() + scan sprite dirs
  │     xmlLocator/gssLocator refresh (OmfgGUI)
  ├── Game::loadWeapons()  — loads weapon definitions from Lua scripts
  └── Game::runInitScripts()  — runs startup Lua

Game::changeLevel("levelname")
  ├── Level::unload()  — frees material/image BITMAPs
  ├── Find level file via levelLocator
  ├── Loader (gusanos/liero/lierox) parses file into Level
  ├── objects.resize(grid) — sets up spatial grid
  └── Level::loaderSucceeded()
```

## Abbreviation Guide

| Term | Meaning |
|------|---------|
| GSS | GUI Style Sheet (OmfgGUI theming language) |
| Omfg | Open Modular F*cking GUI (the GUI framework) |
| GConsole | Game Console (Console subclass) |
| GUS | Gusanos (level file format) |
| LOSP | Losp (level file format variant) |
| ZCom | ZoidCom (original networking library, now stubbed) |