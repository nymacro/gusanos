#ifndef OMFG_BLITTERS_BLITTERS_H
#define OMFG_BLITTERS_BLITTERS_H

#ifdef DEDSERV
#error "Can't use this in dedicated server"
#endif //DEDSERV

#include "allegro_compat.h"
#include "types.h"
//#include "mmx.h"

#define HAS_MMX (cpu_capabilities & CPU_MMX)
#define HAS_SSE (cpu_capabilities & CPU_SSE)
#define HAS_MMXSSE (cpu_capabilities & CPU_MMXPLUS)

//#define HAS_MMX (false)
//#define HAS_SSE (false)


#define FOR_MMX(x_) if(HAS_MMX) { x_ }

namespace Blitters
{

/*
	Naming:

	function [ - filter] - bitdepth [ - parallelism] [ - variant]

	e.g.:
	rectfill_blend_32_mmx

	defaults:
		parallelism = 1
		variant = C
*/

inline Pixel getpixel_32(BITMAP* where, int x, int y)
{
	return ((Pixel32 *)where->line[y])[x];
}

inline void putpixel_solid_32(BITMAP* where, int x, int y, Pixel color1)
{
	((Pixel32 *)where->line[y])[x] = color1;
}

void putpixel_add_32(BITMAP* where, int x, int y, Pixel color1);
void putpixel_addFact_32(BITMAP* where, int x, int y, Pixel color1, int fact);
void putpixelwu_add_32(BITMAP* where, float x, float y, Pixel color1, int fact);
void putpixel_blendHalf_32(BITMAP* where, int x, int y, Pixel color1);
void putpixel_blend_32(BITMAP* where, int x, int y, Pixel color1, int fact);
void putpixelwu_blend_32(BITMAP* where, float x, float y, Pixel color1, int fact);

inline void putpixel_solid_8(BITMAP* where, int x, int y, Pixel color1)
{
	((Pixel8 *)where->line[y])[x] = color1;
}

void rectfill_add_32(BITMAP* where, int x1, int y1, int x2, int y2, Pixel colour, int fact);
void rectfill_add_32_mmx(BITMAP* where, int x1, int y1, int x2, int y2, Pixel colour, int fact);
void rectfill_blend_32(BITMAP* where, int x1, int y1, int x2, int y2, Pixel colour, int fact);

void hline_add_32(BITMAP* where, int x1, int y1, int x2, Pixel colour, int fact);
void hline_add_32_mmx(BITMAP* where, int x1, int y1, int x2, Pixel colour, int fact);
void hline_blend_32(BITMAP* where, int x1, int y1, int x2, Pixel colour, int fact);

bool linewu_blend(BITMAP* where, float x, float y, float destx, float desty, Pixel colour, int fact);
bool linewu_add(BITMAP* where, float x, float y, float destx, float desty, Pixel colour, int fact);

void line_blend(BITMAP* where, int x, int y, int destx, int desty, Pixel colour, int fact);
void line_add(BITMAP* where, int x, int y, int destx, int desty, Pixel colour, int fact);

void drawSprite_multsec_32_with_8(BITMAP* where, BITMAP* from, BITMAP* secondary, int x, int y, int sx, int sy, int cutl, int cutt, int cutr, int cutb);

void drawSprite_blendtint_8_to_32(BITMAP* where, BITMAP* from, int x, int y, int cutl, int cutt, int cutr, int cutb, int fact, int color);
void drawSprite_mult_8_to_32(BITMAP* where, BITMAP* from, int x, int y, int cutl, int cutt, int cutr, int cutb);
void drawSprite_mult_8_to_32_mmx_sse(BITMAP* where, BITMAP* from, int x, int y, int cutl, int cutt, int cutr, int cutb);

void drawSpriteLine_add_32(BITMAP* where, BITMAP* from, int x, int y, int x1, int y1, int x2, int fact);
void drawSpriteLine_add_8(BITMAP* where, BITMAP* from, int x, int y, int x1, int y1, int x2, int fact);
void drawSpriteLine_add_8_mmx_sse(BITMAP* where, BITMAP* from, int x, int y, int x1, int y1, int x2, int fact);
} // namespace Blitters

using Blitters::linewu_blend;
using Blitters::linewu_add;
using Blitters::line_blend;
using Blitters::line_add;


