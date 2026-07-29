# Gusanos

A Liero/Worms-inspired real-time 2D shooting game with destructible terrain,
originally based on the Gusanos (Gusanos) engine. This fork ports the legacy
Allegro 4 + ZoidCom + FMOD codebase to SDL3 + ENet + SDL3_mixer, targeting
C++17 with LuaJIT scripting via pkg-config.

## Quick Reference

- **Entry point:** `Goop/gusanos.cpp` `main()`
- **Build:** `scons build=debug` (debug) or `scons` (release)
- **Dedicated server:** `scons build=dedserv`
- **Config:** `autoexec.cfg` / `autoexec-ded.cfg` (console script)
- **Mods:** `default/` directory tree scanned for resources at startup

## Architecture Overview

```
                  ┌─────────────────────────────────────────────┐
                  │                main()                       │
                  │        Goop/gusanos.cpp                     │
                  │   init → loop → shutdown                    │
                  └──────┬──────────────────────────────────────┘
          ┌──────────────┼──────────────────┬─────────────────┐
          ▼              ▼                  ▼                 ▼
   ┌────────────┐ ┌────────────┐  ┌────────────────┐ ┌──────────────┐
   │ Game       │ │ Gfx        │  │ Network        │ │ Sfx          │
   │ (core      │ │ (renderer, │  │ (ENet wrap,    │ │ (SDL3_mixer) │
   │  logic)    │ │  viewport) │  │  ZoidCompat)   │ │              │
   ├────────────┤ ├────────────┤  ├────────────────┤ ├──────────────┤
   │ Level      │ │ Console    │  │ Server/Client  │ │ LuaJIT (system) │
   │ (terrain)  │ │ (dev UI)   │  │ BitStream      │ │ (embedded)   │
   │            │ │            │  │ Replicators    │ │              │
   ├────────────┤ ├────────────┤  ├────────────────┤ ├──────────────┤
   │ Objects    │ │ Menu/GUI   │  │ Updater        │ │ OmfgScript   │
   │ Particles  │ │ OmfgGUI    │  │ (file DL)      │ │ (GSS/XML)    │
   │ Weapons    │ │            │  │                │ │              │
   └────────────┘ └────────────┘  └────────────────┘ └──────────────┘
```

Most subsystems are global singleton objects: `game`, `gfx`, `network`, `sfx`,
`console`, `updater`, `luaCallbacks`.

## Docs Index

- [ARCHITECTURE.md](ARCHITECTURE.md) — Component relationships, dependencies, data flow
- [COMPONENTS.md](COMPONENTS.md) — Per-component reference: responsibilities, public API, key files
- [BUILD.md](BUILD.md) — Build system, targets, flags, dependency detection
- [MIGRATION.md](../MIGRATION.md) — SDL3 migration plan and status (in repo root)
