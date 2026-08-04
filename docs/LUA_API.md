# Lua API Reference

## Overview

Gusanos uses **LuaJIT** (Lua 5.1-compatible) as its scripting language for mods, particle types,
weapons, and game logic. The engine links LuaJIT via `pkg-config` and wraps it with the
`LuaContext` class (`luaapi/luaapi/context.h`) and exposes a rich API through C function bindings.

Mod scripts live in the mod directory and are loaded by the engine at startup
or on demand through resource loaders (`.part`, `.wpn`, `.script` files).

## Key Architecture

| Component | Header | Purpose |
|---|---|---|
| `LuaContext` | `luaapi/luaapi/context.h` | Wraps `lua_State*`; provides stack ops, ref tracking, serialization |
| `LuaCallbacks` | `Goop/glua.h` | ~30 named callback points fired from the game loop |
| `LuaObject` | `Goop/glua.h` | Base class for Lua-visible game objects (ref-counted lifetime) |
| Class/Method macros | `luaapi/luaapi/macros.h`, `classes.h` | `CLASS()`, `METHODC()`, `CLASSM()` for binding C++ types |
| `LuaBindings` | `Goop/lua/bindings.*` | All C→Lua binding registration |

### Global Lua Objects

The engine exposes two special Lua tables with metatable behavior:

| Table | Behavior |
|---|---|
| `bindings` | `__newindex` hook: assigning a function registers it as a Lua callback (see Callback Reference) |
| `console` | `__index`/`__newindex` hooks: reads and writes engine CVars |

## Global Functions

### Control & Registration

#### `console_register_command(name, function)`
Registers a Lua function as a console command. The command is ephemeral
(cleared on map change). Arguments are passed as individual parameters.

```lua
console_register_command("greet", function(name)
    print("Hello, " .. name)
end)
-- In console: greet World → prints "Hello, World"
```

#### `console_register_control(name, function)`
Registers a player control. The function receives `(player, state)` where
`state` is `true` (activated) or `false` (deactivated). It creates
`+P{index}_{name}` / `-P{index}_{name}` console commands for binding.

```lua
console_register_control("lean", function(player, state)
    if state then
        print("Player leaning")
    end
end)
-- Bindable as: BIND A +P0_lean
```

#### `console_key_for_action(action)` → `int`
Returns the key code (integer, indexing the `Keys` table) bound to a console action string, or `nil` if unbound.

#### `console_action_for_key(key)` → `string`
Returns the console action string bound to a key code (integer), or `nil` if unbound.

#### `console_bind(key, action)`
Binds a key code to a console action string. `key` is an integer key code (e.g. `Keys.F1`); `action` is the console command string to run. Equivalent to the console `BIND` command.
```lua
console_bind(Keys.F1, "screenshot")
```

#### `quit()`
Exits the game.

### Connections

#### `connect(address)`
Connects to a server. `address` is a string like `"hostname:port"`.

#### `host()`
Hosts a local game (single-player or server).

#### `map(name)`
Loads a map by name (non-host — set `HOST 1` first or use console).
```lua
map("dm01")
```

### Network

#### `fetch_server_list(handler)`
Fetches the server list from the master server. `handler` is called with an
array of server tables. Each server table has fields `ip`, `title`, `desc`,
`mod`.

```lua
fetch_server_list(function(servers)
    for _, s in ipairs(servers) do
        print(s.title .. " (" .. s.ip .. ")")
    end
end)
```

### Input

#### `clear_keybuf()` (client-only)
Clears the keyboard buffer.

#### `key_name(key_code)` (client-only)
Returns the name of a key code (from the `Keys` table).

### Map Queries

#### `game_get_closest_worm(x, y)`
Returns the Worm object closest to `(x, y)` that is active and line-of-sight
reachable by particles.

#### `map_is_blocked(x1, y1, x2, y2)`
Returns `true` if the line between `(x1, y1)` and `(x2, y2)` is blocked for
particle movement.

#### `map_is_particle_pass(x, y)`
Returns `true` if point `(x, y)` is passable by particles.

### Player Iterators

#### `game_players()`
Returns an iterator for all players in the game. Use with `for`:

```lua
for player in game_players() do
    print(player:name())
end
```

#### `game_local_player(index)`
Returns the local Player object at `index` (0-based), or `nil` if invalid.

#### `game_local_player_name(index)`
Returns the name of the local player at `index`.

#### `game_tick()` → `int`
Returns the current game logic tick. Advances at 100 ticks per second and is
available on both client and server.

#### `game_weapon_name(index)` → `string`
Returns the name of the weapon at the given WeaponType index, or an empty
string if the index is invalid.

### Other

#### `print(...)`
Prints to the engine log and in-game console.

### `DEDSERV` Global

A boolean global `DEDSERV` reflects the runtime `g_dedicated` flag: `true` when
the binary is launched with `--dedicated` (headless server mode), `false`
otherwise. It is no longer a compile-time constant — the same binary serves both
modes. Use for conditional rendering/input code.

### `Keys` Table (client-only)

Maps SDL3 scancode name strings to integer key codes:

```lua
if key == Keys.ESCAPE then
    -- handle escape
end
```

## Game Object Methods

### Object: Base Methods

All game objects (`BaseObject` subclasses: particles, worms, etc.) expose:

#### `object:pos()` → `x, y`
Returns the object's position as two values.

```lua
local x, y = particle:pos()
```

#### `object:set_pos(x, y)`
Sets the object's position.

```lua
particle:set_pos(100, 200)
```

#### `object:spd()` → `vx, vy`
Returns the object's velocity as two values.

#### `object:set_spd(vx, vy)`
Sets the object's velocity.

```lua
object:set_spd(10, 0)  -- Move right
```

#### `object:push(vx, vy)`
Adds to the object's velocity (acceleration).

```lua
object:push(0, -5)  -- Push upward
```

#### `object:data()` → `table`
Returns a Lua table associated with this object for arbitrary script data.

```lua
local data = object:data()
data.hitCount = (data.hitCount or 0) + 1
```

#### `object:player()` → `Player`
Returns the Player object that owns this object.

#### `object:damage(amount[, player])`
Inflicts damage on the object. The optional `player` parameter is the
responsible Player.

```lua
object:damage(50, attacker)
```

#### `object:closest_worm()` → `Worm`
Returns the closest active worm that:
- Is not the owner of the object
- Is visible and active
- Has a straight path not blocked for particles

#### `object:shoot(type, [amount], [speed], [speedVariation], [motionInheritance], [amountVariation], [distribution], [angleOffset], [distanceOffset])`
Fires a projectile of `PartType` `type`. All parameters except `type` are
optional.

```lua
object:shoot(bulletType, 1, 10, 2, 0, 0, 0, 0, 0)
```

### Worm Methods

Worm objects inherit all Object methods plus:

#### `worm:set_angle(angle)`
Sets the worm's angle (in degrees).

```lua
worm:set_angle(45)
```

### Player Methods

Player objects provide:

#### `player:kills()` → `int`
Returns the player's kill count.

#### `player:deaths()` → `int`
Returns the player's death count.

#### `player:damage_dealt()` → `number`
Returns the total damage this player has dealt to other worms.
Server-authoritative: returns `0` on clients.

#### `player:damage_taken()` → `number`
Returns the total damage this player has received from other worms.
Server-authoritative: returns `0` on clients.

#### `player:weapon_kills()` → `table`
Returns a table mapping weapon names to the number of kills made with each
weapon. Server-authoritative: returns an empty table on clients.

#### `player:unique_id()` → `int`
Returns the stable unique ID for this player. The same value is kept across
reconnects, so it can be used to track scoring events in Lua.

#### `player:name()` → `string`
Returns the player's name.

#### `player:team()` → `int`
Returns the player's team number.

#### `player:worm()` → `Worm`
Returns the player's Worm object.

#### `player:is_local()` → `bool`
Returns `true` if this is a local player.

#### `player:data()` → `table`
Returns a per-player data table (similar to `object:data()`).

#### `player:stats()` → `table`
Returns a per-stats data table.

#### `player:say(text)`
Sends a chat message as this player.

```lua
player:say("Hello everyone!")
```

