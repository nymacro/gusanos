# Components

## Core Game Infrastructure

### Game (`Goop/game.cpp` + `game.h`)

The central orchestrator. Singleton `game`.

| Responsibility | Key Members |
|---|---|
| Initialization & command-line parsing | `init()`, `parseCommandLine()` |
| Mod management | `setMod()`, `loadMod()`, `reloadModWithoutMap()`, `refreshMods()` |
| Level lifecycle | `changeLevel()`, `refreshLevels()` |
| Object/player containers | `objects` (Grid), `players` (list), `localPlayers` (vector) |
| Weapon definitions | `weaponList` (vector of `WeaponType*`) |
| Game options | `options` (struct with physics/balance variables) |
| Messaging | `messages` (screen messages), `displayKillMsg()`, `displayChatMsg()` |
| Lua callback dispatch | `think()` fires `afterUpdate` callbacks |
| Network role assignment | `assignNetworkRole()`, `removeNode()` (via ZoidCom API) |

**Message queue:** `Game::msg` (MessageQueue) uses the `mq_define_message` macros
for deferred operations like `ChangeLevel`.

### Level (`Goop/level.cpp` + `level.h`)

Singleton `level` (owned by `game`). Represents the destructible terrain map.

| Area | Details |
|---|---|
| Terrain storage | `BITMAP* material` — 8-bit index bitmap (256 material types) |
| Visual layers | `image` (main), `background`, `paralax`, `lightmap`, `watermap` |
| Material system | `array<Material, 256> m_materialList` — indexed by pixel value in material bitmap |
| Pixel access | `getMaterial()`, `putMaterial()`, `getMaterialIndex()` — bounds-checked |
| Water simulation | `m_water` (list of `WaterParticle`), updated in `think()` |
| Level effects | `applyEffect()` — terrain deformation (explosions, digging) |
| Ray tracing | `trace()` — template-based line-of-sight with predicate |
| Spawn points | `LevelConfig::spawnPoints` — team-based positions |
| Level loaders | Plugin system via `ResourceLocator<Level>` — supports Gusanos `.gusanos`, Liero `.lev`, LieroX `.lxl` formats |

**File:** `Goop/loaders/` — `gusanos.cpp`, `liero.cpp`, `lierox.cpp`, `losp.cpp`.

### Material (`Goop/material.h` + `material.cpp`)

Defines the per-pixel terrain properties:

```cpp
struct Material {
    int index;           // 0-255
    float friction;
    float density;
    float particle_damping_factor;
    float destruction;
    int particle_pass;   // 0 = solid, 1 = passable
    // ... more physics properties
};
```

### Object Grid (`Goop/object_grid.h`)

Spatial partitioning system (`Grid`) for efficient collision and rendering queries.
Dual-linked-list per cell with 32×32 pixel squares (configurable via `shift = 5`).
Supports 10 collision layers × 10 render layers.

Key types:
- `Grid` — the spatial container
- `List<BaseObject>` — intrusive linked list (both singly-linked for iteration and doubly-linked for cell ordering)
- `area_iterator` — iterates over objects in a rectangular area
- Guard nodes at each cell for fast insertion

### Options (`Goop/game.h`)

`Options` struct holds all game physics and balance variables, exposed to the
console via `registerInConsole()`. Key variables:

| Variable | Default | Purpose |
|---|---|---|
| `worm_maxSpeed` | — | Speed cap |
| `worm_acceleration` | — | Ground acceleration |
| `worm_gravity` | — | Gravity force |
| `worm_jumpForce` | — | Jump impulse |
| `worm_friction` | — | Ground friction |
| `ninja_rope_*` | — | Ninja rope physics |

---

## Entity Hierarchy

### BaseObject (`Goop/base_object.h`)

Root of all game entities. Inherits `LuaObject` for Lua scripting integration.

