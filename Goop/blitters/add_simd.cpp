#ifndef DEDSERV

#include "blitters.h"
#include "colors.h"
#include "macros.h"

#include <cstring>

#if defined(__SSE2__)
#include <immintrin.h>
#define AVX2_TARGET __attribute__((target("avx2")))
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

// SIMD replacements for the legacy MMX inline-asm blitters.  Each variant
// reproduces the *scalar* reference (addColorsCrude_32 / addColorsCrude_8_4 /
// scaleColor_8_4) bit-for-bit rather than true saturating add (paddusb), so the
// SIMD and scalar code paths render identically.  Unaligned loads/stores are
// used throughout; 8-bit sprite paths align dest to a 4-byte boundary first
// because addColorsCrude_8_4 is a 4-byte SWAR operation.

namespace Blitters {

// Shared scalar prologue/tail helpers for the 8-bit add path (no SIMD ops, so
// they are callable from any arch-specialised body).
// Matches the scalar SPRITE_X_LOOP_ALIGN(4, 4, ...) alignment prologue
// exactly: the scalar `while (ptrdiff_t(dest_) & (align_ - 1))` loop has NO
// remaining-count guard, so it runs until dest is 4-aligned and may process
// up to 3 bytes past `count` (c goes negative).  The SIMD path must over-run
// identically to stay bit-for-bit with the scalar reference; the subsequent
// 16-byte / 4-byte / 1-byte loops all key off `c >= N` and are correctly
// skipped once c has gone negative.
static void add8_align(uint8_t *&dest, const uint8_t *&src, int &c, int fact) {
	while (reinterpret_cast<ptrdiff_t>(dest) & 3) {
		Pixel sv = static_cast<Pixel>(*src);
		if (fact < 255)
			sv = scaleColor_8_4(sv, fact);
		*dest = static_cast<uint8_t>(addColorsCrude_8_4(static_cast<Pixel>(*dest), sv));
		++dest;
		++src;
		--c;
	}
}

static void add8_tail(uint8_t *&dest, const uint8_t *&src, int &c, int fact) {
	while (c >= 4) {
		uint32_t dv, sv;
		std::memcpy(&dv, dest, 4);
		std::memcpy(&sv, src, 4);
		if (fact < 255)
			sv = scaleColor_8_4(sv, fact);
		dv = addColorsCrude_8_4(dv, sv);
		std::memcpy(dest, &dv, 4);
		dest += 4;
		src += 4;
		c -= 4;
	}
	while (c >= 1) {
		Pixel sv = static_cast<Pixel>(*src);
		if (fact < 255)
			sv = scaleColor_8_4(sv, fact);
		*dest = static_cast<uint8_t>(addColorsCrude_8_4(static_cast<Pixel>(*dest), sv));
		++dest;
		++src;
		--c;
	}
}

#if defined(__SSE2__)

// ---- 32-bit rectfill/hline core: addColorsCrude_32(*p, col) per pixel ----

static void add_row_sse2(Pixel32 *p, int count, Pixel col) {
	const __m128i vsrc = _mm_set1_epi32(static_cast<int>(col));
	const __m128i m_fe = _mm_set1_epi32(0x00FEFEFF);
	const __m128i m_carry = _mm_set1_epi32(0x01010100);
	const __m128i one = _mm_set1_epi32(0x010101);
	const __m128i m_rgb = _mm_set1_epi32(0x00FFFFFF);
	const __m128i m_alpha = _mm_set1_epi32(0xFF000000);
	int c = count;
	for (; c >= 4; c -= 4, p += 4) {
		__m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i *>(p));
		__m128i sum = _mm_add_epi32(_mm_and_si128(d, m_fe), _mm_and_si128(vsrc, m_fe));
		__m128i temp1 = _mm_srli_epi32(_mm_and_si128(sum, m_carry), 7);
		__m128i res = _mm_or_si128(sum, _mm_sub_epi32(one, temp1));
		res = _mm_and_si128(res, m_rgb);
		res = _mm_or_si128(res, _mm_and_si128(vsrc, m_alpha));
		_mm_storeu_si128(reinterpret_cast<__m128i *>(p), res);
	}
	for (; c >= 1; --c, ++p)
		*p = addColorsCrude_32(*p, col);
}

