# Gusanos Migration Plan

## Overview

Migrate the Gusanos game engine from Allegro 4 + ZoidCom + FMOD to SDL3 + ENet + SDL3_mixer,
targeting C++17. Boost libraries are kept as-is (latest versions).

## Dependencies

### Existing (kept)
- **Boost** 1.70+ (filesystem, signals, random, crc, pool, lexical_cast, assign, preprocessor,
  cstdint, utility, array, progress, bind)
- **LuaJIT** (system library, pkg-config) — replaces bundled Lua 5.1 source
- **libpng** + **zlib**
- **re2c** (parser generation)

### New
- **SDL3** (graphics, input, events, video)
- **SDL3_mixer** (audio)
- **SDL3_ttf** (text rendering - optional/supplementary)
- **ENet** (reliable UDP networking)

### Removed
- Allegro 4.x
- ZoidCom (proprietary networking)
- FMOD 3.74

## Step 0: Clean Up

Remove legacy engine directories that are not built by the current SConstruct:

| Directory | Reason |
|-----------|--------|
| `src/` | Intermediate engine variant, superseded by Goop/ |
| `zoid/` | Oldest engine variant, superseded by Goop/ |

## Step 1: SConstruct & Build System

### Issues
- `types.ListType`, `UserList.UserList` — removed in Python 3
- `print "..."` — Python 2 syntax
- Hardcoded legacy paths and fragile custom detection (`MyConf`)
- `os.system()` for parser generation and stripping

### Changes
1. **Python 3 Conversion:** Update `SConstruct` and all `SConscript` files to Python 3 syntax.
2. **Compiler Flags:** Add `-std=c++17` and remove Allegro-specific flags.
3. **Dependency Detection:** 
   - Replace custom `MyConf` with standard SCons `pkg-config` usage via `env.ParseConfig`.
   - Detect: `sdl3`, `SDL3_mixer`, `SDL3_ttf`, `libenet`, `libpng`, `zlib`.
   - Update Boost detection to look for modern library names (e.g., `boost_filesystem`).
4. **Tooling:** Replace `os.system()` with `subprocess.run()`.
5. **Dedicated Server:** Ensure `DEDSERV` build only links against non-GUI dependencies where possible, or initializes SDL3 in headless mode (`SDL_INIT_EVENTS` only).

## Step 2: Graphics & Input (Allegro 4 → SDL3)

**Status: Complete.** SDL3 backend is implemented. Hybrid software/hardware
rendering pipeline is functional. Blitters operate on raw pixel data with SIMD
optimizations.

### Rendering Strategy: Hybrid Software/Hardware
Gusanos relies on destructible terrain and custom pixel blitters.
- **Master Buffer:** Maintain a CPU-side `SDL_Surface` (or raw memory buffer) for the game world.
- **Blitters:** Port `Goop/blitters/` to operate on raw pixel data with pitch, compatible with `SDL_Surface`.
- **Display:** Upload the modified buffer to an `SDL_Texture` each frame using `SDL_UpdateTexture`.
- **UI/Sprites:** Use `SDL_Renderer` for sprites and UI elements where pixel-perfect software blending isn't required by the original look.

### Core API Mapping

| Allegro 4 | SDL3 | Notes |
|-----------|------|-------|
| `allegro_init()` | `SDL_Init()` | |
| `set_gfx_mode()` | `SDL_CreateWindow()` + `SDL_CreateRenderer()` | |
| `BITMAP*` | `SDL_Surface*` (for CPU) / `SDL_Texture*` (for GPU) | |
| `create_bitmap()` | `SDL_CreateSurface()` or `SDL_CreateTexture()` | |
| `blit()` | `SDL_BlitSurface()` or `SDL_RenderTexture()` | |
| `masked_blit()` | `SDL_SetSurfaceColorKey()` + blit | |
| `drawing_mode()` | `SDL_SetRenderBlendMode()` or custom blitter logic | |
| `install_keyboard()` | `SDL_INIT_EVENTS` | |
| `key[]` | `SDL_GetKeyboardState()` or Event loop | |
| `install_mouse()` | `SDL_INIT_EVENTS` | |

### Key Files to Rewrite

| File | Action |
|------|--------|
| `Goop/gfx.cpp/h` | Main graphics system migration. Implement Hybrid Rendering. |
| `Goop/blitters/*` | Adapt to raw buffer access (ptr + pitch). |
| `Goop/font.cpp/h` | Port bitmap font drawing to use sub-rects of `SDL_Texture`. |
| `Goop/menu.cpp/h` | Port `AllegroRenderer` to `SDLRenderer` (implements `OmfgGUI::Renderer`). |
| `Goop/keyboard.cpp/h` | Port to SDL3 event polling. |
| `Goop/mouse.cpp/h` | Port to SDL3 mouse events. |

## Step 3: Networking (ZoidCom → ENet)

**Status: Largely complete.** The ENet-backed ZoidCom compatibility layer is
implemented in `Net/`. `ZCom_BitStream`, `ZCom_Node`, `ZCom_Control`,
`ZCom_Replicator`, and `ZCom_Address` are all functional. The remaining work
is game-level networking logic (snapshot interpolation, client prediction,
movement replication, file transfer).

