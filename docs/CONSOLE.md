# Console System Reference

## Overview

The Gusanos console is an in-game Lua-interactive command shell. It provides
access to all engine variables and commands, and serves as the primary
configuration and debugging interface.

## Key Files

| File | Purpose |
|---|---|
| `Console/console.h` / `.cpp` | Core `Console` class: command/variable registry, alias storage, parsing, execution |
| `Console/consoleitem.h` | `ConsoleItem` base class |
| `Console/command.h` / `.cpp` | `TCommand` class — a named command backed by a function |
| `Console/variables.h` / `.cpp` | `TVariableBase` hierarchy — typed console variables with optional callbacks |
| `Console/alias.h` / `.cpp` | `Alias` class — user-defined command aliases |
| `Console/special_command.h` / `.cpp` | `SpecialCommand` — commands with custom argument parsing |
| `Goop/gconsole.h` / `.cpp` | `GConsole` — the in-game console UI (drawing, input buffer, history) |
| `Goop/gusanos.cpp` | Bootstrap; registers `CL_SHOWFPS`, `CL_SHOWDEBUG`, `QUIT` |

## Console Variables (CVars)

Console variables are typed, named values registered by the engine or mods.
They can be read and written from the console or Lua.

### Registration Pattern

Each subsystem registers its variables in a `registerInConsole()` method:

```cpp
console.registerVariables()
    ("VAR_NAME", &backingField, defaultValue)
    ("VAR_NAME", &backingField, defaultValue, callback)
;
```

An `EnumVariable` with a set of named integer choices can also be registered:

```cpp
console.registerVariable(
    new EnumVariable("VID_FILTER", &m_filter, NO_FILTER, filterMap, callback));
```

### Built-in Console Variables

#### Graphics (`Goop/gfx.cpp`)

| Variable | Type | Default | Description |
|---|---|---|---|
| `VID_FULLSCREEN` | int | 0 | Toggle fullscreen mode (changes take effect on restart) |
| `VID_DOUBLERES` | int | 0 | Double internal resolution (320x240 → 640x480) |
| `VID_VSYNC` | int | 1 | Enable vertical sync |
| `VID_CLEAR_BUFFER` | int | 0 | Clear the offscreen buffer each frame |
| `VID_BITDEPTH` | int | 32 | Color depth (32-bit forced in SDL3 build) |
| `VID_DISTORTION_AA` | int | 1 | Distortion anti-aliasing |
| `VID_HAX_WORMLIGHT` | int | 1 | Enable worm light hack |
| `VID_FILTER` | enum | PIXELART | Video filter/scaling algorithm (see options below) |

**VID_FILTER options** (enum defined in `Goop/gfx.cpp`):

| Value | Description |
|---|---|
| `NEAREST` | Nearest-neighbor (no filtering) |
| `LINEAR` | Bilinear (smooth) filtering |
| `PIXELART` | Pixel-art scaling — nearest with improved scaling (default) |
| `XBRZ2X` | xBRZ 2× upscaling |
| `XBRZ3X` | xBRZ 3× upscaling |
| `XBRZ4X` | xBRZ 4× upscaling |

#### Audio (`Goop/sfx.cpp`)

| Variable | Type | Default | Description |
|---|---|---|---|
| `SFX_VOLUME` | int | 255 | Master volume (0–255) |
| `SFX_LISTENER_DISTANCE` | int | 20 | 3D listener Z-distance for pan/volume calculations |

#### Network (`Goop/network.cpp`)

| Variable | Type | Default | Description |
|---|---|---|---|
| `NET_SERVER_PORT` | int | 9898 | Server listening port |
| `NET_SERVER_NAME` | string | "Unnamed server" | Server name (appears in server list) |
| `NET_SERVER_DESC` | string | "" | Server description |
| `NET_REGISTER` | int | 1 | Register with master server (1 = yes) |
| `NET_MASTER_SERVER` | string | "comser.liero.org.pl" | Master server hostname for server listing |
| `NET_SIM_LAG` | int | 0 | Simulated latency in ms |
| `NET_SIM_LOSS` | float | -1.0 | Simulated packet loss rate |
| `NET_UP_LIMIT` | int | 10000 | Upload bandwidth limit (bytes/sec) |
| `NET_DOWN_BPP` | int | 200 | Download bytes per packet |
| `NET_DOWN_PPS` | int | 20 | Download packets per second |
| `NET_CHECK_CRC` | int | 1 | Enable CRC checking for level/mod sync |
| `NET_LOG` | int | 0 | Log ZoidCom network debug output |
| `NET_AUTODOWNLOADS` | int | 1 | Auto-download missing levels/mods from server |

