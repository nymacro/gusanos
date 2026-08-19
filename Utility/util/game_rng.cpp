#include "game_rng.h"
#include "math_func.h"

#include <cstring>

const uint32_t kBurstChecksumOffset = 2166136261u;

GameRng::GameRng()
	: m_rnd01(m_gen), m_mid(m_gen, boost::uniform_real<>(-0.5, 0.5)), m_check(kBurstChecksumOffset) {}

void GameRng::seed(uint32_t s) {
	m_gen.seed(static_cast<boost::mt19937::result_type>(s));
	m_check = kBurstChecksumOffset;
}

void GameRng::fold(uint64_t bits) {
	for (int i = 0; i < 8; ++i) {
		m_check = (m_check ^ (bits & 0xFFu)) * 0x01000193u;
		bits >>= 8;
	}
}

double GameRng::rnd() {
	double v = m_rnd01();
	uint64_t bits;
	static_assert(sizeof(bits) == sizeof(v), "double must be 64-bit");
	std::memcpy(&bits, &v, sizeof(bits));
	fold(bits);
	return v;
}

double GameRng::midrnd() {
	double v = m_mid();
	uint64_t bits;
	std::memcpy(&bits, &v, sizeof(bits));
	fold(bits);
	return v;
}

unsigned long GameRng::rndInt(unsigned long max) {
	unsigned long v = static_cast<unsigned long>((static_cast<unsigned long long>(m_gen()) * max) >>
												 (CHAR_BIT * sizeof(boost::mt19937::result_type)));
	fold(static_cast<uint64_t>(v));
	return v;
}

GameRng *g_gameplayRng = nullptr;

int g_netRngCheck = 0;
int g_netRngDesyncs = 0;

double grnd() {
	if (g_gameplayRng)
		return g_gameplayRng->rnd();
	return rnd();
}

double gmidrnd() {
	if (g_gameplayRng)
		return g_gameplayRng->midrnd();
	return midrnd();
}

unsigned long grndInt(unsigned long max) {
	if (g_gameplayRng)
		return g_gameplayRng->rndInt(max);
	return rndInt(max);
}