AVX2_TARGET
static void add_row_avx2(Pixel32 *p, int count, Pixel col) {
	const __m256i vsrc = _mm256_set1_epi32(static_cast<int>(col));
	const __m256i m_fe = _mm256_set1_epi32(0x00FEFEFF);
	const __m256i m_carry = _mm256_set1_epi32(0x01010100);
	const __m256i one = _mm256_set1_epi32(0x010101);
	const __m256i m_rgb = _mm256_set1_epi32(0x00FFFFFF);
	const __m256i m_alpha = _mm256_set1_epi32(0xFF000000);
	int c = count;
	for (; c >= 8; c -= 8, p += 8) {
		__m256i d = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p));
		__m256i sum = _mm256_add_epi32(_mm256_and_si256(d, m_fe), _mm256_and_si256(vsrc, m_fe));
		__m256i temp1 = _mm256_srli_epi32(_mm256_and_si256(sum, m_carry), 7);
		__m256i res = _mm256_or_si256(sum, _mm256_sub_epi32(one, temp1));
		res = _mm256_and_si256(res, m_rgb);
		res = _mm256_or_si256(res, _mm256_and_si256(vsrc, m_alpha));
		_mm256_storeu_si256(reinterpret_cast<__m256i *>(p), res);
	}
	for (; c >= 1; --c, ++p)
		*p = addColorsCrude_32(*p, col);
}

void rectfill_add_32_sse2(BITMAP *where, int x1, int y1, int x2, int y2, Pixel colour, int fact) {
	CLIP_RECT();
	Pixel col = scaleColor_32(colour, fact);
	for (; y1 <= y2; ++y1)
		add_row_sse2(reinterpret_cast<Pixel32 *>(where->line[y1]) + x1, x2 - x1 + 1, col);
}

AVX2_TARGET
void rectfill_add_32_avx2(BITMAP *where, int x1, int y1, int x2, int y2, Pixel colour, int fact) {
	CLIP_RECT();
	Pixel col = scaleColor_32(colour, fact);
	for (; y1 <= y2; ++y1)
		add_row_avx2(reinterpret_cast<Pixel32 *>(where->line[y1]) + x1, x2 - x1 + 1, col);
}

void hline_add_32_sse2(BITMAP *where, int x1, int y1, int x2, Pixel colour, int fact) {
	Pixel col = scaleColor_32(colour, fact);
	add_row_sse2(reinterpret_cast<Pixel32 *>(where->line[y1]) + x1, x2 - x1 + 1, col);
}

AVX2_TARGET
void hline_add_32_avx2(BITMAP *where, int x1, int y1, int x2, Pixel colour, int fact) {
	Pixel col = scaleColor_32(colour, fact);
	add_row_avx2(reinterpret_cast<Pixel32 *>(where->line[y1]) + x1, x2 - x1 + 1, col);
}

// ---- 8-bit sprite line add core ----