#define CHECK_RANGE() \
	if((unsigned int)x >= (unsigned int)where->w \
	|| (unsigned int)y >= (unsigned int)where->h) \
		return

inline void putpixel_add(BITMAP* where, int x, int y, Pixel color1, int fact)
{
	CHECK_RANGE();
	Blitters::putpixel_addFact_32(where, x, y, color1, fact);
}

inline void putpixel_addFull(BITMAP* where, int x, int y, Pixel color1)
{
	CHECK_RANGE();
	Blitters::putpixel_add_32(where, x, y, color1);
}

inline void putpixelwu_add(BITMAP* where, float x, float y, Pixel color1, int fact)
{
	//blendwu blender checks range
	Blitters::putpixelwu_add_32(where, x, y, color1, fact);
}

inline void putpixel_blendHalf(BITMAP* where, int x, int y, Pixel color1)
{
	CHECK_RANGE();
	Blitters::putpixel_blendHalf_32(where, x, y, color1);
}

inline void putpixel_blend(BITMAP* where, int x, int y, Pixel color1, int fact)
{
	CHECK_RANGE();
	Blitters::putpixel_blend_32(where, x, y, color1, fact);
}

inline void putpixel_blendalpha(BITMAP* where, int x, int y, Pixel color1, int fact)
{
	CHECK_RANGE();
	Blitters::putpixel_blend_32(where, x, y, color1, fact);
}

inline void putpixelwu_blend(BITMAP* where, float x, float y, Pixel color1, int fact)
{
	//blendwu blender checks range
	Blitters::putpixelwu_blend_32(where, x, y, color1, fact);
}

inline void putpixelwu_blendalpha(BITMAP* where, float x, float y, Pixel color1, int fact)
{
	//blendwu blender checks range
	Blitters::putpixelwu_blend_32(where, x, y, color1, fact);
}

inline void putpixel_solid(BITMAP* where, int x, int y, Pixel color1)
{
	CHECK_RANGE();
	switch(bitmap_color_depth(where)) {
		case 8: Blitters::putpixel_solid_8(where, x, y, color1); break;
		case 32: Blitters::putpixel_solid_32(where, x, y, color1); break;
	}
}

inline void putpixelwu_solid(BITMAP* where, float x, float y, Pixel color1)
{
	//blendwu blender checks range
	Blitters::putpixelwu_blend_32(where, x, y, color1, 256);
}

inline void rectfill_add(BITMAP* where, int x1, int y1, int x2, int y2, Pixel colour, int fact)
{
	if(HAS_MMX) Blitters::rectfill_add_32_mmx(where, x1, y1, x2, y2, colour, fact);
	else Blitters::rectfill_add_32(where, x1, y1, x2, y2, colour, fact);
}

inline void rectfill_blend(BITMAP* where, int x1, int y1, int x2, int y2, Pixel colour, int fact)
{
	Blitters::rectfill_blend_32(where, x1, y1, x2, y2, colour, fact);
}

inline void rectfill_blendalpha(BITMAP* where, int x1, int y1, int x2, int y2, Pixel colour, int fact)
{
	Blitters::rectfill_blend_32(where, x1, y1, x2, y2, colour, fact);
}

inline void rectfill_solid(BITMAP* where, int x1, int y1, int x2, int y2, Pixel colour)
{
	rectfill(where, x1, y1, x2, y2, colour); //TODO: Make own
}

inline void hline_add(BITMAP* where, int x1, int y1, int x2, Pixel colour, int fact)
{
	if(HAS_MMX) Blitters::hline_add_32_mmx(where, x1, y1, x2, colour, fact);
	else Blitters::hline_add_32(where, x1, y1, x2, colour, fact);
}

inline void hline_blend(BITMAP* where, int x1, int y1, int x2, Pixel colour, int fact)
{
	Blitters::hline_blend_32(where, x1, y1, x2, colour, fact);
}

inline void hline_blendalpha(BITMAP* where, int x1, int y1, int x2, Pixel colour, int fact)
{
	Blitters::hline_blend_32(where, x1, y1, x2, colour, fact);
}

inline void hline_solid(BITMAP* where, int x1, int y1, int x2, Pixel colour)
{
	hline(where, x1, y1, x2, colour);
}

