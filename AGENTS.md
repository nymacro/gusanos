# Agent Guide

This document is an entry point for AI agents (LLMs, coding assistants, analysis
tools) that need to understand and work with the Gusanos codebase.

Always keep AGENTS.md and other project documentation up to date.

Use the task tool to launch subagents specifying the model tier where possible to
execute tasks that can be performed easily with medium or weak model tiers.

## Overview

Gusanos is a C++17 game engine (SDL3 + ENet + SDL3_mixer) ported from the
original Allegro 4 / ZoidCom / FMOD codebase. The engine features destructible
terrain, pixel-level software blitters, LuaJIT scripting, and networked
multiplayer.

## Documentation Index

| File | Contents | Read first if... |
|---|---|---|
| [README.md](docs/README.md) | Project overview, quick reference, architecture diagram | You need one-paragraph context |
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | Layer diagram, dependency graph, singleton ownership, main loop, build targets, dedicated (headless) mode, network architecture | You need to understand how subsystems connect |
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

- **`DEDSERV`** — no longer defined at compile time. Dedicated/headless mode is
  a runtime flag, `g_dedicated` (default `false` = client; set `true` by passing
  `--dedicated` on the command line; see `Goop/dedicated.h`). Load-bearing
  rendering/audio/input code is gated with `if (!g_dedicated)` / `if (g_dedicated)`.
  Inert structural `#ifndef DEDSERV` guards (member declarations, `#include`
  lines, `#ifdef DEDSERV #error` stubs, whole-file wrappers) are left in place
  but are dead — `DEDSERV` is never defined, so `#ifndef DEDSERV` blocks always
  compile and the client build is unchanged.
- **`DEBUG`** — debug build only. Enables extra logging and assertions.
- **`NDEBUG`** — release builds. Disables assertions.

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
| `lua` | `LuaContext` | `luaapi/luaapi/context.h` |
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

- **LuaJIT** (2.x) is linked via `pkg-config` (`luajit-5.1`).
  The `luaapi/` directory contains the C++ wrapper layer (`LuaContext`, etc.).
- `LuaContext` (`luaapi/luaapi/context.h`) wraps the Lua state. Provides stack
  operations, function calls, class bindings, and reference tracking.
- `LuaCallbacks` (`Goop/glua.h`) defines ~20 callbacks (`afterUpdate`,
  `afterRender`, `wormRender`, `wormDeath`, etc.) fired from the game loop.
- `LuaObject` base class enables any game object to be Lua-scripted. Uses
  `makeReference()` / `deleteThis()` for lifetime management (ref-counted).
- Lua bindings are registered in `Goop/lua/bindings.cpp` via `registerLuaBindings()`.

### Deterministic Gameplay RNG

Gameplay-critical random draws (weapon fire particle counts/angles/speeds, dig
and death bursts, explosion timeouts) must be deterministic across peers so
spawned particles match byte-for-byte. The authority seeds a per-burst
`GameRng` from `mix32(wormNodeID, actionSequence)` and runs the action under a
`GameplayRngScope` (RAII swap of the `g_gameplayRng` pointer); every peer re-runs
the same action from the sequence shipped in the SHOOT/Dig/Die event. See
`Utility/util/game_rng.{h,cpp}`. C++ gameplay code uses `grnd`/`gmidrnd`/
`grndInt` (not the legacy `rnd`/`midrnd`/`rndInt`): they draw from the active
scope when one is set and fall back to the legacy global stream otherwise.
Particle lifetime (think()-time sub-spawns) is deliberately left on the legacy
cosmetic stream.

**Lua scripts:** for any gameplay-affecting roll, use the bound `randomint`/
`randomfloat` functions — not `math.random`. The bindings route through
`grndInt`/`grnd`, so a draw made inside a `GameplayRngScope` (particle creation
scripts, the `wormDeath` callback fired during the scoped death burst, in-burst
Lua) is deterministic across peers; `math.random` is the LuaJIT stdlib, is never
seeded by the game, and stays non-deterministic. Out-of-scope `randomint`/
`randomfloat` draws fall back to the legacy stream and are cosmetic.

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

### Comment Style

Comments should be succinct and earn their place.

- **Comment *why*, not *what*.** Don't restate what the next line does (e.g.
  `i++; // increment i`) unless the code is genuinely complicated and
  breakage-prone. Explain non-obvious decisions, workarounds, gotchas,
  platform/compat quirks, magic numbers, and lines that look wrong but aren't.
- **No commented-out dead code.** Delete it — the history lives in git. This
  includes `/* ... */` blocks of obsolete implementations and `//` stubs of
  removed declarations.
- **No restating box/ASCII-art headers** that only repeat a function or class
  name.