static void sprite_add_row_sse2(uint8_t *dest, const uint8_t *src, int count, int fact) {
	if (fact <= 0)
		return;
	int c = count;
	add8_align(dest, src, c, fact);
	if (fact >= 255) {
		const __m128i m7f = _mm_set1_epi32(0x7F7F7F7F);
		const __m128i m80 = _mm_set1_epi32(0x80808080);
		const __m128i one = _mm_set1_epi32(0x01010101);
		for (; c >= 16; c -= 16, dest += 16, src += 16) {
			__m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i *>(dest));
			__m128i s = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src));
			__m128i a = _mm_and_si128(_mm_srli_epi32(d, 1), m7f);
			__m128i b = _mm_and_si128(_mm_srli_epi32(s, 1), m7f);
			__m128i sum = _mm_add_epi32(a, b);
			__m128i temp1 = _mm_srli_epi32(_mm_and_si128(sum, m80), 6);
			__m128i res = _mm_slli_epi32(_mm_or_si128(sum, _mm_sub_epi32(one, temp1)), 1);
			_mm_storeu_si128(reinterpret_cast<__m128i *>(dest), res);
		}
	} else {
		const __m128i m00ff = _mm_set1_epi32(0x00FF00FF);
		const __m128i mff00 = _mm_set1_epi32(0xFF00FF00);
		const __m128i vfact = _mm_set1_epi16(static_cast<short>(static_cast<uint16_t>(fact)));
		const __m128i m7f = _mm_set1_epi32(0x7F7F7F7F);
		const __m128i m80 = _mm_set1_epi32(0x80808080);
		const __m128i one = _mm_set1_epi32(0x01010101);
		for (; c >= 16; c -= 16, dest += 16, src += 16) {
			__m128i s = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src));
			__m128i t1 = _mm_and_si128(s, m00ff);
			__m128i r1 = _mm_and_si128(_mm_srli_epi32(_mm_mullo_epi16(t1, vfact), 8), m00ff);
			__m128i t2 = _mm_srli_epi32(_mm_and_si128(s, mff00), 8);
			__m128i r2 = _mm_and_si128(_mm_mullo_epi16(t2, vfact), mff00);
			__m128i sc = _mm_or_si128(r1, r2);
			__m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i *>(dest));
			__m128i a = _mm_and_si128(_mm_srli_epi32(d, 1), m7f);
			__m128i b = _mm_and_si128(_mm_srli_epi32(sc, 1), m7f);
			__m128i sum = _mm_add_epi32(a, b);
			__m128i temp1 = _mm_srli_epi32(_mm_and_si128(sum, m80), 6);
			__m128i res = _mm_slli_epi32(_mm_or_si128(sum, _mm_sub_epi32(one, temp1)), 1);
			_mm_storeu_si128(reinterpret_cast<__m128i *>(dest), res);
		}
	}
	add8_tail(dest, src, c, fact);
}

AVX2_TARGET
static void sprite_add_row_avx2(uint8_t *dest, const uint8_t *src, int count, int fact) {
	if (fact <= 0)
		return;
	int c = count;
	add8_align(dest, src, c, fact);
	if (fact >= 255) {
		const __m256i m7f = _mm256_set1_epi32(0x7F7F7F7F);
		const __m256i m80 = _mm256_set1_epi32(0x80808080);
		const __m256i one = _mm256_set1_epi32(0x01010101);
		for (; c >= 32; c -= 32, dest += 32, src += 32) {
			__m256i d = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(dest));
			__m256i s = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src));
			__m256i a = _mm256_and_si256(_mm256_srli_epi32(d, 1), m7f);
			__m256i b = _mm256_and_si256(_mm256_srli_epi32(s, 1), m7f);
			__m256i sum = _mm256_add_epi32(a, b);
			__m256i temp1 = _mm256_srli_epi32(_mm256_and_si256(sum, m80), 6);
			__m256i res = _mm256_slli_epi32(_mm256_or_si256(sum, _mm256_sub_epi32(one, temp1)), 1);
			_mm256_storeu_si256(reinterpret_cast<__m256i *>(dest), res);
		}
	} else {
		const __m256i m00ff = _mm256_set1_epi32(0x00FF00FF);
		const __m256i mff00 = _mm256_set1_epi32(0xFF00FF00);
		const __m256i vfact = _mm256_set1_epi32(fact);
		const __m256i m7f = _mm256_set1_epi32(0x7F7F7F7F);
		const __m256i m80 = _mm256_set1_epi32(0x80808080);
		const __m256i one = _mm256_set1_epi32(0x01010101);
		for (; c >= 32; c -= 32, dest += 32, src += 32) {
			__m256i s = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src));
			__m256i t1 = _mm256_and_si256(s, m00ff);
			__m256i r1 = _mm256_and_si256(_mm256_srli_epi32(_mm256_mullo_epi32(t1, vfact), 8), m00ff);
			__m256i t2 = _mm256_srli_epi32(_mm256_and_si256(s, mff00), 8);
			__m256i r2 = _mm256_and_si256(_mm256_mullo_epi32(t2, vfact), mff00);
			__m256i sc = _mm256_or_si256(r1, r2);
			__m256i d = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(dest));
			__m256i a = _mm256_and_si256(_mm256_srli_epi32(d, 1), m7f);
			__m256i b = _mm256_and_si256(_mm256_srli_epi32(sc, 1), m7f);
			__m256i sum = _mm256_add_epi32(a, b);
			__m256i temp1 = _mm256_srli_epi32(_mm256_and_si256(sum, m80), 6);
			__m256i res = _mm256_slli_epi32(_mm256_or_si256(sum, _mm256_sub_epi32(one, temp1)), 1);
			_mm256_storeu_si256(reinterpret_cast<__m256i *>(dest), res);
		}
	}
	add8_tail(dest, src, c, fact);
}