```
BaseObject (LuaObject)
├── pos, spd (Vec)               — position and velocity (read-only convention)
├── nextS_, nextD_, prevD_       — intrusive list pointers (for Grid)
├── cellIndex_                   — spatial grid cell
├── deleteMe                     — flagged for removal
├── luaData (LuaReference)      — per-object Lua data table
├── m_owner (BasePlayer*)        — owning player
│
├── virtual think()              — per-frame logic
├── virtual draw(Viewport*)      — per-frame rendering
├── virtual damage()             — apply damage
├── virtual remove()             — request removal
├── virtual removeRefsToPlayer() — cleanup on player disconnect
│
├── Particle                     — bullets, debris, effects
├── Worm (via BaseWorm)          — player-controlled character
├── Explosion                    — area damage + terrain deformation
├── NinjaRope                    — grappling hook segments
├── Sound1D                      — positional audio emitter
├── BaseAction                   — scripted action entities
└── (Lua-created objects)
```

### BaseWorm (`Goop/base_worm.cpp` + `base_worm.h`)

The worm character. ~1112 lines. Handles:

- Movement physics (walk, jump, climb, swim, fly)
- Collision with terrain (pixel-perfect via material bitmap)
- Weapon aiming and firing
- Ninja rope attachment
- Health/damage system

### BasePlayer (`Goop/base_player.cpp` + `base_player.h`)

Player controller. ~865 lines. Hierachy:

```
BasePlayer
├── Player           (local human player, player_input.cpp)
├── ProxyPlayer      (network proxy, proxy_player.cpp)
└── PlayerAI         (bot AI, player_ai.cpp)
```

Each owns a `BaseWorm*` and manages input, camera viewport, and HUD.

### Particle (`Goop/particle.cpp` + `particle.h`)

Most numerous entity. Represents bullets, debris, sparks, etc. Configured by a
`PartType` which defines all behavior parameters. ~594 lines.

### Weapon (`Goop/weapon.cpp` + `weapon.h`)

Weapon state for a worm. Tracks ammo, reload, cooldown, fire mode.

### WeaponType (`Goop/weapon_type.cpp` + `weapon_type.h`)

Weapon class definitions loaded from Lua. Each defines:
- `PartType*` for the projectile
- `PartType*` for the explosion
- Fire rate, ammo, recoil, spread

### PartType (`Goop/part_type.cpp` + `part_type.h`)

Particle class definitions. The most heavily parameterised type in the engine.
Controls: sprite animation, physics (gravity/damping/bounce), collision behavior,
damage, lifespan, spawning, rendering style (blend mode, light emission).

### Explosion (`Goop/explosion.cpp` + `explosion.h`)

Area-effect entity. Applies damage + terrain deformation via `Level::applyEffect()`.

---

## Graphics Pipeline

### Gfx (`Goop/gfx.cpp` + `gfx.h`)

Singleton `gfx`. Manages the hybrid rendering pipeline.

| Stage | Description |
|---|---|
| `init()` | Creates SDL window, renderer, 320×240 offscreen buffer |
| Game objects render | Each draws into `gfx.buffer` (a BITMAP) |
| `updateScreen()` | Uploads buffer → SDL_Texture → SDL_RenderTexture |

**SDL3 objects:** `window`, `renderer`, `screenTexture`

### BITMAP (via `allegro_compat.h`)

Custom struct replacing Allegro's `BITMAP`:

```cpp
struct BITMAP {
    int w, h;
    int cl, ct, cr, cb;       // clip rect
    unsigned char** line;     // row pointers
    int format;               // bit depth (8, 16, 32)
    void* pixels;             // contiguous pixel data
    SDL_Surface* sdl_surface; // backing surface
};
```

### Blitters (`Goop/blitters/`)

Software pixel blenders operating on raw BITMAP data. Organised by blend mode:

| File | Purpose |
|---|---|
| `add.cpp` / `add_simd.cpp` | Additive blending |
| `blend.cpp` / `blend_simd.cpp` | Alpha blending |
| `mult.cpp` / `mult_simd.cpp` | Multiplicative blending |
| `macros.h` | Core pixel access macros (`bmp_select`, `bmp_write32`) |
| `context.h` | BlitterContext — pre-computed params for fast blitting |
| `colors.h` | Color space conversions, lookup tables |
| `types.h` | Pixel format descriptors |

