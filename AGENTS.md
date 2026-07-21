# Agent Guide

This document is an entry point for AI agents (LLMs, coding assistants, analysis
tools) that need to understand and work with the Gusanos codebase.

Always keep AGENTS.md and other project documentation up to date.

Use the task tool to launch subagents specifying the model tier where possible to
execute tasks that can be performed easily with medium or weak model tiers.

## Overview

Gusanos is a C++17 game engine (SDL3 + ENet + SDL3_mixer) ported from the
original Allegro 4 / ZoidCom / FMOD codebase. The engine features destructible
terrain, pixel-level software blitters, Lua 5.1 scripting, and networked
multiplayer.

## Documentation Index

| File | Contents | Read first if... |
|---|---|---|
| [README.md](docs/README.md) | Project overview, quick reference, architecture diagram | You need one-paragraph context |
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | Layer diagram, dependency graph, singleton ownership, main loop, build targets, DEDSERV guards, network architecture | You need to understand how subsystems connect |
| [COMPONENTS.md](docs/COMPONENTS.md) | Per-file/per-class breakdown by subsystem: Core, Entities, Graphics, UI, Network, Audio, Scripting, Input, Utility, Tools | You need to find where something lives |
| [BUILD.md](docs/BUILD.md) | Build commands, output layout, SConscript targets, compiler flags, parser generation pipeline | You need to build, configure, or add dependencies |
| [zoidcom-spec.md](docs/zoidcom/spec/zoidcom-spec.md) | Generated specification for Zoidcom-compatible networking. Read-only, never edit | You need to know more about networking in the project |

## Recommended Reading Order

1. **READ FIRST:** [README.md](docs/README.md) — 30-second orientation
2. Read [ARCHITECTURE.md](docs/ARCHITECTURE.md) — understand the layer stack and main loop
3. Read [BUILD.md](docs/BUILD.md) — understand build targets and flags before modifying code
4. Use [COMPONENTS.md](docs/COMPONENTS.md) — reference, not read-through — to locate specific files and classes

## Key Conventions

### Preprocessor Guards

- **`DEDSERV`** — gates all rendering/audio/input code. Defined for dedicated
  server builds (`build=dedserv` / `build=dedserv-debug`). Every graphics call,
  sound call, input poll, font draw, and GUI operation must be wrapped in
  `#ifndef DEDSERV` / `#endif`.
- **`DEBUG`** — debug build only. Enables extra logging and assertions.
- **`NDEBUG`** — release/dedicated server builds. Disables assertions.

### Global Singletons

All major subsystems are `extern` globals defined with the same name as their
class (lowercase):

| Global | Class | Header |
|---|---|---|
| `game` | `Game` | `game.h` |
| `gfx` | `Gfx` | `gfx.h` |
| `network` | `Network` | `network.h` |
| `sfx` | `Sfx` | `sfx.h` |
| `console` | `GConsole` | `gconsole.h` |
| `updater` | `Updater` | `updater.h` |
| `lua` | `LuaContext` | `luaapi/context.h` |
| `luaCallbacks` | `LuaCallbacks` | `glua.h` |

### File Structure Conventions

- Each primary subsystem = one `.h` + `.cpp` pair in `Goop/` (e.g. `gfx.h` + `gfx.cpp`)
- Lua bindings live in `Goop/lua/` (e.g. `bindings-game.cpp`)
- Blitters live in `Goop/blitters/`
- Level loaders live in `Goop/loaders/`
- GUI framework lives in `GUI/`
- Utility code lives in `Utility/util/`
- Console subsystem lives in `Console/`

### Threading Model

Single-threaded. No mutexes, atomics, or thread-safe patterns in game code.
SDL3_mixer has its own internal audio thread (transparent). ENet is polled on
the main thread in `Network::update()`.

### Lua Integration

- **Lua 5.1** is bundled as `lua51/` (full source).
- `LuaContext` (`Goop/luaapi/context.h`) wraps the Lua state. Provides stack
  operations, function calls, class bindings, and reference tracking.
- `LuaCallbacks` (`Goop/glua.h`) defines ~20 callbacks (`afterUpdate`,
  `afterRender`, `wormRender`, `wormDeath`, etc.) fired from the game loop.
- `LuaObject` base class enables any game object to be Lua-scripted. Uses
  `makeReference()` / `deleteThis()` for lifetime management (ref-counted).