void drawSpriteLine_add_8_sse2(BITMAP *where, BITMAP *from, int x, int y, int x1, int y1, int x2, int fact) {
	if (bitmap_color_depth(from) != 8)
		return;
	CLIP_HLINE();
	sprite_add_row_sse2(reinterpret_cast<uint8_t *>(where->line[y]) + x,
						reinterpret_cast<const uint8_t *>(from->line[y1]) + x1, x2 - x1, fact);
}

AVX2_TARGET
void drawSpriteLine_add_8_avx2(BITMAP *where, BITMAP *from, int x, int y, int x1, int y1, int x2, int fact) {
	if (bitmap_color_depth(from) != 8)
		return;
	CLIP_HLINE();
	sprite_add_row_avx2(reinterpret_cast<uint8_t *>(where->line[y]) + x,
						reinterpret_cast<const uint8_t *>(from->line[y1]) + x1, x2 - x1, fact);
}

#elif defined(__ARM_NEON) || defined(__aarch64__)

// ---- NEON 32-bit add row ----

static void add_row_neon(Pixel32 *p, int count, Pixel col) {
	const uint32x4_t vsrc = vdupq_n_u32(static_cast<uint32_t>(col));
	const uint32x4_t m_fe = vdupq_n_u32(0x00FEFEFFu);
	const uint32x4_t m_carry = vdupq_n_u32(0x01010100u);
	const uint32x4_t one = vdupq_n_u32(0x010101u);
	const uint32x4_t m_rgb = vdupq_n_u32(0x00FFFFFFu);
	const uint32x4_t m_alpha = vdupq_n_u32(0xFF000000u);
	int c = count;
	for (; c >= 4; c -= 4, p += 4) {
		uint32x4_t d = vld1q_u32(p);
		uint32x4_t sum = vaddq_u32(vandq_u32(d, m_fe), vandq_u32(vsrc, m_fe));
		uint32x4_t temp1 = vshrq_n_u32(vandq_u32(sum, m_carry), 7);
		uint32x4_t res = vorrq_u32(sum, vsubq_u32(one, temp1));
		res = vandq_u32(res, m_rgb);
		res = vorrq_u32(res, vandq_u32(vsrc, m_alpha));
		vst1q_u32(p, res);
	}
	for (; c >= 1; --c, ++p)
		*p = addColorsCrude_32(*p, col);
}

void rectfill_add_32_neon(BITMAP *where, int x1, int y1, int x2, int y2, Pixel colour, int fact) {
	CLIP_RECT();
	Pixel col = scaleColor_32(colour, fact);
	for (; y1 <= y2; ++y1)
		add_row_neon(reinterpret_cast<Pixel32 *>(where->line[y1]) + x1, x2 - x1 + 1, col);
}