### Transition to Authoritative Server
ZoidCom's high-level replication must be replaced by a manual snapshot-based system.

1. **NetBitStream:** Rewrite as a wrapper around ENet packets for bit-level serialization.
2. **NetObject:** Replace `ZCom_Node`. Each networked object must have a unique ID and a way to serialize its "dirty" state.
3. **Replication Layer:**
   - Server: Tracks all `NetObject`s, sends periodic snapshots to clients.
   - Client: Receives snapshots, performs interpolation for other worms/objects.
   - Prediction: Local worm uses client-side prediction; server sends corrections.
4. **Event System:** Port `LuaEventDef` to use the new `NetBitStream`.

### Key Files to Rewrite

| File | Action |
|------|--------|
| `Goop/network.cpp/h` | Core facade rewrite. |
| `Goop/server.cpp/h` | Implement ENet host and snapshot broadcasting. |
| `Goop/client.cpp/h` | Implement ENet peer and snapshot interpolation. |
| `Goop/base_player.cpp` | Replace `ZCom_Node` usage with `NetObject` equivalent. |
| `Goop/posspd_replicator.h` | Rewrite as a simple struct with `serialize/deserialize` methods. |

## Step 4: Audio (FMOD 3.74 → SDL3_mixer)

**Status: Complete.** FMOD → SDL3_mixer adapter is implemented in
`fmod_compat.h/cpp`.

### Mapping

| FMOD 3.74 | SDL3_mixer | Notes |
|-----------|------------|-------|
| `FSOUND_Sample_Load()` | `Mix_LoadWAV()` | |
| `FSOUND_PlaySound()` | `Mix_PlayChannel()` | |
| `FSOUND_SetVolume()` | `Mix_Volume()` | |
| `FSOUND_SetPan()` | `Mix_SetPanning()` | For `sound1d.cpp` |
| `FSOUND_StopSound()` | `Mix_HaltChannel()` | |

## Step 5: Verification & Testing

### Phase 1: Build & Headless
- Successfully build `dedserv` target with ENet and Boost.
- Verify networking (host/connect) without graphics.

### Phase 2: Core Rendering
- Implement `Gfx` and basic blitters.
- Verify level rendering and destructible terrain.

### Phase 3: Input & UI
- Verify keyboard/mouse in menus.
- Verify `OmfgGUI` rendering via `SDLRenderer`.

### Phase 4: Full Integration
- Test local game with sound.
- Test networked game with multiple clients.

## Implementation Details & Constraints

- **Thread Safety:** SDL3 events should be handled on the main thread. ENet can be serviced on the main thread as well.
- **Legacy Removal:** Strictly remove `src/` and `zoid/` as they contain conflicting legacy code.
- **Blitters:** MMX/SSE optimizations in `Goop/blitters/` should be preserved if possible, but raw C++ fallbacks must be prioritized for portability.

## Lua: bundled 5.1 → LuaJIT system dep

**Status: Complete.**

The bundled Lua 5.1 source tree (`lua51/`) was removed and replaced with
LuaJIT 2.x linked via `pkg-config` (`luajit-5.1`). The C API is
compatible with Lua 5.1.

### What was deleted

- `lua51/` — the full Lua 5.1 C interpreter source tree (50+ `.c`/`.h`
  files: `lapi.c`, `lcode.c`, `ldo.c`, `lgc.c`, `llex.c`, `lmem.c`,
  `lobject.c`, `lparser.c`, `lstate.c`, `lstring.c`, `ltable.c`, `lvm.c`,
  `lzio.c`, plus `lua.h`, `lauxlib.h`, `lualib.h`, `luaconf.h`).
- `lua/` (Lua 5.0.2 — dead code, was not in SConstruct's build list).

### What was kept

The C++ wrapper layer was preserved and moved to `luaapi/luaapi/`:

| File | Purpose |
|------|---------|
| `luaapi/luaapi/context.h/cpp` | `LuaContext` — the singleton `lua` global, call proxies, stack helpers, reference tracking |
| `luaapi/luaapi/types.h` | `LuaReference` struct, `lua_new_*` macros for userdata allocation |
| `luaapi/luaapi/macros.h` | `METHODC()`, `CLASS()`, `ENUM()` macros for binding C++ objects to Lua metatables |
| `luaapi/luaapi/classes.h` | `LuaID<T>` + `getObject<T>()` — type-safe userdata extraction via metatable tags |

The includes were updated from `#include "../lua.h"` to `#include <lua.hpp>`.

### Build system changes (already applied)

| File | Change |
|------|--------|
| `SConstruct` | Removed `'lua51'` from `sconscript` list; removed `#lua51` from `CPPPATH`; added `luajit` to `pkg-config` libs |
| `luaapi/SConscript` | Wraps `luaapi/` source files; links `libluajit-5.1` via `ParseConfig` |

### API compatibility

All C API features used by this codebase are identical between Lua 5.1 and
LuaJIT 2.x (`LUA_GLOBALSINDEX`, `LUA_REGISTRYINDEX`, `lua_newstate`,
`lua_newuserdata`, `luaL_ref`, `lua_pcall`, `luaopen_*`). No game code in
`Goop/` needed changes.