inline void linewu_blendalpha(BITMAP* where, float x, float y, float destx, float desty, Pixel colour, int fact)
{
	Blitters::linewu_blend(where, x, y, destx, desty, colour, fact);
}

inline void linewu_solid(BITMAP* where, float x, float y, float destx, float desty, Pixel colour)
{
	Blitters::linewu_blend(where, x, y, destx, desty, colour, 256);
}

inline void line_solid(BITMAP* where, int x, int y, int destx, int desty, Pixel colour)
{
	line(where, x, y, destx, desty, colour);
}

inline void line_blendalpha(BITMAP* where, int x, int y, int destx, int desty, Pixel colour, int fact)
{
	Blitters::line_blend(where, x, y, destx, desty, colour, fact);
}

inline void drawSprite_add(BITMAP* where, BITMAP* from, int x, int y, int fact)
{
	sdlBlitBlendMode(where, from, x, y, 0, 0, from->w, from->h, fact, SDL_BLENDMODE_ADD);
}

inline void drawSprite_blend(BITMAP* where, BITMAP* from, int x, int y, int fact)
{
	sdlBlitBlendMode(where, from, x, y, 0, 0, from->w, from->h, fact, SDL_BLENDMODE_BLEND);
}

inline void drawSprite_blendalpha(BITMAP* where, BITMAP* from, int x, int y, int fact)
{
	sdlBlitBlendMode(where, from, x, y, 0, 0, from->w, from->h, fact, SDL_BLENDMODE_BLEND);
}

inline void drawSprite_blendtint(BITMAP* where, BITMAP* from, int x, int y, int fact, int color)
{
	Blitters::drawSprite_blendtint_8_to_32(where, from, x, y, 0, 0, 0, 0, fact, color);
}

inline void drawSprite_solid(BITMAP* where, BITMAP* from, int x, int y)
{
	draw_sprite(where, from, x, y);
}

inline void drawSprite_mult_8(BITMAP* where, BITMAP* from, int x, int y)
{
	if(HAS_MMXSSE || HAS_SSE) Blitters::drawSprite_mult_8_to_32_mmx_sse(where, from, x, y, 0, 0, 0, 0);
	else Blitters::drawSprite_mult_8_to_32(where, from, x, y, 0, 0, 0, 0);
}

inline void drawSpriteCut_add(BITMAP* where, BITMAP* from, int x, int y, int cutl, int cutt, int cutr, int cutb, int fact)
{
	sdlBlitBlendMode(where, from, x, y, cutl, cutt, from->w - cutl - cutr, from->h - cutt - cutb, fact, SDL_BLENDMODE_ADD);
}

inline void drawSpriteCut_blend(BITMAP* where, BITMAP* from, int x, int y, int cutl, int cutt, int cutr, int cutb, int fact)
{
	sdlBlitBlendMode(where, from, x, y, cutl, cutt, from->w - cutl - cutr, from->h - cutt - cutb, fact, SDL_BLENDMODE_BLEND);
}

inline void drawSpriteCut_blendalpha(BITMAP* where, BITMAP* from, int x, int y, int cutl, int cutt, int cutr, int cutb, int fact)
{
	sdlBlitBlendMode(where, from, x, y, cutl, cutt, from->w - cutl - cutr, from->h - cutt - cutb, fact, SDL_BLENDMODE_BLEND);
}

inline void drawSpriteCut_solid(BITMAP* where, BITMAP* from, int x, int y, int cutl, int cutt, int cutr, int cutb)
{
	masked_blit(from, where, cutl, cutt, x+cutl, y+cutt
		, from->w - (cutl + cutr)
		, from->h - (cutt + cutb));
}

inline void drawSpriteLine_add(BITMAP* where, BITMAP* from, int x, int y, int x1, int y1, int x2, int fact)
{
	switch(bitmap_color_depth(where)) {
		case 8:
			if(HAS_MMXSSE || HAS_SSE) Blitters::drawSpriteLine_add_8_mmx_sse(where, from, x, y, x1, y1, x2, fact);
			else Blitters::drawSpriteLine_add_8(where, from, x, y, x1, y1, x2, fact);
			break;
		case 32: Blitters::drawSpriteLine_add_32(where, from, x, y, x1, y1, x2, fact); break;
	}
}

#endif //OMFG_BLITTERS_BLITTERS_H
