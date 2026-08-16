#ifndef GUSANOS_DEDICATED_H
#define GUSANOS_DEDICATED_H

// Runtime dedicated (headless) server flag: true when launched with
// `--dedicated` (no graphics/audio/input), false for client. Set early in
// main() before registerVariables()/game.init() so cvars and config observe it.
// Replaces the old compile-time DEDSERV macro for load-bearing gating; inert
// #ifndef DEDSERV guards are left as dead-but-harmless structural markers.
extern bool g_dedicated;

#endif // GUSANOS_DEDICATED_H