### Viewport (`Goop/viewport.cpp` + `viewport.h`)

Camera/viewport system. Each player has a viewport that follows their worm.
Supports smoothing/interpolation for camera movement.

| Method | Purpose |
|---|---|
| `setDestination()` | Bind to a BITMAP + rect |
| `render(BasePlayer*)` | Draw everything visible in this viewport |
| `interpolateTo(Vec, float)` | Smooth camera tracking |

### Sprite (`Goop/sprite.cpp` + `sprite.h`)

Wraps a BITMAP with a pivot point. The pivot defaults to the sprite's
center (`m_xPivot = w/2`, `m_yPivot = h/2`) when not specified, so by
default `Sprite::draw(where, x, y, ...)` renders the sprite **centered**
on `(x, y)` (i.e. the bitmap's top-left is drawn at `(x - w/2, y - h/2)`).
Supports alignment flags (`ALIGN_LEFT | ALIGN_TOP`, etc.) for callers that
need edge-anchored rendering. Drawn via `draw()` or `drawCut()` with a
`BlitterContext`.

> **Coordinate convention note**: dynamic particles (worms, weapons,
> bullets, blood) and map-authored `put_particle` calls all anchor at the
> sprite's own **pivot point** (default = center) as marked in the sprite
> image, following the engine's default pivot.

### SpriteSet (`Goop/sprite_set.cpp` + `sprite_set.h`)

Animated sprite: collection of frames, each a `Sprite*`. Supports playback
direction, speed, and looping.

### Distortion (`Goop/distortion.cpp` + `distortion.h`)

Screen-space distortion effect (heat shimmer, explosion shockwave).

### Font (`Goop/font.cpp` + `font.h`)

Bitmap font system. Fonts are loaded from sprite sets with character layout
data. Supports formatting markup (`\013xx` color codes).

### SDLRenderer (`Goop/menu.h`)

Implements `OmfgGUI::Renderer` for the menu/GUI system. Renders boxes, text,
sprites, and frames onto a `BlitterContext`.

---

## UI Systems

### Console (`Console/`)

Developer console (toggle with tilde). Key classes:

| File | Purpose |
|---|---|
| `console.h/cpp` | Core: variable/command registration, parsing, binding |
| `command.h/cpp` | Command functor wrapper |
| `variables.h/cpp` | Typed variable wrappers with console exposure |
| `bindings.h/cpp` | Key-to-action binding table |
| `consoleitem.h/cpp` | Abstract console item |
| `special_command.h/cpp` | Special command dispatch |

### GConsole (`Goop/gconsole.cpp` + `gconsole.h`)

Game-specific console subclass. Adds:
- Log display with scrolling
- Input line editing
- History navigation
- Auto-completion
- Console rendering to BITMAP

### OmfgGUI (`GUI/`)

XML/GSS-driven GUI framework. Key components:

| File | Purpose |
|---|---|
| `omfggui.h` | Namespace entry point |
| `detail/context.h/cpp` | Window context, event dispatch |
| `detail/wnd.h/cpp` | Base window/widget class |
| `detail/renderer.h/cpp` | Abstract renderer interface |
| `detail/list.h/cpp` | List/combobox widget |
| `detail/button.h/cpp` | Button widget |
| `detail/edit.h/cpp` | Text edit widget |
| `detail/check.h/cpp` | Checkbox widget |
| `detail/label.h/cpp` | Label widget |
| `detail/group.h/cpp` | Group/panel widget |
| `detail/llist.h` | Linked list templates for widget tree |
| `detail/gss.h/cpp` | GSS parser and style engine |
| `detail/xml.h/cpp` | XML layout parser |
| `lua/bindings-gui.h/cpp` | Lua bindings for GUI |

### Menu (`Goop/menu.cpp` + `menu.h`)

Game-specific menu integration. Provides:
- `GContext` — OmfgGUI context with game event handling
- `GusanosFont` — adapter between game Font and GUI BaseFont
- `GusanosSpriteSet` — adapter between game SpriteSet and GUI BaseSpriteSet
- `SDLRenderer` — renders GUI widgets onto `gfx.buffer`
- Resource locators: `xmlLocator`, `gssLocator`

---

## Network System

### Network (`Goop/network.cpp` + `network.h`)

Singleton `network`. Full ENet-backed implementation via `Net/` compatibility layer.

| Method | Role |
|---|---|
| `init()` / `shutDown()` | Lifecycle |
| `host()` / `connect()` | Session management |
| `update()` | Process incoming data |
| `disconnect()` | Session teardown |
| `kick()` / `ban()` | Player management |
| `fetchServerList()` | HTTP-based server discovery |

### network_compat.h / `Net/`

ZoidCom API compatibility layer backed by ENet. All types and enums are preserved
from the original ZoidCom API so game code requires no rewriting.

| Component | File | Purpose |
|---|---|---|
| `ZCom_BitStream` | `Net/net_bitstream.h/cpp` | Bit-level read/write (int, float, string, bool, nested streams) |
| `ZCom_Address` | `Net/net_address.h/cpp` | ENet address wrapper |
| `ZCom_Control` | `Net/net_control.h/cpp` | Connection management, ENet event dispatch, peer/address maps |
| `ZCom_Node` | `Net/net_node.h/cpp` | Networked object registration, replication setup, event queue |
| `ZCom_Replicator` | `Net/net_replicator.h` | Base class; numeric, boolean, string replicators with change-detection |
| `eZCom_*` enums | `Net/net_types.h` | Send modes, node roles, event types, connection results, close reasons |
| Type aliases | `Net/net_types.h` | `ZCom_ConnID`, `ZCom_NodeID`, `ZCom_ClassID`, `zU8`, `zU32`, etc. |

### Client / Server (`Goop/client.cpp` + `client.h`, `server.cpp` + `server.h`)

`Client` and `Server` inherit from `ZCom_Control` (implemented in `Net/`) and
override virtual callback methods with game-specific logic: connection requests,
data delivery, Zoid level transitions, player creation, and node announcements.

### Terrain Destruction Replication (`Goop/game.cpp`, `Goop/level.cpp`)

Terrain destruction rides on the `Game` *unique* node (registered with
`registerNodeUnique`), which the ENet compat layer announces to clients via
`MSG_NODE_ANNOUNCE_UNIQUE` (see `Net/net_control.cpp`). Two event kinds share the
Game node's event channel:

- `eHole` — live destruction during play. The authority broadcasts it
  (`sendEvent` with `ZCOM_REPRULE_AUTH_2_ALL`) from `Game::applyLevelEffect`;
  proxies decode and apply it via `Level::applyEffect`.
- `eTerrainSnapshot` — full 1bpp RLE destruction mask sent to a late-joining
  client on its `eZCom_EventInit` (authority → that peer via `sendEventDirect`),
  so it catches up to the current terrain state without replaying history.

The destruction mask + RLE codec live on `Level` (`markDestroyed` /
`encodeDestructionMaskRLE` / `applyDestructionMaskRLE`).

### Encoding (`Goop/encoding.h`)

Bit-level serialization utilities for network streaming:
- `VectorEncoding` — quantized Vec serialization (with sub-pixel accuracy)
- `DiffVectorEncoding` — delta-encoded Vec serialization
- Elias gamma and delta coding for variable-length integers
- `signedToUnsigned` / `unsignedToSigned` — zigzag encoding

### Replicators (`Goop/*replicator*`)

Data replication helpers for networked objects. Work with the `ZCom_Replicator`
base class from `Net/net_replicator.h`, which provides change-detection and
rule-based broadcasting (AUTH_2_ALL, OWNER_2_AUTH, etc.).

| File | Purpose |
|---|---|
| `posspd_replicator.h/cpp` | Position + speed replication |
| `vector_replicator.h/cpp` | Generic vector replication |
| `stl_str_replicator.h/cpp` | String replication |
| `net_bitstream.h/cpp` | Bit-level network I/O |
| `net_worm.h/cpp` | Worm network state |

### `Net/` — ENet Implementation

The `Net/` directory contains the ENet-backed ZoidCom API compatibility layer.
This is a separate build target (`libomfgnet.a`) linked by the main game binary.

| File | Purpose |
|---|---|
| `SConscript` | Build target — `libomfgnet.a` |
| `network_compat.h` | Umbrella header — includes all `net_*.h` files |
| `net_types.h` | Standalone type definitions (`ZCom_ConnID`, `eZCom_*` enums, replication constants) — no Boost or ENet dependency |
| `net_bitstream.h/cpp` | `ZCom_BitStream` — bit-level serialization with variable-length int encoding |
| `net_address.h/cpp` | `ZCom_Address` — wraps `ENetAddress` |
| `net_replicator.h` | `ZCom_Replicator` base class with `pack()`/`unpack()` virtuals; derived classes for bool, numeric, string, and memblock replication |
| `net_node.h/cpp` | `ZCom_Node` — per-object network state; manages replicators, auto-replication entries, and event queue; handles `beginReplicationSetup`/`endReplicationSetup`/`packAllReplicators`/`unpackAllReplicators` |
| `net_control.h/cpp` | `ZCom_Control` — connection manager; owns `ENetHost`; handles peer map, address map, class registration, node registration; dispatches ENet events to virtual callbacks overridden by `Client`/`Server` |
| `tests/` | Unit tests for bitstream, replication, broadcast, events, file transfer, movement, and zoid level |

### Updater (`Goop/updater.cpp` + `updater.h`)

File transfer manager. Handles level/mod downloads from server to clients.

---

## Audio System

### Sfx (`Goop/sfx.cpp` + `sfx.h`)

Singleton `sfx`. Channels, volume control, listener management.

| Method | Purpose |
|---|---|
| `init()` / `shutDown()` | Lifecycle (SDL3_mixer init) |
| `think()` | Update 3D positions, cull finished sounds |
| `setChanObject()` | Attach sound channel to game object |
| `newListener()` / `freeListener()` | 3D audio listener pool |
| `volumeChange()` | React to volume CVar changes |

### Sound (`Goop/sound.cpp` + `sound.h`)

Sound sample loading and playback. Manages `Mix_Chunk` resources.

### Sound1D (`Goop/sound1d.cpp` + `sound1d.h`)

Positional audio emitter game object. Calculates pan/volume based on
distance from each viewport's listener.

### fmod_compat (`Goop/fmod_compat.h` + `fmod_compat.cpp`)

FMOD → SDL3_mixer adapter. Maps FMOD types:

```
FSOUND_SAMPLE  → Mix_Chunk*
FSOUND_STREAM  → Mix_Music*
FSOUND_CHANNEL → int
```

---

## Scripting System

### LuaJIT (`luaapi/`)

LuaJIT 2.x linked via pkg-config. The C API is compatible with Lua 5.1.

### Lua API Wrapper (`luaapi/luaapi/`)

| File | Purpose |
|---|---|
| `context.h/cpp` | `LuaContext` — Lua state wrapper, stack ops, function calls |
| `types.h` | `LuaReference`, type traits |
| `macros.h` | Lua stack manipulation macros for binding generation |
| `classes.h` | Class binding helpers |

### GLua (`Goop/glua.h` + `glua.cpp`)

Game Lua glue. Provides:
- `LuaObject` — base class for Lua-scripted objects (ref counting via `makeReference()` / `deleteThis()`)
- `LuaCallbacks` — pre-defined callback points bound from Lua scripts:
  - `atGameStart`, `afterRender`, `afterUpdate`
  - `wormRender`, `wormDeath`, `wormRemoved`
  - `playerUpdate`, `playerInit`, `playerRemoved`
  - `localplayerInit`, `localplayerEvent`, `localplayerEventAny`
  - `gameNetworkInit`, `playerNetworkInit`, `gameError`
  - `transferUpdate`, `transferFinished`, `networkStateChange`
- `EACH_CALLBACK` macro for iteration

### Lua Bindings (`Goop/lua/`)

| File | Binds |
|---|---|
| `bindings-game.h/cpp` | Game, Level, Options, Network, Console |
| `bindings-gfx.h/cpp` | Gfx, BITMAP, drawing functions |
| `bindings-math.h/cpp` | Vec, Angle math operations |
| `bindings-network.h/cpp` | Network events, nodes |
| `bindings-objects.h/cpp` | Particle, Worm, Player constructors/properties |
| `bindings-resources.h/cpp` | SpriteSet, Font, Sound loading |
| `bindings.h/cpp` | Master binding registration |

### Script (`Goop/script.h` + `script.cpp`)

Lua script resource. Loaded from files via `ResourceLocator<Script>`.
Supports lazy evaluation via `LazyScript`.

---

## Input System

### Keyboard (`Goop/keyboard.cpp` + `keyboard.h`)

SDL3 event-based keyboard handling. Maps SDL scancodes → Allegro key constants.
Tracks key state in `key[]` array (compatible with legacy code). Handles
`SDL_EVENT_QUIT`.

### Mouse (`Goop/mouse.cpp` + `mouse.h`)

SDL3 mouse state. Tracks `mouse_x`, `mouse_y`, `mouse_b`, `mouse_z`. Uses
`SDL_PumpEvents()` + `SDL_GetMouseState()` each frame.

### Player Input (`Goop/player_input.cpp` + `player_input.h`)

Maps raw keyboard/mouse state → game actions (aim, fire, jump, weapon select,
ninja rope). Supports bindings from console.

---

## HTTP & Network I/O

### HTTP (`http/http.cpp` + `http.h`)

Minimal HTTP client using SDL3's socket API. Used for:
- Server list fetching (`fetchServerList()`)
- Optional resource downloads

### Sockets / TCP (`http/sockets.cpp` + `sockets.h`, `tcp.cpp` + `tcp.h`)

Low-level networking primitives built on SDL3 net: socket creation, connect,
send, recv, resolve.

---

## Utility

### Vec / Angle (`Utility/util/vec.h`, `Utility/util/angle.h`)

2D vector math library. Types:
- `Vec` — float x,y
- `IVec` — int x,y
- `Angle` / `AngleDiff` — direction with wrapping

### Rect (`Utility/util/rect.h`)

2D rectangle utilities (intersection, containment, union).

### Log (`Utility/util/log.cpp` + `log.h`)

Logging system with severity levels.

### Text (`Utility/util/text.cpp` + `text.h`)

String utilities: case-insensitive compare, splitting, formatting.

### Math Functions (`Utility/util/math_func.h`)

`rndInt()`, `rndFloat()`, `clamp()`, `lerp()`.

### BitArray (`Utility/util/bitarray.h`)

Compressed bit array for network state.

### Cache (`Utility/util/cache.h`)

Generic LRU-ish cache template.

### StringBuild (`Utility/util/stringbuild.h`)

Efficient string builder.

### Trace (`Utility/util/trace.h`)

Debug trace/assert macros.

---

## Tools

### lighter (`lighter/`)

Standalone level editor tool (separate SConscript target). Shares `culling.h`,
basic `Level`, `Material`, and level loaders. Renders modified version of game
code.

### liero2gus (`liero2gus/`)

Command-line converter: Liero / LieroX level files → Gusanos format.

### loadpng (`loadpng/`)

PNG loading/saving via libpng (standalone, not game-specific).

### parsergen (`parsergen/`)

Parser generator tool. Takes `.pg` grammar files → `.h.re` → (re2c) → `.h` parser.
Used by Console grammar and OmfgScript parser.

### OmfgScript (`OmfgScript/`)

GSS (GUI Style Sheet) parser. Uses a grammar file processed by parsergen + re2c.
- `omfg_script_parser.h` — generated parser (1264 lines)
- `omfg_script.cpp/h` — script evaluation, style resolution

---

## Default Resources (`default/`)

Game data directory. Contains:
- `default/` — base mod
- `autoexec.cfg` — startup console script
- Sprites, levels, sounds, fonts, GUI XML/GSS files