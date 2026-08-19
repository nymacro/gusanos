#ifndef GUSANOS_GAME_RNG_H
#define GUSANOS_GAME_RNG_H

// Deterministic gameplay RNG layer.
//
// Gameplay-critical draws (weapon fire, dig/death bursts, explosion timeouts)
// must match byte-for-byte across peers. The authority seeds a per-burst GameRng
// via mix32(wormNodeID, actionSequence) and ships the seed in the reliable
// event; server and clients run the same action from it => identical particles.
//
// g_gameplayRng is null by default, so grnd/gmidrnd/grndInt fall back to the
// legacy global streams (un-retargeted paths, and deliberately-legacy ones like
// sound/level/AI, stay byte-identical). GameplayRngScope RAII-swaps it for a
// burst and always restores the prior pointer. Particle think()-time sub-spawns
// are deliberately left on the legacy stream: they fire from peer-dependent
// local state, so seeding them would add a sync point without containing
// divergence (particles are cosmetic on non-authority peers).

#include <cstdint>
#include <climits>
#include <boost/random.hpp>

// Seedable mt19937 with the same distribution adapters as the legacy globals
// (uniform_01 and uniform_real(-0.5,0.5)) so gameplay *feel* is unchanged.
struct GameRng {
	GameRng();

	void seed(uint32_t s);

	double rnd();
	double midrnd();
	unsigned long rndInt(unsigned long max);

	// Per-burst FNV-1a checksum over the bit pattern of every produced value.
	// Same seed + same draw sequence => same checksum; any extra/missing/
	// reordered draw diverges. The authority ships it in the burst event so
	// receivers can detect desync instead of silently diverging (NET_RNG_CHECK).
	uint32_t checksum() const {
		return m_check;
	}

  private:
	void fold(uint64_t bits);

	boost::mt19937 m_gen;
	// Reference-typed adapters so they advance (and reflect) m_gen directly,
	// not a copy taken at construction.
	boost::uniform_01<boost::mt19937 &> m_rnd01;
	boost::variate_generator<boost::mt19937 &, boost::uniform_real<>> m_mid;
	uint32_t m_check;
};

// FNV-1a offset basis: initial (and post-seed reset) checksum state.
extern const uint32_t kBurstChecksumOffset;

// Global gameplay RNG context pointer. null => fall back to legacy globals.
extern GameRng *g_gameplayRng;

double grnd();
double gmidrnd();
unsigned long grndInt(unsigned long max);

// RNG desync diagnostics (console vars NET_RNG_CHECK / NET_RNG_DESYNCS are
// bound to these in Network::registerInConsole).
extern int g_netRngCheck;   // verify shipped burst checksums on replay
extern int g_netRngDesyncs; // running desync counter

// Reports a detected RNG desync: bumps NET_RNG_DESYNCS, DLOGs and posts a
// console warning. Implemented in Goop/network.cpp (needs the console).
void rngDesyncReport(const char *kind, uint32_t wormNodeId, uint32_t seq,
					 uint32_t expected, uint32_t actual);

// Verify a replayed burst's checksum against the authority-shipped one.
// No-op unless NET_RNG_CHECK is enabled. Returns true on mismatch.
inline bool burstChecksumMismatch(const char *kind, uint32_t wormNodeId, uint32_t seq, uint32_t shipped,
								  uint32_t local) {
	if (g_netRngCheck && shipped != local) {
		rngDesyncReport(kind, wormNodeId, seq, shipped, local);
		return true;
	}
	return false;
}

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

// RAII swap of g_gameplayRng for a fire/dig/die burst (and its spawned
// particles); restores the prior pointer on destruction. See
// BaseWorm::fireSeedNodeID() / fireActionSeq().
class GameplayRngScope {
  public:
	explicit GameplayRngScope(GameRng &rng) : m_prev(g_gameplayRng) {
		g_gameplayRng = &rng;
	}
	~GameplayRngScope() {
		g_gameplayRng = m_prev;
	}

  private:
	GameplayRngScope(GameplayRngScope const &);
	GameplayRngScope &operator=(GameplayRngScope const &);
	GameRng *m_prev;
};

#endif // GUSANOS_GAME_RNG_H
