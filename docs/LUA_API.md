# Lua API Reference

## Overview

Gusanos uses **Lua 5.1** as its scripting language for mods, particle types,
weapons, and game logic. The engine embeds Lua via the `LuaContext` class
(`lua51/luaapi/context.h`) and exposes a rich API through C function bindings.

Mod scripts live in the mod directory and are loaded by the engine at startup
or on demand through resource loaders (`.part`, `.wpn`, `.script` files).

## Key Architecture

| Component | Header | Purpose |
|---|---|---|
| `LuaContext` | `lua51/luaapi/context.h` | Wraps `lua_State*`; provides stack ops, ref tracking, serialization |
| `LuaCallbacks` | `Goop/glua.h` | ~30 named callback points fired from the game loop |
| `LuaObject` | `Goop/glua.h` | Base class for Lua-visible game objects (ref-counted lifetime) |
| Class/Method macros | `lua51/luaapi/macros.h`, `classes.h` | `CLASS()`, `METHODC()`, `CLASSM()` for binding C++ types |
| `LuaBindings` | `Goop/lua/bindings.*` | All C→Lua binding registration |

### Global Lua Objects

The engine exposes two special Lua tables with metatable behavior:

| Table | Behavior |
|---|---|
| `bindings` | `__newindex` hook: assigning a function binds it to a key action |
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

#### `console_key_for_action(action)`
Returns the SDL3 key name bound to a console action string.

#### `console_action_for_key(key)`
Returns the console action bound to a SDL3 key name.

#### `console_bind(binding_string)`
Binds a key from Lua. Equivalent to console `BIND` command.
```lua
console_bind("F1 SCREENSHOT")
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

A boolean global `DEDSERV` is set to `true` on dedicated server builds, `false`
otherwise. Use for conditional rendering/input code.

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

The engine provides a typed event system for network-synchronized custom events:

### Event Registration

```lua
-- Register a network game event
gameEvent = NetworkGameEvent:new("myEvent")

-- Register a network player event
playerEvent = NetworkPlayerEvent:new("myPlayerEvent")

-- Register a network worm event
wormEvent = NetworkWormEvent:new("myWormEvent")
```

### Event Sending

```lua
-- gameEvent:send([data], [connection], [mode], [rules])
gameEvent:send("hello")  -- Send to all

-- playerEvent:send(player, [data], [connection], [mode], [rules])
playerEvent:send(somePlayer, data)

-- wormEvent:send(worm, [data], [connection], [mode], [rules])
wormEvent:send(someWorm, data)
```

Parameters:
- `data` — optional string payload
- `connection` — target connection ID (default: all)
- `mode` — send mode enum (default: reliable)
- `rules` — delivery rules

### Event Receiving

Events are received through the callback system. Register a handler:

```lua
addEventHandler("gameNetworkInit", function()
    registerNetworkHandler(myEvent, function(data, connection)
        print("Got network event:", data)
    end)
end)
```

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

PartType objects can define events and timers in their `.part` Lua scripts:

```lua
-- Events are triggered by engine conditions
creation = function(self)
    print("Particle created at", self:pos())
end

groundCollision = function(self)
    print("Particle hit the ground")
end

death = function(self)
    print("Particle destroyed")
end

-- Timers fire repeatedly
timer{
    timeout = 30,  -- Ticks before first fire
    interval = 60, -- Repeat interval (nil = one-shot)
    func = function(self)
        self:push(0, -1)  -- Float upward
    end
}