- Lua bindings are registered in `Goop/lua/bindings.cpp` via `registerLuaBindings()`.

### Memory Management

- Game entities (`BaseObject` subclasses) use `operator new(size_t)` to allocate
  from the Lua state (placement new into Lua userdata). Deletion via
  `deleteThis()` (virtual, ref-counted path) or direct `delete` (for non-Lua
  objects).
- All core resources (sprites, fonts, levels, sounds) use
  `ResourceLocator<T, Cache, ReturnResource>` — a template-based caching loader
  with path scanning.
- Boost `shared_ptr` is used for `PlayerOptions` only.

### Network System

All network synchronization uses a ZoidCom API compatibility layer built on top
of ENet for reliable UDP transport. The layer lives in the `Net/` directory and
preserves the original ZoidCom type names (`ZCom_Control`, `ZCom_Node`,
`ZCom_BitStream`, `ZCom_Replicator`) so game code in `Goop/` requires no
rewriting.

Key files in `Net/`:

| File | Purpose |
|---|---|
| `network_compat.h` | Umbrella header — includes all ZoidCom API types |
| `net_types.h` | Type aliases and enums (`ZCom_ConnID`, `eZCom_SendMode`, etc.) |
| `net_bitstream.h/cpp` | `ZCom_BitStream` — bit-level serialization |
| `net_address.h/cpp` | `ZCom_Address` — ENet address wrapper |
| `net_replicator.h` | `ZCom_Replicator` base class + numeric/bool/string replicators |
| `net_node.h/cpp` | `ZCom_Node` — networked object registration and replication |
| `net_control.h/cpp` | `ZCom_Control` — ENet server/peer connection management |
| `tests/` | Unit tests for the networking layer |

The `Goop/network.cpp/h` facade sits on top of this layer, managing game-level
state (session lifecycle, Lua events, server list, player management).

## Common Tasks

### Adding a new file

1. Create the `.h` and `.cpp` in the appropriate directory
2. If the file is in `Goop/`, add it to `Goop/SConscript` — the `getObjects()`
   function auto-discovers `.cpp` files in `Goop/` and its `lua/`, `blitters/`,
   `loaders/` subdirectories. For other directories, add the `.cpp` to the
   respective `SConscript`.
3. Wrap rendering/input/audio code in `#ifndef DEDSERV`

### Adding a new console variable

```cpp
console.registerVariables()
    ("MY_VAR", &myVar, 42)  // name, pointer, default
;
```

### Adding a new Lua binding

1. Add the binding function in `Goop/lua/bindings-*.cpp` (or create a new file)
2. Call `LuaBindings::init()` in the binding function — each binding file's init is called from `game.cpp` startup.

### Adding a new Lua callback

1. Add an entry to the `LuaCallbacks` enum in `Goop/glua.h`
2. Fire it with `EACH_CALLBACK(i, MyNewCallback) { ... }`
3. Expose it in Lua bindings via `luaCallbacks.bind("myNewCallback", ref)` in the bindings init code.

## Build Verification

```bash
# Full debug build (10 min timeout)
scons build=debug -j1 | tail -20

# Clean
scons -c
```

## Pitfalls to Avoid

- **Don't refactor or clean up unrelated code** — the SDL3/ENet migration is
  complete; touching working code risks breaking fragile compat shims. The
  remaining work is completing the networked gameplay features (snapshot
  interpolation, client prediction, movement replication).
- **Don't add `#include "allegro_compat.h"` after other headers** — it must be
  the very first include in a translation unit.
- **Don't use standard mutexes** — the engine is single-threaded. Adding
  threading requires careful audit of global state.
- **Don't remove `#ifndef DEDSERV` guards** — dedicated server builds depend on
  them to omit all rendering code.

## Quick Search Cheatsheet

| Goal | Tool & Pattern |
|---|---|
| Find a class definition | `grep(pattern="class MyClass", path="Goop")` |
| Find all callers of a function | `grep(pattern="->myMethod\(", path="Goop")` |
| Find a global singleton declaration | `grep(pattern="extern .* mySingleton", path="Goop")` |
| Find DEDSERV-guarded sections | `grep(pattern="#ifndef DEDSERV", path="Goop")` |
| Find Lua binding registrations | `grep(pattern="registerLuaBindings", path="Goop")` |