#### Game Physics (`Goop/game.cpp` — `Options`)

| Variable | Type | Default | Description |
|---|---|---|---|
| `SV_NINJAROPE_SHOOT_SPEED` | float | 2.0 | Ninja rope shoot speed |
| `SV_NINJAROPE_PULL_FORCE` | float | 0.031 | Ninja rope pull force |
| `SV_NINJAROPE_START_DISTANCE` | float | 249.0 | Ninja rope start distance (pixels) |
| `SV_NINJAROPE_MAX_LENGTH` | float | 2000.0 | Ninja rope max length (pixels) |
| `SV_WORM_MAX_SPEED` | float | 0.45 | Worm maximum movement speed |
| `SV_WORM_ACCELERATION` | float | 0.03 | Worm ground acceleration |
| `SV_WORM_AIR_ACCELERATION_FACTOR` | float | 1.0 | Air acceleration multiplier |
| `SV_WORM_FRICTION` | float | ~0.078 | Worm ground friction |
| `SV_WORM_AIR_FRICTION` | float | 1.0 | Worm air friction |
| `SV_WORM_GRAVITY` | float | 0.009 | Worm gravity |
| `SV_WORM_DISABLE_WALL_HUGGING` | int | 0 | Disable wall hugging behavior |
| `SV_WORM_BOUNCE_QUOTIENT` | float | 0.333 | Worm bounce quotient |
| `SV_WORM_BOUNCE_LIMIT` | int | 2 | Worm bounce limit |
| `SV_WORM_JUMP_FORCE` | float | 0.6 | Worm jump force |
| `SV_WORM_WEAPON_HEIGHT` | int | 5 | Weapon offset height from worm center |
| `SV_WORM_HEIGHT` | int | 9 | Worm collision box height |
| `SV_WORM_WIDTH` | int | 3 | Worm collision box width |
| `SV_WORM_MAX_CLIMB` | int | 4 | Max climbable step height |
| `SV_WORM_BOX_RADIUS` | float | 2.0 | Worm collision circle radius |
| `SV_WORM_BOX_TOP` | float | 3.0 | Worm collision box top offset |
| `SV_WORM_BOX_BOTTOM` | float | 4.0 | Worm collision box bottom offset |
| `SV_MAX_RESPAWN_TIME` | int | -1 | Max respawn time (-1 = disabled) |
| `SV_MIN_RESPAWN_TIME` | int | 100 | Min respawn time (ticks) |
| `SV_TEAM_PLAY` | int | 0 | Enable team play mode |
| `HOST` | int | 0 | Host a game (set by `/host` command) |
| `SV_MAX_WEAPONS` | int | 5 | Max weapons a player can carry |
| `CL_SPLITSCREEN` | int | 0 | Enable split-screen mode |
| `RCON_PASSWORD` | string | "" | Remote console password |
| `CL_SHOW_MAP_DEBUG` | int | 0 | Show map debug overlay |
| `CL_SHOW_DEATH_MESSAGES` | bool | true | Show death messages on screen |
| `CL_LOG_DEATH_MESSAGES` | bool | false | Log death messages to console |

#### Console UI (`Goop/gconsole.cpp`)

| Variable | Type | Default | Description |
|---|---|---|---|
| `CON_SPEED` | int | 4 | Console slide animation speed |
| `CON_HEIGHT` | int | 120 | Console draw height in pixels |
| `CON_FONT` | string | "minifont" | Console font resource name (client-only) |

#### Client (`Goop/gusanos.cpp`)

