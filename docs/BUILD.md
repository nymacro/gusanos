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
| LuaJIT | Homebrew (`luajit`) | 2.x |
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

# Skip parser regeneration (parsers regenerate by default; any value disables it)
scons no-parsers=1
```

## Code quality targets

```bash
# Reformat all project C/C++ sources in place
scons format

# Run clang-tidy over project C++ sources (uses compile_commands.json)
scons tidy
```

Starter configuration files are provided at the repository root:

- `.clang-format` — controls `scons format`.
- `.clang-tidy` — controls `scons tidy`.

Both tools operate on the project’s own sources under `Goop/`, `Net/`, `Console/`, `GUI/`, `Utility/util/`, `OmfgScript/`, `http/`, `luaapi/`, `lighter/`, `liero2gus/`, and `parsergen/`. Bundled third-party code (`loadpng/`), ZoidCom reference samples (`Net/Reference/`), and generated parser headers are excluded.

`scons tidy` only runs on C++ files that are present in `compile_commands.json`. Run a normal build first to ensure the database is up to date.

## Fuzz testing

libFuzzer harnesses exercise the `Net` and `http` libraries with uncontrolled
input. They are built only when both `build-tests=1` and `fuzz=1` are passed,
and require clang/libFuzzer; clang is auto-detected (e.g. `clang++-22`) when
bare `clang++` is absent from PATH. Targets live under `Net/fuzz/` and
`http/fuzz/` and are isolated in their own `.../fuzz` build directory so they
do not disturb the gcc debug/release/test builds.

```bash
# Build fuzz targets (requires clang/libFuzzer):
scons build=debug build-tests=1 fuzz=1 CC=clang CXX=clang++
# clang auto-detected if bare clang++ is absent (e.g. clang++-22).

# Run a harness (each is a long-running libFuzzer binary):
bin/posix/net_fuzz_bitstream -max_total_time=60 Net/fuzz/corpus/bitstream/
bin/posix/net_fuzz_bitstream_roundtrip -max_total_time=60 \
    -dict=Net/fuzz/net.dict Net/fuzz/corpus/bitstream_roundtrip/
bin/posix/net_fuzz_address -max_total_time=60 Net/fuzz/corpus/address/
bin/posix/http_fuzz_parseheaders -max_total_time=60 \
    -dict=http/fuzz/http.dict http/fuzz/corpus/parseheaders/
bin/posix/http_fuzz_urlencode -max_total_time=60 http/fuzz/corpus/urlencode/
```

Harnesses:

- `net_fuzz_bitstream` — deserializes raw attacker bytes through the
  `ZCom_BitStream` read API.
- `net_fuzz_bitstream_roundtrip` — property test: writes typed values
  (int/bool/string/buffer), serializes, deserializes, and aborts on any
  round-trip mismatch. Floats are excluded (lossy fixed-point encoding).
- `net_fuzz_address` — exercises `ZCom_Address`'s offline API (setIP/getters/
  `toString`/`getAddressIP`/`computeHashKey`/copy/compare). `setAddress()` is
  not fuzzed because it does synchronous DNS resolution.
- `http_fuzz_parseheaders` / `http_fuzz_urlencode` — HTTP header parsing and
  URL-encoding.

Resolved finding: `http_fuzz_parseheaders` found a `boost::bad_lexical_cast`
abort on a malformed (non-numeric or overflowing) `Content-Length` header in
`Request::addHeader` (`http/http.cpp`). This is now fixed — the cast is wrapped
in a `try`/`catch` that ignores invalid values. All five harnesses run clean.

## Output Layout

```
bin/posix/
├── gusanos         — full game (release/debug)
└── gusanos-ded     — dedicated server (dedserv/dedserv-debug)

lib/posix/release/
├── libomfgconsole.a
├── libomfggui.a
├── libomfgutil.a
├── libomfgscript.a
├── libomfghttp.a
├── libglua.a
├── libomfgnet.a
├── libomfgxbrz.a
└── librenderLoaders.a

lib/posix/debug/
└── (same libs, debug variant)

Note: `Goop/SConscript` builds the `gusanos` / `gusanos-ded` programs directly
(no `libgusanos.a`); the libraries above are its link inputs.
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
| `luaapi/SConscript` | `libglua.a` | `luaapi/luaapi/` (LuaJIT wrapper) |
| `Net/SConscript` | `libomfgnet.a` | `Net/` (ZoidCom-on-ENet compat layer) |
| `Vendor/xBRZ_1.9/SConscript` | `libomfgxbrz.a` | `Vendor/xBRZ_1.9/` (xBRZ pixel scaler) |
| `liero2gus/SConscript` | `liero2gus` | `liero2gus/` |
| `lighter/SConscript` | `lighter` | `lighter/` |
| `lighter/loaders/SConscript` | `librenderLoaders.a` | `lighter/loaders/` |
| `parsergen/SConscript` | `parsergen` | `parsergen/` |

## Build Configuration

### Compiler Flags

| Build | CCFLAGS | CPPDEFINES |
|---|---|---|
| `release` | `-O3 -g` | `NDEBUG` |
| `debug` | `-Og -g -fno-omit-frame-pointer -Wextra` | `DEBUG`, `MAP_DOWNLOADING`, `LOG_RUNTIME` |
| `dedserv` | `-O3 -g` | `NDEBUG`, `DEDSERV` |
| `dedserv-debug` | `-Og -g -fno-omit-frame-pointer` | `DEBUG`, `DEDSERV`, `LOG_RUNTIME` |

All builds use `-std=c++17` and the shared base CCFLAGS
`-pipe -fno-diagnostics-show-option -Wfatal-errors -Wall -Wno-unused -Wno-register
-Wno-implicit-fallthrough`, and define `_GNU_SOURCE`,
`BOOST_TIMER_ENABLE_DEPRECATED`.

### Library Detection

Libraries detected via `pkg-config`:
- `sdl3`, `sdl3-mixer`, `sdl3-image`, `sdl3-ttf`
- `libenet`, `libpng`, `zlib`
- `luajit`

Boost libraries detected via `CheckLib`:
- `boost_filesystem` only (in Boost 1.70+ `boost_system` is merged into
  `boost_filesystem`, so it is no longer linked separately)

## Parser Generation

Two parsers are generated at build time (guarded by `NO_PARSERS`; regenerated
by default, pass `no-parsers=1` to skip and use the committed headers):

1. **OmfgScript grammar** (`OmfgScript/omfg_script_parser.h` from `omfg_script_parser.pg`) — parses gameplay object/weapon/exp definitions
2. **GSS grammar** (`GUI/detail/gss-grammar.h` from `detail/gss-grammar.pg`) — parses GUI style sheets

`Console/console-grammar.h` is a hand-maintained committed header — it is **not**
generated (no `.pg` file, no `Parser()` call in `Console/SConscript`).

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