#### `player:select_weapons(weapons)`
Changes the player's weapons. `weapons` is an array of `WeaponType` objects.

```lua
player:select_weapons({rocketLauncher, shotgun})
```

### PartType Methods

PartType objects (particle definitions) provide:

#### `parttype:put([x], [y], [xspd], [yspd], [angle])` → `Particle`
Spawns a particle of this type at `(x, y)` (default 0,0) with optional
velocity and angle. Returns the created Particle object.

```lua
local p = myParticleType:put(100, 200, 0, -5, 90)
```

#### `parttype:destroy()`
Destroys this PartType.

## Network Event System

The engine provides a typed event system for network-synchronized custom events (`Goop/lua/bindings-network.cpp`). Events are created with global constructor functions that take a unique name and a handler function. The handler runs on every peer that receives the event.

### Event Registration

```lua
-- network_game_event(name, handler) -> NetworkGameEvent
-- handler: function(event, data)
gameEvent = network_game_event("myEvent", function(event, data)
    print("Got game event")
end)

-- network_player_event(name, handler) -> NetworkPlayerEvent
-- handler: function(event, player, data)
playerEvent = network_player_event("myPlayerEvent", function(event, player, data)
    print("Got player event for", player:name())
end)

-- network_worm_event(name, handler) -> NetworkWormEvent
-- handler: function(event, worm, data)
wormEvent = network_worm_event("myWormEvent", handler)

-- network_particle_event(name, handler) -> NetworkParticleEvent
-- handler: function(event, particle, data)
particleEvent = network_particle_event("myParticleEvent", handler)
```

`data` is a `ZCom_BitStream` object. Create one with `new_bitstream()` and fill it with `add_int`, `add_string`, `add_bool`, or `dump`; read it back with `get_int`, `get_string`, `get_bool`, or `undump`.

### Event Sending

```lua
-- NetworkGameEvent:send([data[, connection[, mode[, rules]]]])
gameEvent:send(bitstream)  -- rules decide recipients

-- NetworkPlayerEvent:send(player, [data[, ...]])
playerEvent:send(somePlayer, bitstream)

-- NetworkWormEvent:send(worm, [data[, ...]])
wormEvent:send(someWorm, bitstream)

-- NetworkParticleEvent:send(particle, [data[, ...]])
particleEvent:send(someParticle, bitstream)
```

Parameters:
- `data` — optional `ZCom_BitStream` payload
- `connection` — target connection ID; `0` means `rules` decides recipients (default `0`)
- `mode` — `SendMode.ReliableOrdered` (default), `SendMode.ReliableUnordered`, or `SendMode.Unreliable`
- `rules` — who receives the event when `connection` is `0`. A sum of `RepRule` values: `RepRule.Auth2All` (server→all clients), `RepRule.Auth2Owner` (server→owning client), `RepRule.Auth2Proxy` (server→non-owning clients), `RepRule.Owner2Auth` (owning client→server), `RepRule.None`. A common combination is `RepRule.Owner2Auth + RepRule.Auth2All`.

The global `AUTH` is `true` on the server (authority) and `false` on clients.

## Resource Functions

### Mod and Map Iteration

```lua
for mod in mods() do
    print("Mod:", mod)
end

for map in maps() do
    print("Map:", map)
end
```

## Event & Timer System

Particle (`.obj`), explosion (`.exp`), and weapon (`.wpn`) behavior is defined
with the engine's **OmfgScript** format (parsed by `OmfgScript::Parser`), not
Lua. Events are `on <name>(<params>)` blocks containing action commands, e.g.
`on creation()` / `on ground_collision()` / `on timer(delay, delay_var, max_trigger)`.
See [`obj-format.md`](obj-format.md) and [`wpn-format.md`](wpn-format.md) for the
full property and event reference.

Lua is reached from OmfgScript in three ways:

- **`run_script(code)`** action — compiles inline Lua code and calls it with
  `(object, object2)` (see the Action Commands table in obj-format.md).
- **LazyScript properties** — `network_init`, `light_gen`, and `distort_gen`
  are strings evaluated as Lua expressions to a function.