| Variable | Type | Default | Description |
|---|---|---|---|
| `CL_SHOWFPS` | int | 1 | Show FPS counter |
| `CL_SHOWDEBUG` | int | 0 | Show debug info overlay |

#### Per-Player Options (`Goop/player_options.cpp`)

Registered for each local player slot (P0, P1, ...):

| Variable | Type | Default | Description |
|---|---|---|---|
| `P{index}_AIM_ACCEL` | AngleDiff | 0.17 | Aim acceleration rate |
| `P{index}_AIM_FRICTION` | float | ~0.078 | Aim friction |
| `P{index}_AIM_SPEED` | AngleDiff | 1.7 | Max aim speed |
| `P{index}_VIEWPORT_FOLLOW` | float | 0.1 | Viewport follow factor |
| `P{index}_ROPE_ADJUST_SPEED` | float | 0.5 | Rope adjustment speed |
| `P{index}_NAME` | string | "GusPlayer" | Player name |

## Console Commands

### Registration Pattern

```cpp
console.registerCommands()
    ("CMD_NAME", functionHandler)
    ("CMD_NAME", functionHandler, completer)
    ("CMD_NAME", functionHandler, true)  // true = ephemeral (cleared on map change)
;
```

A command handler is a function with signature:
```cpp
std::string handler(std::list<std::string> const& args);
```

A completer is a function with signature:
```cpp
std::vector<std::string> completer(std::list<std::string> const& args);
```

### Built-in Console Commands

| Command | Source | Description |
|---|---|---|
| `QUIT` | `gusanos.cpp` | Exit the game |
| `SCREENSHOT` | `gfx.cpp` | Take a screenshot |
| `MAP <name>` | `game.cpp` | Load a map (with current mod) |
| `GAME <mod>` | `game.cpp` | Change to a different mod |
| `ADDBOT [team]` | `game.cpp` | Add an AI bot player |
| `CONNECT <host>[:port]` | `game.cpp` | Connect to a remote server |
| `RCON <password> <command>` | `game.cpp` | Execute remote console command |
| `KICK <name>` | `game.cpp` | Kick a player by name |
| `BAN <name>` | `game.cpp` | Ban a player by name |
| `BIND <key> <command>` | `gconsole.cpp` | Bind a key to a command |
| `SETCONSOLEKEY <key>` | `gconsole.cpp` | Set the console toggle key |
| `EXEC <file>` | `gconsole.cpp` | Execute a .cfg script file |
| `ALIAS <name> <command>` | `gconsole.cpp` | Create a command alias |
| `ECHO <text>` | `gconsole.cpp` | Print text to the console |
| `RND_SEED [seed]` | `gconsole.cpp` | Get or set the random seed |
| `REST <var>` | `gconsole.cpp` | Reset a variable to its default value |
| `NET_SET_PROXY <host> <port>` | `network.cpp` | Configure network proxy |
| `DISCONNECT` | `network.cpp` | Disconnect from current server |
| `SAY <message>` | `player_input.cpp` | Send a chat message |
| `GUI_LOADXML <file>` | `menu.cpp` | Load a GUI XML layout |
| `GUI_LOADGSS <file>` | `menu.cpp` | Load a GUI GSS stylesheet |
| `GUI_GSS <rules>` | `menu.cpp` | Apply inline GUI style rules |
| `GUI_FOCUS <widget>` | `menu.cpp` | Set GUI focus to a widget |
| `P{index}_COLOUR <R> <G> <B>` | `player_options.cpp` | Set player worm colour |
| `P{index}_TEAM <team>` | `player_options.cpp` | Set player team number |
| `+P{index}_{DIRECTION}` | `player_input.cpp` | Player control commands |
| `-P{index}_{DIRECTION}` | `player_input.cpp` | Player control stop commands |

### Player Control Commands

The engine registers `+P{index}_{ACTION}` and `-P{index}_{ACTION}` commands for
each local player slot (0 to `MAX_LOCAL_PLAYERS-1`, typically 2).

