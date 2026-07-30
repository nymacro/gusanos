#include "game_rng.h"
#include "math_func.h"

GameRng::GameRng()
	: m_rnd01(m_gen), m_mid(m_gen, boost::uniform_real<>(-0.5, 0.5)) {}

void GameRng::seed(uint32_t s) {
	m_gen.seed(static_cast<boost::mt19937::result_type>(s));
}

double GameRng::rnd() {
	return m_rnd01();
}

double GameRng::midrnd() {
	return m_mid();
}

unsigned long GameRng::rndInt(unsigned long max) {
	return static_cast<unsigned long>((static_cast<unsigned long long>(m_gen()) * max) >>
									  (CHAR_BIT * sizeof(boost::mt19937::result_type)));
}

GameRng *g_gameplayRng = nullptr;

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
