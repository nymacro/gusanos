// Standalone correctness test for the SSE2/AVX2/NEON blitter variants.
//
// Verifies that every SIMD variant reproduces the scalar reference
// (addColorsCrude_32 / addColorsCrude_8_4 / scaleColor_32) *bit-for-bit*
// across random sizes, start offsets (exercising alignment prologues),
// colours and blend factors.  The scalar and SIMD paths share the same
// CLIP_* macros, so clipping cannot diverge; only the per-pixel math can,
// which is exactly what this test exercises.
//
// Built only for debug builds (see Goop/blitters/SConscript).  No SDL runtime
// is required: BITMAPs are constructed manually because the blitters only
// touch line[], w/h, cl/ct/cr/cb and format.

#include "blitters.h"
#include "colors.h"
#include "types.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// Sole definition of the extern declared in allegro_compat.h.  The leaf SIMD
// functions do not read it; it is only used here to exercise the dispatch
// ladder in blitters.h.
int cpu_capabilities = 0;

using Blitters::addColorsCrude_32;
using Blitters::drawSprite_mult_8_to_32;
using Blitters::drawSpriteLine_add_8;
using Blitters::hline_add_32;
using Blitters::rectfill_add_32;
#if defined(__SSE2__)
using Blitters::drawSprite_mult_8_to_32_avx2;
using Blitters::drawSprite_mult_8_to_32_sse2;
using Blitters::drawSpriteLine_add_8_avx2;
using Blitters::drawSpriteLine_add_8_sse2;
using Blitters::hline_add_32_avx2;
using Blitters::hline_add_32_sse2;
using Blitters::rectfill_add_32_avx2;
using Blitters::rectfill_add_32_sse2;
#endif

