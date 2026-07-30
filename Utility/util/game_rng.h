#ifndef GUSANOS_GAME_RNG_H
#define GUSANOS_GAME_RNG_H

// Deterministic gameplay RNG layer.
//
// Problem this solves: gameplay-critical random draws (weapon fire particle
// counts/angles/speeds, dig/death bursts, explosion timeouts, particle
// think-time sub-spawns) are consumed from a per-peer global RNG stream
// (`rndgen`/`rnd`/`midrnd` in math_func). Each peer advances that stream
// independently (and at different logical times for predicted vs. proxied
// fire), so spawned particles diverge between host and clients.
//
// Fix: the authority generates a per-burst seed and ships it in the existing
// reliable event channel; both server and clients run the *same* action code
// drawing from a `GameRng` seeded with that value. Same seed + same code +
// same call order => identical particles on every peer.
//
// This file provides:
//   - `GameRng`: a seedable RNG holding its own mt19937 plus the same
//     distribution adapters as the legacy globals (uniform_01 and
//     uniform_real(-0.5,0.5)) so gameplay *feel* is unchanged. The adapters
//     reference the owned engine by reference, so `seed()` actually takes
//     effect (the legacy globals hold *copies* of the engine taken once at
//     construction and would ignore re-seeding).
//   - `g_gameplayRng`: a single-threaded global pointer. When null (default)
//     the free functions fall back to the legacy global streams, so paths
//     that are not yet retargeted (or are deliberately left on legacy, e.g.
//     sound/level/AI) are byte-identical to the pre-change behaviour.
//   - `grnd/gmidrnd/grndInt`: free functions honouring the scoped context.
//   - `mix32`: combine two 32-bit values into a well-mixed seed, used to
//     derive a per-burst gameplay seed from (wormNodeID, actionSequence).
//   - `GameplayRngScope`: RAII swap of `g_gameplayRng` for the duration of a
//     deterministic burst; always restores the previous pointer, so a seeded
//     context can never leak past its burst.
//
// Particle lifetime (think()-time sub-spawns, detect/timer/ground-collision
// events) is deliberately left on the legacy non-deterministic stream. Worm
// bursts (fire/dig/die) run their action under a `GameplayRngScope` on every
// peer, so particles spawned *during* a burst — including their creation
// scripts — already draw from the deterministic burst stream with no extra
// sync point. think()-time events instead fire from peer-dependent local
// state, so a per-particle seed would add a sync point without containing real
// divergence. Particles are cosmetic on non-authority peers (clients remove
// them on eZCom_EventRemoved).

#include <cstdint>
#include <climits>
#include <boost/random.hpp>

// Burst-level RNG: a seedable mt19937 plus the same distribution adapters as
// the legacy globals (uniform_01 and uniform_real(-0.5,0.5)) so gameplay *feel*
// is unchanged. The adapters reference the owned engine by reference, so
// `seed()` actually takes effect (the legacy globals hold *copies* of the
// engine taken once at construction and would ignore re-seeding).
struct GameRng {
	GameRng();

	void seed(uint32_t s);

	double rnd();
	double midrnd();
	unsigned long rndInt(unsigned long max);

  private:
	boost::mt19937 m_gen;
	// Reference-typed adapters so they advance (and reflect) m_gen directly,
	// not a copy taken at construction.
	boost::uniform_01<boost::mt19937 &> m_rnd01;
	boost::variate_generator<boost::mt19937 &, boost::uniform_real<>> m_mid;
};

// Global gameplay RNG context pointer. null => fall back to legacy globals.
extern GameRng *g_gameplayRng;

double grnd();
double gmidrnd();
unsigned long grndInt(unsigned long max);

// Combine two 32-bit values into a well-mixed seed. Used to derive a per-burst
// gameplay seed from (wormNodeID, actionSequence). murmur3-finalizer-style
// avalanche.
inline uint32_t mix32(uint32_t a, uint32_t b) {
	uint32_t x = a * 0x9E3779B1u + b;
	x ^= x >> 16;
	x *= 0x85EBCA6Bu;
	x ^= x >> 13;
	x *= 0xC2B2AE35u;
	x ^= x >> 16;
	return x;
}

// RAII scoped swap of g_gameplayRng. Restores the prior pointer on destruction.
class GameplayRngScope {
  public:
	explicit GameplayRngScope(GameRng &rng) : m_prev(g_gameplayRng) { g_gameplayRng = &rng; }
	~GameplayRngScope() { g_gameplayRng = m_prev; }

  private:
	GameplayRngScope(GameplayRngScope const &);
	GameplayRngScope &operator=(GameplayRngScope const &);
	GameRng *m_prev;
};

#endif // GUSANOS_GAME_RNG_H