- **Game-loop callbacks** — the global `bindings` table (see the Lua Callback
  System above) fires Lua functions at fixed points in the loop.

### `TimerEvent` (`Goop/timer_event.h`)

`TimerEvent` extends `Event` (`Goop/events.h`) and is created from
`on timer(...)` OmfgScript blocks. It holds `delay`, `delayVariation`,
`triggerTimes`, and `startDelay`.

Particle timers (`Goop/part_type.cpp`):

```
on timer(delay, delay_var, max_trigger)
```

Weapon timers (`Goop/weapon_type.cpp`) add `start_delay` and a separate
`active_timer` event that only ticks while the weapon is active:

```
on timer(delay, delay_var, max_trigger, start_delay)
on active_timer(delay, delay_var, max_trigger, start_delay)
```

The full set of weapon events: `primary_shoot`, `primary_press`,
`primary_release`, `out_of_ammo`, `reload_end`, `timer`, `active_timer`.
The full set of particle events: `creation`, `death`, `ground_collision`,
`timer`, `detect_range`, `custom_event`.

Weapons have three timer lists:
- `timer` — general timers
- `activeTimer` — active weapon timers
- `shootTimer` — shooting-sequence timers

## Lua Callback System

### Callback Reference (`Goop/glua.h`, `Goop/glua.cpp`)

Callbacks are named points in the game loop where Lua functions can be
registered. A callback is registered by assigning a function to the global
`bindings` table using the exact callback name (case-sensitive), e.g.
`bindings.afterUpdate = function() ... end`. The accepted names are matched in
`LuaCallbacks::bind()`.

| Bind Name | Fired When | Parameters | Returns |
|---|---|---|---|
| `afterUpdate` | After each logic update tick | — | — |
| `afterRender` | After each frame render | — | — |
| `wormRender` | Per worm/viewport during render | `x, y, worm, viewport, ownerPlayer` | — |
| `viewportRender` | Per viewport HUD render | `viewport, worm` | — |
| `wormDeath` | A worm dies | `worm, killer, weaponName` (`killer` may be nil) | — |
| `wormRemoved` | A worm is removed | `worm` | — |
| `playerUpdate` | Each tick for every player | `player` | — |
| `playerInit` | A player is added | `player` | — |
| `playerRemoved` | A player is removed | `player` | — |
| `playerNetworkInit` | A player is replicated to a client | `player, connID` | — |
| `gameNetworkInit` | A new client joins | `connID` | — |
| `gameEnded` | The game ends (no new game pending) | `reason` (int, `EndReason`) | — |
| `gameError` | A game error occurs | `error` (int, `Error`) | — |
| `localplayerInit` | Local player initialized | `player` | — |
| `localplayerEvent` | Any local-player action (the "any" handler) | `player, action, pressed` | bool: `true` = consume the input |
| `localplayerLeft` / `localplayerRight` / `localplayerUp` / `localplayerDown` / `localplayerFire` / `localplayerJump` / `localplayerChange` | The matching local-player action | `player, pressed` | bool: `true` = consume the input |
| `transferUpdate` | File-transfer progress | `path, bps, transferred, size` | — |
| `transferFinished` | File transfer completed | — | — |
| `networkStateChange` | Network connection state changed | `state` (`Network` enum) | — |

`atGameStart` exists in the `LuaCallbacks` enum but is **not** registered in
`bind()` and is never fired, so it cannot currently be used.

### Local Player Action Callbacks

Local-player input is surfaced as per-action callbacks (`Goop/player_input.cpp`). Each fires with `(player, pressed)` where `pressed` is `true` on key-down and `false` on key-up. Returning `true` from the handler consumes the input so the engine ignores it. The action order matches the `Player` action enum:

| Bind Name | Action |
|---|---|
| `localplayerLeft` | Left (`Player.Left`) |
| `localplayerRight` | Right (`Player.Right`) |
| `localplayerUp` | Up / aim up (`Player.Up`) |
| `localplayerDown` | Down / aim down (`Player.Down`) |
| `localplayerFire` | Fire weapon (`Player.Fire`) |
| `localplayerJump` | Jump (`Player.Jump`) |
| `localplayerChange` | Change weapon (`Player.Change`) |
| `localplayerEvent` | Any of the above — fires for every action with `(player, action, pressed)` |