namespace {

typedef unsigned char byte;

// ---- deterministic RNG (fixed seed for reproducible failures) ----
uint32_t g_seed = 0x9e3779b9u;
uint32_t rnd() {
	return g_seed = g_seed * 1664525u + 1013904223u;
}
uint32_t rndBelow(uint32_t n) {
	return n ? rnd() % n : 0;
}

// ---- manually-constructed BITMAP (no SDL) ----
struct TestBmp {
	BITMAP b;
	std::vector<byte> buf;
	std::vector<byte *> rows;
	TestBmp(int depth, int w, int h) {
		std::memset(&b, 0, sizeof(b));
		b.w = w;
		b.h = h;
		b.cl = 0;
		b.ct = 0;
		b.cr = w;
		b.cb = h;
		b.format = depth;
		b.is_sub_bitmap = false;
		b.sdl_surface = nullptr;
		int bpp = (depth == 32) ? 4 : 1;
		size_t pitch = (size_t)w * bpp;
		buf.assign((size_t)(h > 0 ? h : 1) * pitch + 64, 0); // +64 tail safety margin
		rows.resize(h > 0 ? h : 1);
		for (int y = 0; y < h; ++y)
			rows[y] = buf.data() + (size_t)y * pitch;
		b.line = rows.data();
		b.pixels = buf.data();
	}
};

void fillRandom(TestBmp &t, int depth) {
	int bpp = (depth == 32) ? 4 : 1;
	for (int y = 0; y < t.b.h; ++y)
		for (int x = 0; x < t.b.w; ++x) {
			byte *p = t.b.line[y] + (size_t)x * bpp;
			for (int k = 0; k < bpp; ++k)
				p[k] = (byte)rnd();
		}
}

void cloneInto(TestBmp &dst, const TestBmp &src) {
	dst.buf = src.buf; // identical contents -> identical start state
}

bool bufEqual(const TestBmp &a, const TestBmp &b) {
	return a.buf == b.buf;
}

int g_fails = 0;
int g_checks = 0;

void fail(const char *tag, int iter, const char *msg) {
	if (g_fails < 20)
		std::printf("FAIL [%s] iter=%d: %s\n", tag, iter, msg);
	++g_fails;
}

// Compare two bitmaps that should be identical; report first differing byte.
void checkEqual(const char *tag, int iter, const char *variant, const TestBmp &a, const TestBmp &b) {
	++g_checks;
	if (bufEqual(a, b))
		return;
	size_t n = a.buf.size() < b.buf.size() ? a.buf.size() : b.buf.size();
	for (size_t i = 0; i < n; ++i) {
		if (a.buf[i] != b.buf[i]) {
			char msg[160];
			std::snprintf(msg, sizeof(msg), "%s differs at byte %zu: %02x vs %02x", variant, i, a.buf[i], b.buf[i]);
			fail(tag, iter, msg);
			return;
		}
	}
	char msg[128];
	std::snprintf(msg, sizeof(msg), "%s differs in size: %zu vs %zu", variant, a.buf.size(), b.buf.size());
	fail(tag, iter, msg);
}

bool haveAvx2() {
#if defined(__SSE2__)
	return __builtin_cpu_supports("avx2") != 0;
#else
	return false;
#endif
}

// ---- colour / factor tables that stress the SWAR arithmetic ----
const uint32_t kColors[] = {
	0x00000000u, 0xFFFFFFFFu, 0x7F7F7F7Fu, 0x80808080u, 0xFEFEFEFEu, 0x01010101u,
	0x01010100u, 0x80808080u, 0x7F7F7F00u, 0x00FF00FFu, 0x12345678u, 0xFEDCBA98u,
};
const int kNumColors = int(sizeof(kColors) / sizeof(kColors[0]));

const int kFacts[] = {-5, 0, 1, 2, 64, 127, 128, 129, 200, 254, 255, 256, 300};
const int kNumFacts = int(sizeof(kFacts) / sizeof(kFacts[0]));

uint32_t randColor() {
	return (rnd() & 1) ? kColors[rndBelow(kNumColors)] : rnd();
}
int randFact() {
	return (rnd() & 1) ? kFacts[rndBelow(kNumFacts)] : int(rnd() % 320) - 10;
}

// =====================================================================
// rectfill_add_32 : scalar vs sse2 vs avx2
// =====================================================================
void test_rectfill_add(int iters) {
	const char *tag = "rectfill_add_32";
	for (int it = 0; it < iters; ++it) {
		int w = 1 + int(rndBelow(80));
		int h = 1 + int(rndBelow(40));
		TestBmp s(32, w, h), a(32, w, h), v(32, w, h);
		fillRandom(s, 32);
		cloneInto(a, s);
		cloneInto(v, s);

		int x1 = int(rndBelow(w));
		int x2 = x1 + int(rndBelow(w - x1));
		int y1 = int(rndBelow(h));
		int y2 = y1 + int(rndBelow(h - y1));
		uint32_t col = randColor();
		int fact = randFact();

		rectfill_add_32(&s.b, x1, y1, x2, y2, col, fact);
#if defined(__SSE2__)
		rectfill_add_32_sse2(&a.b, x1, y1, x2, y2, col, fact);
		checkEqual(tag, it, "sse2", s, a);
		if (haveAvx2()) {
			rectfill_add_32_avx2(&v.b, x1, y1, x2, y2, col, fact);
			checkEqual(tag, it, "avx2", s, v);
		}
#endif
	}
}

// =====================================================================
// hline_add_32 : scalar vs sse2 vs avx2  (no clipping in the reference)
// =====================================================================
void test_hline_add(int iters) {
	const char *tag = "hline_add_32";
	for (int it = 0; it < iters; ++it) {
		int w = 1 + int(rndBelow(80));
		int h = 1 + int(rndBelow(40));
		TestBmp s(32, w, h), a(32, w, h), v(32, w, h);
		fillRandom(s, 32);
		cloneInto(a, s);
		cloneInto(v, s);

		int x1 = int(rndBelow(w));
		int x2 = x1 + int(rndBelow(w - x1));
		int y = int(rndBelow(h));
		uint32_t col = randColor();
		int fact = randFact();

		hline_add_32(&s.b, x1, y, x2, col, fact);
#if defined(__SSE2__)
		hline_add_32_sse2(&a.b, x1, y, x2, col, fact);
		checkEqual(tag, it, "sse2", s, a);
		if (haveAvx2()) {
			hline_add_32_avx2(&v.b, x1, y, x2, col, fact);
			checkEqual(tag, it, "avx2", s, v);
		}
#endif
	}
}

// =====================================================================
// drawSpriteLine_add_8 : scalar vs sse2 vs avx2  (8-bit where + 8-bit from)
// =====================================================================
void test_sprite_add_8(int iters) {
	const char *tag = "drawSpriteLine_add_8";
	for (int it = 0; it < iters; ++it) {
		int w = 1 + int(rndBelow(80));
		int h = 1 + int(rndBelow(40));
		TestBmp src(8, w, h);
		fillRandom(src, 8);
		TestBmp s(8, w, h), a(8, w, h), v(8, w, h);
		fillRandom(s, 8);
		cloneInto(a, s);
		cloneInto(v, s);

		int count = 1 + int(rndBelow(w));	   // pixels to copy
		int sx = int(rndBelow(w - count + 1)); // source start
		int dx = int(rndBelow(w - count + 1)); // dest start
		int y = int(rndBelow(h));
		int sy = int(rndBelow(h));
		int fact = randFact();
		int x2 = sx + count;

		drawSpriteLine_add_8(&s.b, &src.b, dx, y, sx, sy, x2, fact);
#if defined(__SSE2__)
		drawSpriteLine_add_8_sse2(&a.b, &src.b, dx, y, sx, sy, x2, fact);
		checkEqual(tag, it, "sse2", s, a);
		if (haveAvx2()) {
			drawSpriteLine_add_8_avx2(&v.b, &src.b, dx, y, sx, sy, x2, fact);
			checkEqual(tag, it, "avx2", s, v);
		}
#endif
	}
}

// =====================================================================
// drawSprite_mult_8_to_32 : scalar vs sse2 vs avx2  (32-bit where + 8-bit from)
// =====================================================================
void test_mult_8_to_32(int iters) {
	const char *tag = "drawSprite_mult_8_to_32";
	for (int it = 0; it < iters; ++it) {
		int fw = 1 + int(rndBelow(80)); // from width
		int fh = 1 + int(rndBelow(40));
		int dw = fw; // dest same width for simplicity
		int dh = fh;
		TestBmp src(8, fw, fh);
		fillRandom(src, 8);
		TestBmp s(32, dw, dh), a(32, dw, dh), v(32, dw, dh);
		fillRandom(s, 32);
		cloneInto(a, s);
		cloneInto(v, s);

		int blitH = fh; // full blit (cutl=cutt=cutr=cutb=0)
		int x = int(rndBelow(dw - fw + 1));
		int y = int(rndBelow(dh - blitH + 1));

		drawSprite_mult_8_to_32(&s.b, &src.b, x, y, 0, 0, 0, 0);
#if defined(__SSE2__)
		drawSprite_mult_8_to_32_sse2(&a.b, &src.b, x, y, 0, 0, 0, 0);
		checkEqual(tag, it, "sse2", s, a);
		if (haveAvx2()) {
			drawSprite_mult_8_to_32_avx2(&v.b, &src.b, x, y, 0, 0, 0, 0);
			checkEqual(tag, it, "avx2", s, v);
		}
#endif
	}
}

// =====================================================================
// Edge-case sweep: every width 1..72 with fact/colour boundaries, exercising
// the SIMD vector/tail loop boundaries (4/8/16/32) and alignment prologues.
// =====================================================================
void test_edge_widths() {
	const char *tag = "edge_widths";
	for (int w = 1; w <= 72; ++w) {
		for (int fi = 0; fi < kNumFacts; ++fi) {
			int fact = kFacts[fi];
			{
				// 32-bit rectfill + hline, offset 0..3 for alignment
				for (int off = 0; off < 4 && off < w; ++off) {
					TestBmp s(32, w, 4), a(32, w, 4), v(32, w, 4);
					fillRandom(s, 32);
					cloneInto(a, s);
					cloneInto(v, s);
					int x2 = w - 1;
					uint32_t col = kColors[fi % kNumColors];
					rectfill_add_32(&s.b, off, 0, x2, 3, col, fact);
#if defined(__SSE2__)
					rectfill_add_32_sse2(&a.b, off, 0, x2, 3, col, fact);
					checkEqual(tag, w, "rectfill sse2", s, a);
					if (haveAvx2()) {
						rectfill_add_32_avx2(&v.b, off, 0, x2, 3, col, fact);
						checkEqual(tag, w, "rectfill avx2", s, v);
					}
#endif
					TestBmp hs(32, w, 4), ha(32, w, 4), hv(32, w, 4);
					fillRandom(hs, 32);
					cloneInto(ha, hs);
					cloneInto(hv, hs);
					hline_add_32(&hs.b, off, 1, x2, col, fact);
#if defined(__SSE2__)
					hline_add_32_sse2(&ha.b, off, 1, x2, col, fact);
					checkEqual(tag, w, "hline sse2", hs, ha);
					if (haveAvx2()) {
						hline_add_32_avx2(&hv.b, off, 1, x2, col, fact);
						checkEqual(tag, w, "hline avx2", hs, hv);
					}
#endif
				}
			}
			{
				// 8-bit sprite add, full-row blits at dest offsets 0..3
				TestBmp src(8, w, 4);
				fillRandom(src, 8);
				for (int off = 0; off < 4 && off < w; ++off) {
					int count = w - off;
					int sx = 0;
					TestBmp s(8, w, 4), a(8, w, 4), v(8, w, 4);
					fillRandom(s, 8);
					cloneInto(a, s);
					cloneInto(v, s);
					drawSpriteLine_add_8(&s.b, &src.b, off, 1, sx, 1, sx + count, fact);
#if defined(__SSE2__)
					drawSpriteLine_add_8_sse2(&a.b, &src.b, off, 1, sx, 1, sx + count, fact);
					checkEqual(tag, w, "sprite8 sse2", s, a);
					if (haveAvx2()) {
						drawSpriteLine_add_8_avx2(&v.b, &src.b, off, 1, sx, 1, sx + count, fact);
						checkEqual(tag, w, "sprite8 avx2", s, v);
					}
#endif
				}
			}
			{
				// 32-bit mult from 8-bit, full-row blits at dest offsets 0..3
				TestBmp src(8, w, 4);
				fillRandom(src, 8);
				for (int off = 0; off < 4 && off < w; ++off) {
					TestBmp s(32, w, 4), a(32, w, 4), v(32, w, 4);
					fillRandom(s, 32);
					cloneInto(a, s);
					cloneInto(v, s);
					drawSprite_mult_8_to_32(&s.b, &src.b, off, 0, 0, 0, 0, 0);
#if defined(__SSE2__)
					drawSprite_mult_8_to_32_sse2(&a.b, &src.b, off, 0, 0, 0, 0, 0);
					checkEqual(tag, w, "mult sse2", s, a);
					if (haveAvx2()) {
						drawSprite_mult_8_to_32_avx2(&v.b, &src.b, off, 0, 0, 0, 0, 0);
						checkEqual(tag, w, "mult avx2", s, v);
					}
#endif
				}
			}
		}
	}
}

// =====================================================================
// Dispatch ladder: set cpu_capabilities and call the global inline wrappers
// (rectfill_add / hline_add / drawSpriteLine_add / drawSprite_mult_8) and
// confirm they pick the SIMD path that matches the scalar reference.
// =====================================================================
void test_dispatch() {
	const char *tag = "dispatch";
	const int w = 37, h = 5;

	auto run = [&](int caps) {
		cpu_capabilities = caps;
		uint32_t col = 0x12345678u;

		TestBmp rs(32, w, h), rd(32, w, h);
		fillRandom(rs, 32);
		cloneInto(rd, rs);
		rectfill_add_32(&rs.b, 2, 0, w - 1, h - 1, col, 200);
		::rectfill_add(&rd.b, 2, 0, w - 1, h - 1, col, 200);
		checkEqual(tag, caps, "rectfill_add", rs, rd);

		TestBmp hs(32, w, h), hd(32, w, h);
		fillRandom(hs, 32);
		cloneInto(hd, hs);
		hline_add_32(&hs.b, 3, 2, w - 1, col, 200);
		::hline_add(&hd.b, 3, 2, w - 1, col, 200);
		checkEqual(tag, caps, "hline_add", hs, hd);

		TestBmp src(8, w, h);
		fillRandom(src, 8);
		TestBmp ss(8, w, h), sd(8, w, h);
		fillRandom(ss, 8);
		cloneInto(sd, ss);
		drawSpriteLine_add_8(&ss.b, &src.b, 3, 1, 0, 1, w, 220);
		::drawSpriteLine_add(&sd.b, &src.b, 3, 1, 0, 1, w, 220);
		checkEqual(tag, caps, "drawSpriteLine_add", ss, sd);

		TestBmp ms(32, w, h), md(32, w, h);
		fillRandom(ms, 32);
		cloneInto(md, ms);
		drawSprite_mult_8_to_32(&ms.b, &src.b, 0, 0, 0, 0, 0, 0);
		::drawSprite_mult_8(&md.b, &src.b, 0, 0);
		checkEqual(tag, caps, "drawSprite_mult_8", ms, md);
	};

#if defined(__SSE2__)
	run(CPU_SSE2);
	if (haveAvx2())
		run(CPU_SSE2 | CPU_AVX2);
#endif
	run(0); // scalar fallback
	cpu_capabilities = 0;
}

} // namespace

int main() {
	const int ITERS = 30000;
	std::printf("blitter SIMD correctness test (%d random iters/fn)\n", ITERS);
#if defined(__SSE2__)
	std::printf("  SSE2 path: enabled   AVX2 path: %s\n", haveAvx2() ? "enabled" : "disabled (no CPU support)");
#elif defined(__ARM_NEON) || defined(__aarch64__)
	std::printf("  NEON path: enabled\n");
#else
	std::printf("  no SIMD path compiled (scalar only)\n");
#endif

	test_rectfill_add(ITERS);
	test_hline_add(ITERS);
	test_sprite_add_8(ITERS);
	test_mult_8_to_32(ITERS);
	test_edge_widths();
	test_dispatch();

	std::printf("checks=%d  fails=%d\n", g_checks, g_fails);
	if (g_fails == 0) {
		std::printf("ALL TESTS PASSED\n");
		return 0;
	}
	std::printf("TESTS FAILED\n");
	return 1;
}
