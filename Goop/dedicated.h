#ifndef GUSANOS_DEDICATED_H
#define GUSANOS_DEDICATED_H

// Runtime dedicated (headless) server flag.
//
// Replaces the old compile-time DEDSERV macro for *load-bearing* gating: true
// when the binary is launched with `--dedicated` (no graphics/audio/input).
// Default false (client). Set early in main(), before registerVariables() and
// game.init(), so cvar registration and config-file selection observe it.
//
// Inert structural #ifndef DEDSERV guards (member declarations, #include lines,
// #ifdef DEDSERV #error stubs, whole-file wrappers) are left in place; with
// DEDSERV never defined they are dead-but-harmless and the client build is
// unchanged. Only call sites that would crash / waste work / drift RNG on a
// headless authority are converted to `if (!g_dedicated)` / `if (g_dedicated)`.
extern bool g_dedicated;

#endif // GUSANOS_DEDICATED_H
