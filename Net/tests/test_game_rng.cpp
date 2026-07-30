// test_game_rng.cpp
// Determinism and scope tests for the gameplay RNG layer (Utility/util/game_rng).
//
// The gameplay RNG model is per-burst: a worm fire/dig/die action seeds a
// `GameRng` from mix32(wormNodeID, actionSeq) and runs under a
// `GameplayRngScope` so every peer reproduces identical particles. These tests
// pin the two invariants that the whole scheme rests on:
//   1. `GameRng` is deterministic: the same seed yields the same draw sequence.
//   2. `GameplayRngScope` is a leak-free RAII swap of `g_gameplayRng` (restores
//      the prior pointer on exit, including when nested).
// and that the `grnd`/`gmidrnd`/`grndInt` free functions route to the scoped
// `GameRng` (rather than the legacy global stream) while a scope is active.

#include <boost/test/unit_test.hpp>
#include "util/game_rng.h"

BOOST_AUTO_TEST_SUITE(game_rng)

// Same seed must reproduce an identical draw sequence across instances.
BOOST_AUTO_TEST_CASE(game_rng_same_seed_is_deterministic) {
	GameRng a, b;
	a.seed(0x12345678u);
	b.seed(0x12345678u);
	for (int i = 0; i < 100; ++i) {
		BOOST_CHECK_EQUAL(a.rnd(), b.rnd());
		BOOST_CHECK_EQUAL(a.midrnd(), b.midrnd());
		BOOST_CHECK_EQUAL(a.rndInt(1000), b.rndInt(1000));
	}
}

// Distinct seeds must not reproduce the same sequence.
BOOST_AUTO_TEST_CASE(game_rng_distinct_seeds_differ) {
	GameRng a, b;
	a.seed(1);
	b.seed(2);
	bool differ = false;
	for (int i = 0; i < 64 && !differ; ++i) {
		if (a.rnd() != b.rnd() || a.rndInt(1000) != b.rndInt(1000))
			differ = true;
	}
	BOOST_CHECK(differ);
}

// Re-seeding resets the stream to the same starting point.
BOOST_AUTO_TEST_CASE(game_rng_reseed_resets_stream) {
	GameRng r;
	r.seed(42);
	double firstRnd = r.rnd();
	unsigned long firstInt = r.rndInt(500);
	// Advance well past the first draws.
	for (int i = 0; i < 50; ++i)
		r.rnd();
	r.seed(42);
	BOOST_CHECK_EQUAL(r.rnd(), firstRnd);
	BOOST_CHECK_EQUAL(r.rndInt(500), firstInt);
}

// GameplayRngScope swaps g_gameplayRng to the given RNG and restores the prior
// pointer on destruction.
BOOST_AUTO_TEST_CASE(scope_swaps_and_restores) {
	GameRng *saved = g_gameplayRng;
	{
		GameRng rg;
		GameplayRngScope scope(rg);
		BOOST_CHECK_EQUAL(g_gameplayRng, &rg);
	}
	BOOST_CHECK_EQUAL(g_gameplayRng, saved);
}

// Nested scopes restore the correct pointer at each level (inner then outer).
BOOST_AUTO_TEST_CASE(scope_nests_and_restores_each_level) {
	GameRng outer, inner;
	GameRng *saved = g_gameplayRng;
	{
		GameplayRngScope so(outer);
		BOOST_CHECK_EQUAL(g_gameplayRng, &outer);
		{
			GameplayRngScope si(inner);
			BOOST_CHECK_EQUAL(g_gameplayRng, &inner);
		}
		BOOST_CHECK_EQUAL(g_gameplayRng, &outer);
	}
	BOOST_CHECK_EQUAL(g_gameplayRng, saved);
}

// While a scope is active, the free functions must draw from the scoped GameRng
// (matching an independently-seeded reference stream), not the legacy globals.
BOOST_AUTO_TEST_CASE(grnd_routes_to_scoped_rng) {
	GameRng rg, ref;
	rg.seed(0xA5A5A5A5u);
	ref.seed(0xA5A5A5A5u);
	GameplayRngScope scope(rg);
	for (int i = 0; i < 50; ++i) {
		BOOST_CHECK_EQUAL(grnd(), ref.rnd());
		BOOST_CHECK_EQUAL(gmidrnd(), ref.midrnd());
		BOOST_CHECK_EQUAL(grndInt(999), ref.rndInt(999));
	}
}

BOOST_AUTO_TEST_SUITE_END()
