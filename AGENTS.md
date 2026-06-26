# Agent Guide

This document is an entry point for AI agents (LLMs, coding assistants, analysis
tools) that need to understand and work with the Gusanos codebase.

## Overview

Gusanos is a C++17 game engine (SDL3 + ENet + SDL3_mixer) ported from the
original Allegro 4 / ZoidCom / FMOD codebase. The engine features destructible
terrain, pixel-level software blitters, Lua 5.1 scripting, and networked
multiplayer. The project is in active SDL3 migration.

## Documentation Index

| File | Contents | Read first if... |
|---|---|---|
| [README.md](docs/README.md) | Project overview, quick reference, architecture diagram | You need one-paragraph context |
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | Layer diagram, dependency graph, singleton ownership, main loop, build targets, DEDSERV guards, network stubs | You need to understand how subsystems connect |
| [COMPONENTS.md](docs/COMPONENTS.md) | Per-file/per-class breakdown by subsystem: Core, Entities, Graphics, UI, Network, Audio, Scripting, Input, Utility, Tools | You need to find where something lives |
| [BUILD.md](docs/BUILD.md) | Build commands, output layout, SConscript targets, compiler flags, parser generation pipeline | You need to build, configure, or add dependencies |

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

### Network System (Stub)

The current build has `DISABLE_ZOIDCOM` defined, which makes all networking
code compile to no-ops. The `Network` class exposes a `static` interface that
returns default values. `ZCom_BitStream`, `ZCom_Node`, `ZCom_Control`, and
other ZoidCom types are stubs in `network_compat.h`.

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
2. Register it in `Goop/lua/bindings.cpp` `registerLuaBindings()`

### Adding a new Lua callback

1. Add an entry to the `LuaCallbacks` enum in `Goop/glua.h`
2. Fire it with `EACH_CALLBACK(i, MyNewCallback) { ... }`
3. Expose it in Lua bindings so scripts can `addEventHandler("myNewCallback", fn)`

## Build Verification

```bash
# Quick syntax check on a single file
clang++ -fsyntax-only -std=c++17 -I. -IUtility -IGoop -Ihttp -IConsole -INet -I../lua51 \
    -D_GNU_SOURCE -DDISABLE_ZOIDCOM -DBOOST_TIMER_ENABLE_DEPRECATED \
    path/to/file.cpp

# Full debug build
scons build=debug

# Full release build (dedicated server)
scons build=dedserv

# Clean
scons -c
```

## Pitfalls to Avoid

- **Don't add features, refactor, or clean up unrelated code** — the migration
  is in progress; touching working code risks breaking fragile compat shims.
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