-- Detect ranges trigger when worms enter a radius
detect{
    range = 50,
    func = function(self, worm)
        print("Worm detected at", worm:pos())
    end
}
```

Events available in PartType definitions:

| Event | Fires when |
|---|---|
| `creation` | Particle is spawned |
| `death` | Particle is destroyed |
| `groundCollision` | Particle hits the terrain |

### Custom Events

PartType also supports custom named events for scripts:

```lua
customEvent("explode", function(self)
    -- Custom behavior
end)
```

## TimerEvent (`Goop/events.h`)

Weapon scripts use `TimerEvent` for timed behavior:

```lua
-- In a weapon .wpn script:
timer{
    timeout = 10,   -- Initial delay in ticks
    interval = 20,  -- Repeating interval (nil for one-shot)
    func = function(self)
        -- self = weapon instance
    end
}
```

Weapons have three timer lists:
- `timer` — general timers
- `activeTimer` — active weapon timers
- `shootTimer` — shooting-sequence timers

## Lua Callback System

### Callback Reference (`Goop/glua.h`)

Callbacks are named points in the game loop where Lua functions can be
registered. All callback names are exposed in the `addEventHandler()` Lua
function.

| Callback Name | Fired When | Parameters |
|---|---|---|
| `atGameStart` | Game starts | — |
| `afterRender` | After frame render | — |
| `afterUpdate` | After logic update | — |
| `wormRender` | Before a worm renders | `worm` |
| `viewportRender` | Before a viewport renders | `viewport, layer` |
| `wormDeath` | A worm dies | `worm, killer, weapon` |
| `wormRemoved` | A worm is removed | `worm` |
| `playerUpdate` | Player updates each tick | `player` |
| `playerInit` | Player is initialized | `player` |
| `playerRemoved` | Player is removed | `player` |
| `playerNetworkInit` | Player network state initialized | `player` |
| `gameNetworkInit` | Game network state initialized | — |
| `gameEnded` | Game ends | `reason` (EndReason enum) |
| `gameError` | A game error occurred | `error` (Error enum) |
| `localplayerInit` | Local player initialized | `player` |
| `networkStateChange` | Network connection state changed | `state` |

### Local Player Action Callbacks

For each action, indexed from `localplayerEvent` (15):

| Callback Index | Equivalent To |
|---|---|
| `localplayerEvent + 0` | `localplayerEventAny` (any action) |
| `localplayerEvent + 1` | Left action |
| `localplayerEvent + 2` | Right action |
| `localplayerEvent + 3` | Up action |
| `localplayerEvent + 4` | Down action |
| `localplayerEvent + 5` | Fire action |
| `localplayerEvent + 6` | Jump action |
| `localplayerEvent + 7` | Change action |
| `transferUpdate` | Transfer progress update |
| `transferFinished` | Transfer completed |

### Using Callbacks in Lua

```lua
addEventHandler("afterUpdate", function()
    -- Called every game tick
end)

addEventHandler("wormDeath", function(worm, killer, weapon)
    print(worm:name() .. " was killed by " .. (killer and killer:name() or "?") .. (weapon ~= "" and " with " .. weapon or ""))
end)

addEventHandler("gameEnded", function(reason)
    if reason == EndReason.ServerQuit then
        print("Server quit")
    end
end)
```

Callback names are case-insensitive and match the enum names from
`LuaCallbacks` (kebab-case converted to camelCase).

### `addEventHandler(callbackName, function)`

Registers a Lua function to be called at the specified callback point.

```lua
addEventHandler("afterUpdate", myUpdateFunction)
```

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

The global `bindings` table provides programmatic key binding:

```lua
-- Assign a function to a key (overrides any command binding)
bindings["F1"] = function()
    print("F1 pressed!")
    return true  -- Return true = consume the key event
end

-- Or clear a binding
bindings["F1"] = nil
```

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
addEventHandler("atGameStart", function()
    print("Game started!")
end)

addEventHandler("afterUpdate", function()
    for player in game_players() do
        local worm = player:worm()
        if worm then
            local x, y = worm:pos()
            if y > 5000 then
                worm:damage(100, player)
            end
        end
    end
end)

addEventHandler("wormDeath", function(worm, killer, weapon)
    if killer and killer:is_local() then
        print("You killed " .. worm:name() .. (weapon ~= "" and " with " .. weapon or ""))
    end
end)

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

-- Key binding from Lua
bindings["F2"] = function()
    print("F2 pressed!")
    return true
end
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
| `lua51/luaapi/context.h` | `LuaContext` class API |
| `lua51/luaapi/macros.h` | `METHODC`, `CLASS`, `ENUM` macros |
| `lua51/luaapi/types.h` | `LuaReference`, type helpers |

## Compile-Time Guards

| Guard | Scope |
|---|---|
| `DEDSERV` | Omitted from dedicated server builds |
| `NO_DEPRECATED` | Removes deprecated binding aliases |

When `DEDSERV` is true, the global `DEDSERV` Lua variable is `true` and all
rendering/input/audio bindings are omitted.