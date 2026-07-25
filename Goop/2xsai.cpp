#include <string.h>
#include "allegro_compat.h"
#include "allegro_compat.h"
#include "2xsai.h"

#define uint32 unsigned long
#define uint16 unsigned short
#define uint8 unsigned char

static uint32 colorMask = 0xF7DEF7DE;
static uint32 lowPixelMask = 0x08210821;
static uint32 qcolorMask = 0xE79CE79C;
static uint32 qlowpixelMask = 0x18631863;
static uint32 redblueMask = 0xF81F;
static uint32 greenMask = 0x7E0;
static int PixelsPerMask = 2;
static int xsai_depth = 0;

int Init_2xSaI(int d) {

	int minr = 0, ming = 0, minb = 0;
	int i;

	if (d != 15 && d != 16 && d != 24 && d != 32)
		return -1;

	/* Get lowest color bit */
	for (i = 0; i < 255; i++) {
		if (!minr)
			minr = makecol(i, 0, 0);
		if (!ming)
			ming = makecol(0, i, 0);
		if (!minb)
			minb = makecol(0, 0, i);
	}

	colorMask = (makecol_depth(d, 255, 0, 0) - minr) | (makecol_depth(d, 0, 255, 0) - ming) |
				(makecol_depth(d, 0, 0, 255) - minb);
	lowPixelMask = minr | ming | minb;
	qcolorMask = (makecol_depth(d, 255, 0, 0) - 3 * minr) | (makecol_depth(d, 0, 255, 0) - 3 * ming) |
				 (makecol_depth(d, 0, 0, 255) - 3 * minb);
	qlowpixelMask = (minr * 3) | (ming * 3) | (minb * 3);
	redblueMask = makecol_depth(d, 255, 0, 255);
	greenMask = makecol_depth(d, 0, 255, 0);

	PixelsPerMask = (d <= 16) ? 2 : 1;

	if (PixelsPerMask == 2) {
		colorMask |= (colorMask << 16);
		qcolorMask |= (qcolorMask << 16);
		lowPixelMask |= (lowPixelMask << 16);
		qlowpixelMask |= (qlowpixelMask << 16);
	}

	TRACE("Color Mask:       0x%lX\n", colorMask);
	TRACE("Low Pixel Mask:   0x%lX\n", lowPixelMask);
	TRACE("QColor Mask:      0x%lX\n", qcolorMask);
	TRACE("QLow Pixel Mask:  0x%lX\n", qlowpixelMask);

	xsai_depth = d;

	return 0;
}

static int GetResult1(uint32 A, uint32 B, uint32 C, uint32 D) {
	int x = 0;
	int y = 0;
	int r = 0;
	if (A == C)
		x += 1;
	else if (B == C)
		y += 1;
	if (A == D)
		x += 1;
	else if (B == D)
		y += 1;
	if (x <= 1)
		r += 1;
	if (y <= 1)
		r -= 1;
	return r;
}

static int GetResult2(uint32 A, uint32 B, uint32 C, uint32 D, uint32 E) {
	int x = 0;
	int y = 0;
	int r = 0;
	if (A == C)
		x += 1;
	else if (B == C)
		y += 1;
	if (A == D)
		x += 1;
	else if (B == D)
		y += 1;
	if (x <= 1)
		r -= 1;
	if (y <= 1)
		r += 1;
	return r;
}

#define GET_RESULT(A, B, C, D) ((A != C || A != D) - (B != C || B != D))

#define INTERPOLATE(A, B) (((A & colorMask) >> 1) + ((B & colorMask) >> 1) + (A & B & lowPixelMask))

#define Q_INTERPOLATE(A, B, C, D)                                                                                      \
	((A & qcolorMask) >> 2) + ((B & qcolorMask) >> 2) + ((C & qcolorMask) >> 2) + ((D & qcolorMask) >> 2) +            \
		((((A & qlowpixelMask) + (B & qlowpixelMask) + (C & qlowpixelMask) + (D & qlowpixelMask)) >> 2) &              \
		 qlowpixelMask)

/* Clipping Macro, stolen from Allegro, modified to work with 2xSaI */
#define BLIT_CLIP2(src, dest, s_x, s_y, d_x, d_y, w, h, xscale, yscale)                                                \
	/* check for ridiculous cases */                                                                                   \
	if ((s_x >= src->cr) || (s_y >= src->cb) || (d_x >= dest->cr) || (d_y >= dest->cb))                                \
		return;                                                                                                        \
                                                                                                                       \
	if ((s_x + w < src->cl) || (s_y + h < src->ct) || (d_x + w * xscale < dest->cl) || (d_y + h * yscale < dest->ct))  \
		return;                                                                                                        \
                                                                                                                       \
	if (xscale < 1 || yscale < 1)                                                                                      \
		return;                                                                                                        \
                                                                                                                       \
	/* clip src left */                                                                                                \
	if (s_x < src->cl) {                                                                                               \
		w += s_x;                                                                                                      \
		d_x -= s_x * xscale;                                                                                           \
		s_x = src->cl;                                                                                                 \
	}                                                                                                                  \
                                                                                                                       \
	/* clip src top */                                                                                                 \
	if (s_y < src->ct) {                                                                                               \
		h += s_y;                                                                                                      \
		d_y -= s_y * yscale;                                                                                           \
		s_y = src->ct;                                                                                                 \
	}                                                                                                                  \
                                                                                                                       \
	/* clip src right */                                                                                               \
	if (s_x + w > src->cr)                                                                                             \
		w = src->cr - s_x;                                                                                             \
                                                                                                                       \
	/* clip src bottom */                                                                                              \
	if (s_y + h > src->cb)                                                                                             \
		h = src->cb - s_y;                                                                                             \
                                                                                                                       \
	/* clip dest left */                                                                                               \
	if (d_x < dest->cl) {                                                                                              \
		d_x -= dest->cl;                                                                                               \
		w += d_x / xscale;                                                                                             \
		s_x -= d_x / xscale;                                                                                           \
		d_x = dest->cl;                                                                                                \
	}                                                                                                                  \
                                                                                                                       \
	/* clip dest top */                                                                                                \
	if (d_y < dest->ct) {                                                                                              \
		d_y -= dest->ct;                                                                                               \
		h += d_y / yscale;                                                                                             \
		s_y -= d_y / yscale;                                                                                           \
		d_y = dest->ct;                                                                                                \
	}                                                                                                                  \
                                                                                                                       \
	/* clip dest right */                                                                                              \
	if (d_x + w * xscale > dest->cr)                                                                                   \
		w = (dest->cr - d_x) / xscale;                                                                                 \
                                                                                                                       \
	/* clip dest bottom */                                                                                             \
	if (d_y + h * yscale > dest->cb)                                                                                   \
		h = (dest->cb - d_y) / yscale;                                                                                 \
                                                                                                                       \
	/* bottle out if zero size */                                                                                      \
	if ((w <= 0) || (h <= 0))                                                                                          \
		return;

// static unsigned char *src_line[4];
// static unsigned char *dst_line[2];