- **Deduplicate.** If the same explanation appears twice, keep the more
  specific copy and trim the rest. Rationale for determinism, networking, and
  the runtime `g_dedicated` flag already lives in this file — don't restate it
  verbatim in source.
- **Trim verbose prose to its essence** rather than deleting a real "why"
  outright. Keep substantive API docs (Lua `/*! ... */` binding docs, public
  method contracts) only where they add info beyond the signature. When in
  doubt whether a comment carries value, keep it.

## Common Tasks

### Adding a new file

1. Create the `.h` and `.cpp` in the appropriate directory
2. If the file is in `Goop/`, add it to `Goop/SConscript` — the `getObjects()`
   function auto-discovers `.cpp` files in `Goop/` and its `lua/`, `blitters/`,
   `loaders/` subdirectories. For other directories, add the `.cpp` to the
   respective `SConscript`.
3. Wrap rendering/input/audio code in `if (!g_dedicated)` at runtime (include `Goop/dedicated.h`)

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
2. Fire it with `dispatchCallbacks(LuaCallbacks::MyNewCallback, args...)` (fire-and-forget), `dispatchCallbacksVeto(...)` (if a callback can veto by returning true), or loop `luaCallbacks.callbacksFor(LuaCallbacks::MyNewCallback)` for custom per-iteration logic
3. Expose it in Lua bindings via `luaCallbacks.bind("myNewCallback", ref)` in the bindings init code.

### Running fuzz tests

libFuzzer harnesses for the `Net` and `http` libraries live under `Net/fuzz/`
and `http/fuzz/`. They build only with clang and require both `build-tests=1`
and `fuzz=1`; clang is auto-detected (e.g. `clang++-22`) when bare `clang++`
is absent from PATH.

```bash
scons build=debug build-tests=1 fuzz=1
bin/posix/net_fuzz_bitstream -max_total_time=60 Net/fuzz/corpus/bitstream/
bin/posix/net_fuzz_bitstream_roundtrip -max_total_time=60 \
    -dict=Net/fuzz/net.dict Net/fuzz/corpus/bitstream_roundtrip/
bin/posix/net_fuzz_address -max_total_time=60 Net/fuzz/corpus/address/
bin/posix/http_fuzz_parseheaders -max_total_time=60 -dict=http/fuzz/http.dict \
    http/fuzz/corpus/parseheaders/
bin/posix/http_fuzz_urlencode -max_total_time=60 http/fuzz/corpus/urlencode/
```

Five harnesses: `net_fuzz_bitstream` (raw deserialize),
`net_fuzz_bitstream_roundtrip` (serialize/deserialize property test),
`net_fuzz_address` (offline `ZCom_Address` API; `setAddress` excluded — it does
synchronous DNS), and `http_fuzz_parseheaders` / `http_fuzz_urlencode`. The
`http_fuzz_parseheaders` finding (a `boost::bad_lexical_cast` abort on malformed
`Content-Length`) is fixed; all harnesses run clean.

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
- **Don't use `#ifndef DEDSERV` for new code** — dedicated mode is now runtime
  (`g_dedicated`, set via `--dedicated`); gate new rendering/audio/input code
  with `if (!g_dedicated)` instead. Existing inert `#ifndef DEDSERV` guards are
  left in place as dead structural markers and should not be mass-removed.
- **Don't add excessive comments** — see *Comment Style* above. Comment *why*,
  not *what*; don't keep commented-out dead code (git has the history); don't
  restate rationale already documented in this file.

## Quick Search Cheatsheet

| Goal | Tool & Pattern |
|---|---|
| Find a class definition | `grep(pattern="class MyClass", path="Goop")` |
| Find all callers of a function | `grep(pattern="->myMethod\(", path="Goop")` |
| Find a global singleton declaration | `grep(pattern="extern .* mySingleton", path="Goop")` |
| Find dedicated (headless)-gated sections | `grep(pattern="g_dedicated", path="Goop")` |
| Find Lua binding registrations | `grep(pattern="registerLuaBindings", path="Goop")` |

## Build Dependencies

```
# set up apt repos
cat <<EOF > /etc/apt/sources.list.d/unstable.sources
Types: deb deb-src
URIs: http://deb.debian.org/debian
Suites: unstable testing
Components: main
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg
EOF

cat <<EOF > /etc/apt/preferences.d/unstable
Package: *
Pin: release a=unstable
Pin-Priority: 100
EOF

apt-get update

# install deps
apt-get install -y build-essential scons libsdl3-dev libsdl3-mixer-dev libsdl3-ttf-dev libenet-dev libsdl3-image-dev libboost-all-dev libluajit-5.1-dev re2c curl vim tini tmux git
```