The `transferUpdate` and `transferFinished` callbacks (file-transfer progress) are separate; see the table above.

### Using Callbacks in Lua

Register a callback by assigning a function to the `bindings` table. Names are
case-sensitive and must match exactly.

```lua
bindings.afterUpdate = function()
    -- Called every game tick
end

bindings.wormDeath = function(worm, killer, weapon)
    print(worm:name() .. " was killed by " .. (killer and killer:name() or "?") .. (weapon ~= "" and " with " .. weapon or ""))
end

bindings.gameEnded = function(reason)
    if reason == EndReason.ServerQuit then
        print("Server quit")
    end
end

-- Input callback that consumes (blocks) the action
bindings.localplayerJump = function(player, pressed)
    if pressed and someCondition then
        return true  -- engine ignores this jump
    end
end
```

### How Registration Works

The global `bindings` table has a `__newindex` metamethod (`l_bind` in
`Goop/lua/bindings.cpp`) that intercepts every assignment. When you write
`bindings.<name> = func`, the engine looks up `<name>` in
`LuaCallbacks::bind()` (`Goop/glua.cpp`) and stores the function in the
matching callback slot. Unknown names are silently ignored.

A global `bind` function is also exported, but it is the same `__newindex`
handler and expects the `(table, name, func)` metamethod arguments — always use
the `bindings.<name> = func` form instead.

## Enumerations

### `EndReason`

| Value | Description |
|---|---|
| `EndReason.ServerQuit` | Server voluntarily quit |
| `EndReason.ServerChangeMap` | Server changed map |
| `EndReason.Kicked` | Client was kicked |
| `EndReason.IncompatibleProtocol` | Version mismatch |
| `EndReason.IncompatibleData` | Data CRC mismatch |

### `Error`

| Value | Description |
|---|---|
| `Error.None` | No error |
| `Error.MapNotFound` | Map file not found |
| `Error.MapLoading` | Map failed to load |
| `Error.ModNotFound` | Mod not found |
| `Error.ModLoading` | Mod failed to load |

### `Player` (Action Enum)

| Value | Description |
|---|---|
| `Player.Left` | Left movement |
| `Player.Right` | Right movement |
| `Player.Up` | Up/Aim up |
| `Player.Down` | Down/Aim down |
| `Player.Fire` | Fire weapon |
| `Player.Jump` | Jump |
| `Player.Change` | Change weapon |

## The `console` Table

The global `console` table provides read/write access to all engine CVars:

```lua
-- Read a variable
local vol = console.SFX_VOLUME

-- Write a variable
console.SFX_VOLUME = 200

-- Setting an unknown name creates an alert (via the __newindex metatable)
```

## The `bindings` Table

The global `bindings` table is the registry for **Lua callbacks** (see the
Lua Callback System above). Assigning a function to a recognized callback name
registers it; the `__newindex` metamethod routes the assignment to
`LuaCallbacks::bind()`:

```lua
bindings.afterUpdate = function() ... end   -- register a callback
```

Only the callback names recognized by `LuaCallbacks::bind()` have any effect;
assigning any other name (e.g. `bindings["F1"] = ...`) is silently ignored.

For **key bindings** (mapping a key to a console command), use
`console_bind(key, action)` instead — the `bindings` table is not for keys.

## Persistence (`persistence` table)

A shadow table for persistent data storage across sessions:

```lua
-- Save a table key
persistence.myData = { highScore = 1000 }

-- Load it back (in a later session)
local data = persistence.myData
-- data == { highScore = 1000 }
```

Data is saved to `persistance/<key>.lpr` files relative to the mod directory.
Only keys matching `[A-Za-z0-9_-]+` are allowed.

## Mod Script Structure

A mod typically provides scripts in its data directory:

```
mods/mymod/
├── script/          # Lua scripts loaded via Script resources
│   ├── init.lua     # Run on mod load
│   └── game.lua     # Game logic
├── parts/           # Particle type definitions (.part files)
│   ├── bullet.part
│   └── explosion.part
└── weapons/         # Weapon type definitions (.wpn files)
    ├── rocket.wpn
    └── shotgun.wpn
```