| Command | Action |
|---|---|
| `+P0_LEFT` / `-P0_LEFT` | Player 0 move left |
| `+P0_RIGHT` / `-P0_RIGHT` | Player 0 move right |
| `+P0_UP` / `-P0_UP` | Player 0 aim up / look up |
| `+P0_DOWN` / `-P0_DOWN` | Player 0 aim down / look down |
| `+P0_FIRE` / `-P0_FIRE` | Player 0 fire weapon |
| `+P0_JUMP` / `-P0_JUMP` | Player 0 jump |
| `+P0_CHANGE` / `-P0_CHANGE` | Player 0 enter weapon change mode (hold) |
| `+P0_WEAPON_NEXT` | Player 0 switch to next weapon (one-shot) |
| `+P0_WEAPON_PREV` | Player 0 switch to previous weapon (one-shot) |
| `+P0_NINJAROPE` | Player 0 shoot ninja rope (one-shot) |

The same pattern repeats for P1.

### Lua-Registered Commands

Mod scripts can register new console commands at runtime:

```lua
-- console_register_command(name, function)
console_register_command("mycommand", function(...)
    -- args are passed as individual parameters
    print("mycommand called with", ...)
end)
```

The registered command is ephemeral (cleared on map change).

## System Aliases & Startup Scripts

### autoexec.cfg

On startup, the console executes `autoexec.cfg` (or `autoexec-ded.cfg` for
dedicated server builds). This file is searched in the mod or data paths.

### Default Bindings

| Key | Command |
|---|---|
| F12 | `SCREENSHOT` |
| (configurable) | `BIND <key> <command>` |

## Console Usage

### Toggling

Press the console key (default: backtick/tilde). Configured via:
```
SETCONSOLEKEY <keyname>
```

### Input

- Type commands at the `]` prompt
- Arrow up/down for command history
- Tab completion for commands, variables, and file paths

### Scripting (`.cfg` files)

Config files are plain text, one command per line. Comments begin with `#` —
everything from `#` to the end of the line is ignored (`#` inside double-quoted
strings is respected, and a line that is only whitespace plus `#` is treated as
a full-line comment). Commands may be chained on one line with `;`, and a nested
command's textual output can be substituted into an identifier or argument with
`{command args}` (the inner command runs and its return string is spliced in):

```
# console.cfg example
CON_HEIGHT 150
VID_FULLSCREEN 1
BIND F9 "MAP map01"
BIND F10 QUIT
```

### Built-in Commands Detail

#### `EXEC <filename>`

Executes commands from a file. Searches the mod path and default path.
Example:
```
EXEC autoexec.cfg
```

#### `ALIAS <name> <command...>`

Creates a shorthand for a command. When the alias is invoked, the remaining
text is appended to the alias body.
```
ALIAS dm "MAP dm01"
ALIAS q QUIT
```

#### `ECHO <text>`

Prints text to the console log. Useful in scripts.
```
ECHO Loading configuration...
```

#### `RND_SEED [seed]`

Without arguments, prints the current random seed. With an argument, seeds the
RNG.
```
RND_SEED 12345
```

#### `REST <variable>`

Resets a variable to its default value.
```
REST VID_FULLSCREEN
```

#### `BIND <key> <command>`

Binds a key to execute a command when pressed. Key names are SDL3 scancode
names.
```
BIND F1 "SAY Hello!"
BIND ESCAPE "MAP menu"
```

#### `SETCONSOLEKEY <key>`

Changes the key that toggles the console.
```
SETCONSOLEKEY F2
```

### Remote Console (RCON)

The server can be administered remotely:
```
RCON <password> <command>
```

The password is set with `RCON_PASSWORD` on the server.

## Architecture Notes

- Console variables and commands are stored in `std::map<std::string, ConsoleItem*>`
  within the global `Console console` singleton.
- Type safety: `TVariable<T>` stores typed values, performing string-to-type
  conversion on assignment.
- Enum variables use `EnumVariable` which maps string names to integer values.
- Aliases are expanded at parse time before variable/command lookup.
- Persistence: the `console` Lua table provides read/write access to all CVars
  from Lua scripts (see `docs/LUA_API.md`).