void hline_add_32_neon(BITMAP *where, int x1, int y1, int x2, Pixel colour, int fact) {
	Pixel col = scaleColor_32(colour, fact);
	add_row_neon(reinterpret_cast<Pixel32 *>(where->line[y1]) + x1, x2 - x1 + 1, col);
}

// ---- NEON 8-bit sprite add row ----

static void sprite_add_row_neon(uint8_t *dest, const uint8_t *src, int count, int fact) {
	if (fact <= 0)
		return;
	int c = count;
	add8_align(dest, src, c, fact);
	const uint32x4_t m7f = vdupq_n_u32(0x7F7F7F7Fu);
	const uint32x4_t m80 = vdupq_n_u32(0x80808080u);
	const uint32x4_t one = vdupq_n_u32(0x01010101u);
	if (fact >= 255) {
		for (; c >= 16; c -= 16, dest += 16, src += 16) {
			uint32x4_t d = vreinterpretq_u32_u8(vld1q_u8(dest));
			uint32x4_t s = vreinterpretq_u32_u8(vld1q_u8(src));
			uint32x4_t a = vandq_u32(vshrq_n_u32(d, 1), m7f);
			uint32x4_t b = vandq_u32(vshrq_n_u32(s, 1), m7f);
			uint32x4_t sum = vaddq_u32(a, b);
			uint32x4_t temp1 = vshrq_n_u32(vandq_u32(sum, m80), 6);
			uint32x4_t res = vshlq_n_u32(vorrq_u32(sum, vsubq_u32(one, temp1)), 1);
			vst1q_u8(dest, vreinterpretq_u8_u32(res));
		}
	} else {
		const uint32x4_t m00ff = vdupq_n_u32(0x00FF00FFu);
		const uint32x4_t mff00 = vdupq_n_u32(0xFF00FF00u);
		const uint16x8_t vfact = vdupq_n_u16(static_cast<uint16_t>(fact));
		for (; c >= 16; c -= 16, dest += 16, src += 16) {
			uint32x4_t s = vreinterpretq_u32_u8(vld1q_u8(src));
			uint16x8_t t1 = vreinterpretq_u16_u32(vandq_u32(s, m00ff));
			uint16x8_t r1 = vmulq_u16(t1, vfact);
			uint32x4_t r1_32 = vandq_u32(vshrq_n_u32(vreinterpretq_u32_u16(r1), 8), m00ff);
			uint16x8_t t2 = vreinterpretq_u16_u32(vshrq_n_u32(vandq_u32(s, mff00), 8));
			uint16x8_t r2 = vmulq_u16(t2, vfact);
			uint32x4_t r2_32 = vandq_u32(vreinterpretq_u32_u16(r2), mff00);
			uint32x4_t sc = vorrq_u32(r1_32, r2_32);
			uint32x4_t d = vreinterpretq_u32_u8(vld1q_u8(dest));
			uint32x4_t a = vandq_u32(vshrq_n_u32(d, 1), m7f);
			uint32x4_t b = vandq_u32(vshrq_n_u32(sc, 1), m7f);
			uint32x4_t sum = vaddq_u32(a, b);
			uint32x4_t temp1 = vshrq_n_u32(vandq_u32(sum, m80), 6);
			uint32x4_t res = vshlq_n_u32(vorrq_u32(sum, vsubq_u32(one, temp1)), 1);
			vst1q_u8(dest, vreinterpretq_u8_u32(res));
		}
	}
	add8_tail(dest, src, c, fact);
}

void drawSpriteLine_add_8_neon(BITMAP *where, BITMAP *from, int x, int y, int x1, int y1, int x2, int fact) {
	if (bitmap_color_depth(from) != 8)
		return;
	CLIP_HLINE();
	sprite_add_row_neon(reinterpret_cast<uint8_t *>(where->line[y]) + x,
						reinterpret_cast<const uint8_t *>(from->line[y1]) + x1, x2 - x1, fact);
}

#endif

} // namespace Blitters

#endif // DEDSERV