### Particle Type Script (`.part`)

```lua
-- .part files define particle/object behavior
-- Properties set before the script block:
-- gravity = 0.01
-- bounceFactor = 0.5
-- groundFriction = 0.1
-- colour = [255, 0, 0]
-- sprite = "bullet"
-- alpha = 255
-- blender = "add"
-- renderLayer = 5

creation = function(self)
    print("Particle created!")
end
```

### Weapon Type Script (`.wpn`)

```lua
-- .wpn files define weapon behavior
-- Properties:
-- ammo = 10
-- reloadTime = 30
-- laserSightColour = [255, 0, 0]
-- laserSightRange = 500

primaryShoot = function(self, player, worm)
    local bullet = bulletType:put(worm:pos())
    if bullet then
        bullet:set_spd(10, 0)
    end
end
```

Weapon event hooks:

| Event | Parameters | Description |
|---|---|---|
| `primaryShoot` | `self, player, worm` | Primary fire pressed |
| `primaryPressed` | `self, player, worm` | Primary fire held |
| `primaryReleased` | `self, player, worm` | Primary fire released |
| `outOfAmmo` | `self, player, worm` | Weapon ran out of ammo |
| `reloadEnd` | `self, player, worm` | Reload completed |

## Example: Full Mod Script

```lua
-- script/init.lua
bindings.localplayerInit = function(player)
    print("Local player initialized:", player:name())
end

bindings.afterUpdate = function()
    for player in game_players() do
        local worm = player:worm()
        if worm then
            local x, y = worm:pos()
            if y > 5000 then
                worm:damage(100, player)
            end
        end
    end
end

bindings.wormDeath = function(worm, killer, weapon)
    if killer and killer:is_local() then
        print("You killed " .. worm:name() .. (weapon ~= "" and " with " .. weapon or ""))
    end
end

-- Custom console command
console_register_command("show_scores", function()
    for player in game_players() do
        print(player:name() .. " - Kills: " .. player:kills() ..
              " Deaths: " .. player:deaths())
    end
end)

-- Custom control
console_register_control("dance", function(player, state)
    if state then
        local worm = player:worm()
        if worm then
            worm:set_angle(180)
        end
    end
end)

-- Key binding from Lua: register a console command, then bind a key to it
console_register_command("f2_action", function()
    print("F2 pressed!")
end)
console_bind(Keys.F2, "f2_action")
```

## File References

| File | Contents |
|---|---|
| `Goop/lua/bindings.cpp` | Master binding registration table |
| `Goop/lua/bindings-game.cpp` | Game/Player callbacks, `game_players()`, `map_is_blocked` |
| `Goop/lua/bindings-gfx.cpp` | Screen/Viewport drawing bindings (client-only) |
| `Goop/lua/bindings-math.cpp` | `Vec`, `Angle`, math utility bindings |
| `Goop/lua/bindings-network.cpp` | Network event types (`NetworkGameEvent`, etc.) |
| `Goop/lua/bindings-objects.cpp` | Object, Worm, PartType methods |
| `Goop/lua/bindings-resources.cpp` | `mods()`, `maps()`, resource loading |
| `Goop/glua.h` | `LuaCallbacks` enum, `LuaObject` base class |
| `Goop/glua.cpp` | Callback firing implementation |
| `luaapi/luaapi/context.h` | `LuaContext` class API |
| `luaapi/luaapi/macros.h` | `METHODC`, `CLASS`, `ENUM` macros |
| `luaapi/luaapi/types.h` | `LuaReference`, type helpers |

## Compile-Time Guards

| Guard | Scope |
|---|---|
| `NO_DEPRECATED` | Removes deprecated binding aliases |

The `DEDSERV` macro is no longer defined at compile time, so the
`#ifndef DEDSERV` wrappers around rendering/input/audio bindings always
compile — those bindings are registered in every build. The global `DEDSERV`
Lua variable instead reflects the runtime `g_dedicated` flag (true when
launched with `--dedicated`, false otherwise); see the `DEDSERV` Global section
above.