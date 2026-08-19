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

// SIMD replacement for the legacy MMX drawSprite_mult_8_to_32.  Reproduces the
// scalar reference scaleColor_32(*dest, *src) bit-for-bit (per channel
// (dest_ch * src) >> 8, alpha preserved from dest), which also fixes the old
// darkMode bug where the MMX pmulhuw-of-two-small-values path output black.
// Unaligned loads/stores throughout.

namespace Blitters {

#if defined(__SSE2__)

static void mult_row_sse2(Pixel32 *dest, const uint8_t *src, int count) {
	const __m128i m_rb = _mm_set1_epi32(0xFF00FF);
	const __m128i m_g = _mm_set1_epi32(0x00FF00);
	const __m128i m_a = _mm_set1_epi32(0xFF000000);
	const __m128i zero = _mm_setzero_si128();
	int c = count;
	for (; c >= 4; c -= 4, dest += 4, src += 4) {
		uint32_t fb;
		std::memcpy(&fb, src, 4);
		__m128i vbytes = _mm_cvtsi32_si128(static_cast<int>(fb));
		__m128i vf = _mm_unpacklo_epi16(_mm_unpacklo_epi8(vbytes, zero), zero);
		__m128i vf_rep = _mm_or_si128(vf, _mm_slli_epi32(vf, 16));
		__m128i vd = _mm_loadu_si128(reinterpret_cast<const __m128i *>(dest));
		__m128i t1 = _mm_and_si128(vd, m_rb);
		__m128i r1 = _mm_and_si128(_mm_srli_epi32(_mm_mullo_epi16(t1, vf_rep), 8), m_rb);
		__m128i t2 = _mm_and_si128(vd, m_g);
		__m128i r2 = _mm_and_si128(_mm_mullo_epi16(_mm_srli_epi32(t2, 8), vf_rep), m_g);
		__m128i res = _mm_or_si128(_mm_or_si128(r1, r2), _mm_and_si128(vd, m_a));
		_mm_storeu_si128(reinterpret_cast<__m128i *>(dest), res);
	}
	for (; c >= 1; --c, ++dest, ++src)
		*dest = scaleColor_32(*dest, *src);
}

AVX2_TARGET
static void mult_row_avx2(Pixel32 *dest, const uint8_t *src, int count) {
	const __m256i m_rb = _mm256_set1_epi32(0xFF00FF);
	const __m256i m_g = _mm256_set1_epi32(0x00FF00);
	const __m256i m_a = _mm256_set1_epi32(0xFF000000);
	int c = count;
	for (; c >= 8; c -= 8, dest += 8, src += 8) {
		uint64_t fb;
		std::memcpy(&fb, src, 8);
		__m256i vf = _mm256_cvtepu8_epi32(_mm_set_epi64x(0, static_cast<long long>(fb)));
		__m256i vd = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(dest));
		__m256i t1 = _mm256_and_si256(vd, m_rb);
		__m256i r1 = _mm256_and_si256(_mm256_srli_epi32(_mm256_mullo_epi32(t1, vf), 8), m_rb);
		__m256i t2 = _mm256_and_si256(vd, m_g);
		__m256i r2 = _mm256_and_si256(_mm256_mullo_epi32(_mm256_srli_epi32(t2, 8), vf), m_g);
		__m256i res = _mm256_or_si256(_mm256_or_si256(r1, r2), _mm256_and_si256(vd, m_a));
		_mm256_storeu_si256(reinterpret_cast<__m256i *>(dest), res);
	}
	for (; c >= 1; --c, ++dest, ++src)
		*dest = scaleColor_32(*dest, *src);
}

void drawSprite_mult_8_to_32_sse2(BITMAP *where, BITMAP *from, int x, int y, int cutl, int cutt, int cutr, int cutb) {
	if (bitmap_color_depth(where) != 32 || bitmap_color_depth(from) != 8)
		return;
	CLIP_SPRITE_REGION();
	for (; y1 < y2; ++y, ++y1)
		mult_row_sse2(reinterpret_cast<Pixel32 *>(where->line[y]) + x,
					  reinterpret_cast<const uint8_t *>(from->line[y1]) + x1, x2 - x1);
}

AVX2_TARGET
void drawSprite_mult_8_to_32_avx2(BITMAP *where, BITMAP *from, int x, int y, int cutl, int cutt, int cutr, int cutb) {
	if (bitmap_color_depth(where) != 32 || bitmap_color_depth(from) != 8)
		return;
	CLIP_SPRITE_REGION();
	for (; y1 < y2; ++y, ++y1)
		mult_row_avx2(reinterpret_cast<Pixel32 *>(where->line[y]) + x,
					  reinterpret_cast<const uint8_t *>(from->line[y1]) + x1, x2 - x1);
}

#elif defined(__ARM_NEON) || defined(__aarch64__)

static void mult_row_neon(Pixel32 *dest, const uint8_t *src, int count) {
	const uint32x4_t m_rb = vdupq_n_u32(0xFF00FFu);
	const uint32x4_t m_g = vdupq_n_u32(0x00FF00u);
	const uint32x4_t m_a = vdupq_n_u32(0xFF000000u);
	int c = count;
	for (; c >= 4; c -= 4, dest += 4, src += 4) {
		uint32_t fb;
		std::memcpy(&fb, src, 4);
		uint8x8_t v8 = vreinterpret_u8_u32(vcreate_u32(static_cast<uint64_t>(fb)));
		uint16x4_t v16 = vget_low_u16(vmovl_u8(v8));
		uint32x4_t vf = vmovl_u16(v16);
		uint32x4_t vf_rep = vorrq_u32(vf, vshlq_n_u32(vf, 16));
		uint32x4_t vd = vld1q_u32(dest);
		uint16x8_t t1 = vreinterpretq_u16_u32(vandq_u32(vd, m_rb));
		uint16x8_t vfr = vreinterpretq_u16_u32(vf_rep);
		uint16x8_t r1 = vmulq_u16(t1, vfr);
		uint32x4_t r1_32 = vandq_u32(vshrq_n_u32(vreinterpretq_u32_u16(r1), 8), m_rb);
		uint16x8_t t2 = vreinterpretq_u16_u32(vshrq_n_u32(vandq_u32(vd, m_g), 8));
		uint16x8_t r2 = vmulq_u16(t2, vfr);
		uint32x4_t r2_32 = vandq_u32(vreinterpretq_u32_u16(r2), m_g);
		uint32x4_t res = vorrq_u32(vorrq_u32(r1_32, r2_32), vandq_u32(vd, m_a));
		vst1q_u32(dest, res);
	}
	for (; c >= 1; --c, ++dest, ++src)
		*dest = scaleColor_32(*dest, *src);
}

void drawSprite_mult_8_to_32_neon(BITMAP *where, BITMAP *from, int x, int y, int cutl, int cutt, int cutr, int cutb) {
	if (bitmap_color_depth(where) != 32 || bitmap_color_depth(from) != 8)
		return;
	CLIP_SPRITE_REGION();
	for (; y1 < y2; ++y, ++y1)
		mult_row_neon(reinterpret_cast<Pixel32 *>(where->line[y]) + x,
					  reinterpret_cast<const uint8_t *>(from->line[y1]) + x1, x2 - x1);
}

#endif

} // namespace Blitters

#endif // DEDSERV
