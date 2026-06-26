# Build System

## Prerequisites

| Dependency | Source | Version |
|---|---|---|
| SDL3 | Homebrew (`sdl3`) | 3.x |
| SDL3_mixer | Homebrew (`sdl3-mixer`) | 3.x |
| SDL3_image | Homebrew (`sdl3-image`) | 3.x |
| SDL3_ttf | Homebrew (`sdl3-ttf`) | 3.x |
| ENet | Homebrew (`libenet`) | 1.3.x |
| libpng | Homebrew (`libpng`) | — |
| zlib | Homebrew (`zlib`) | — |
| Boost | Homebrew (`boost`) | 1.70+ |
| re2c | Homebrew (`re2c`) | — |
| pkg-config | Homebrew (`pkg-config`) | — |

On Linux (Homebrew at `/home/linuxbrew/.linuxbrew`), the `SConstruct` auto-detects
the Homebrew prefix and appends include/library paths.

## Build Commands

```bash
# Debug build (full game)
scons build=debug

# Release build
scons

# Dedicated server (headless)
scons build=dedserv

# Debug dedicated server
scons build=dedserv-debug

# Clean
scons -c

# Force-parser regeneration
scons no-parsers=0
```

## Output Layout

```
bin/posix/
├── gusanos         — full game (release/debug)
└── gusanos-ded     — dedicated server (dedserv/dedserv-debug)

lib/posix/release/
├── libomfggui.a
├── libomfghttp.a
├── libomfgconsole.a
├── libomfgscript.a
├── libglua.a
├── libomfgutil.a
└── libgusanos.a

lib/posix/debug/
└── (same libs, debug variant)
```

## Build Targets

| SConscript | Library/Program | Source Directory |
|---|---|---|
| `Goop/SConscript` | `gusanos` / `gusanos-ded` | `Goop/`, `Goop/lua/`, `Goop/blitters/`, `Goop/loaders/` |
| `Console/SConscript` | `libomfgconsole.a` | `Console/` |
| `GUI/SConscript` | `libomfggui.a` | `GUI/`, `GUI/detail/`, `GUI/lua/` |
| `Utility/util/SConscript` | `libomfgutil.a` | `Utility/util/` |
| `OmfgScript/SConscript` | `libomfgscript.a` | `OmfgScript/` |
| `http/SConscript` | `libomfghttp.a` | `http/` |
| `lua51/SConscript` | `libglua.a` | `lua51/`, `lua51/luaapi/` |
| `liero2gus/SConscript` | `liero2gus` | `liero2gus/` |
| `lighter/SConscript` | `lighter` | `lighter/`, `lighter/loaders/` |
| `parsergen/SConscript` | `parsergen` | `parsergen/` |

## Build Configuration

### Compiler Flags

| Build | CCFLAGS | CPPDEFINES |
|---|---|---|
| `release` | `-O3 -g` | `NDEBUG` |
| `debug` | `-Og -g -fno-omit-frame-pointer -Wextra` | `DEBUG`, `MAP_DOWNLOADING`, `LOG_RUNTIME` |
| `dedserv` | `-O3 -g` | `NDEBUG`, `DEDSERV` |
| `dedserv-debug` | `-Og -g -fno-omit-frame-pointer` | `DEBUG`, `DEDSERV`, `LOG_RUNTIME` |

All builds use `-std=c++17`, `-Wall -Wno-reorder`, and define `_GNU_SOURCE`,
`BOOST_TIMER_ENABLE_DEPRECATED`.

### Library Detection

Libraries detected via `pkg-config`:
- `sdl3`, `sdl3-mixer`, `sdl3-image`, `sdl3-ttf`
- `libenet`, `libpng`, `zlib`

Boost libraries detected via `CheckLib`:
- `boost_filesystem`, `boost_system`

## Parser Generation

Two parsers are generated at build time:

1. **Console grammar** (`Console/console-grammar.h`) — parses console commands
2. **OmfgScript grammar** (`OmfgScript/omfg_script_parser.h`) — parses GUI style sheets

Process:
1. `parsergen` tool is built first
2. `.pg` grammar file → `parsergen` → `.h.re` intermediate
3. `.h.re` → `re2c` → final `.h` parser

## Key SConstruct Variables

| Variable | Default | Purpose |
|---|---|---|
| `MY_CONF` | `posix` | Build configuration variant |
| `MY_BUILD` | `release` | Build type (release/debug/dedserv/dedserv-debug) |
| `MY_SUBFOLDER` | `<conf>/<build>` | Build output subdirectory |
| `NO_PARSERS` | `False` | Skip parser regeneration |