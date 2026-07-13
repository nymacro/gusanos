#ifndef DEDSERV

#include "blitters.h"
#include "colors.h"
#include "macros.h"

#include <algorithm>

namespace Blitters
{

void putpixel_addFact_32(BITMAP* where, int x, int y, Pixel color1, int fact)
{
	Pixel32* p = ((Pixel32 *)where->line[y]) + x;

	*p = addColorsCrude_32(*p, scaleColor_32(color1, fact));
}

void putpixel_add_32(BITMAP* where, int x, int y, Pixel color1)
{
	Pixel32* p = ((Pixel32 *)where->line[y]) + x;

	*p = addColorsCrude_32(*p, color1);
}


void putpixelwu_add_32(BITMAP* where, float x, float y, Pixel color1, int fact)
{
	int xf = int(x * 256.f);
	int yf = int(y * 256.f);
	
	int xi = xf >> 8;
	int yi = yf >> 8;
	
	if((unsigned int)xi < (unsigned int)where->w - 1
	&& (unsigned int)yi < (unsigned int)where->h - 1)
	{
		int fx = xf & 0xFF;
		int fy = yf & 0xFF;
		
		Pixel32* urow = ((Pixel32 *)where->line[yi]) + xi;
		Pixel32* lrow = ((Pixel32 *)where->line[yi + 1]) + xi;
		
		int fyf = fy * fact;
		int fxf = fx * fact;
		int fxfs = fxf >> 8;
		int fyfs = fyf >> 8;
		
		int flr = (fx * fyf) >> 16;  //x * y * fact
		int fll = fyfs - flr;        //(1-x) * y * fact
		int fur = fxfs - flr;        //(1-y) * x * fact
		int ful = fact - fxfs - fll; //(1-x) * (1-y) * fact

		urow[0] = addColorsCrude_32(urow[0], scaleColor_32(color1, ful));
		urow[1] = addColorsCrude_32(urow[1], scaleColor_32(color1, fur));
		lrow[0] = addColorsCrude_32(lrow[0], scaleColor_32(color1, fll));
		lrow[1] = addColorsCrude_32(lrow[1], scaleColor_32(color1, flr));
	}
}

void rectfill_add_32(BITMAP* where, int x1, int y1, int x2, int y2, Pixel colour, int fact)
{
	typedef Pixel32 pixel_t_1;
	
	CLIP_RECT();
	
	Pixel col = scaleColor_32(colour, fact);

	RECT_Y_LOOP(
		RECT_X_LOOP(
			*p = addColorsCrude_32(*p, col)
		)
	)
}

void hline_add_32(BITMAP* where, int x1, int y1, int x2, Pixel colour, int fact)
{
	typedef Pixel32 pixel_t_1;
	
	Pixel col = scaleColor_32(colour, fact);

	RECT_X_LOOP(
		*p = addColorsCrude_32(*p, col)
	)
}

bool linewu_add(BITMAP* where, float x, float y, float destx, float desty, Pixel colour, int fact)
{
	const long prec = 8;
	const long one = (1 << prec);
	const long half = one / 2;
	const long fracmask = one - 1;
	long x1 = long(x * one);
	long y1 = long(y * one);
	long x2 = long(destx * one);
	long y2 = long(desty * one);
	
	long xdiff = x2 - x1;
	long ydiff = y2 - y1;
	
	#define BLEND32(dest_, src_, fact_) addColorsCrude_32(dest_, scaleColor_32(src_, fact_))
		
	if(labs(xdiff) > labs(ydiff))
		WULINE(x, y, 0, 1, 32, 0, 256, BLEND32)
	else
		WULINE(y, x, 1, 0, 32, 0, 256, BLEND32)

	#undef BLEND32

	return false;
}


void line_add(BITMAP* where, int x, int y, int destx, int desty, Pixel colour, int fact)
{
	#define BLEND32(dest_, src_) addColorsCrude_32(dest_, scaleColor_32(src_, fact))
		
	LINE(BLEND32, 32);

	#undef BLEND32
}

void drawSpriteLine_add_32(BITMAP* where, BITMAP* from, int x, int y, int x1, int y1, int x2, int fact)
{
	typedef Pixel32 pixel_t_1;
	
	if(bitmap_color_depth(from) != 32)
		return;
		
	CLIP_HLINE();

	
	if(fact >= 255)
	{
		// load_bitmap converts the magenta maskcolor (0xFFFF00FF) to transparent
		// black (0x00000000, alpha 0); skip alpha-0 mask pixels. addColorsCrude_32
		// takes its result alpha from the source, so feeding it the 0x00000000 mask
		// would zero the destination's alpha -- hence the alpha test, not the old
		// (now dead) 0xFFFF00FF comparison.
		SPRITE_X_LOOP(
			Pixel s = *src;
			if(s & 0xFF000000)
				*dest = addColorsCrude_32(*dest, s);
		)
	}
	else if(fact > 0)
	{
		SPRITE_X_LOOP(
			Pixel s = *src;
			if(s & 0xFF000000)
				*dest = addColorsCrude_32(*dest, scaleColor_32(s, fact));
		)
	}
}

void drawSpriteLine_add_8(BITMAP* where, BITMAP* from, int x, int y, int x1, int y1, int x2, int fact)
{
	typedef Pixel8 pixel_t_1;
	typedef Pixel8_4 pixel_t_2;

	if(bitmap_color_depth(from) != 8)
		return;
		
	CLIP_HLINE();
	
	if(fact >= 255)
	{
		SPRITE_X_LOOP_ALIGN(4, 4,
			*dest = addColorsCrude_8_4(*dest, *src)
		,
			*dest = addColorsCrude_8_4(*dest, *src)
		)
	}
	else if(fact > 0)
	{
		SPRITE_X_LOOP_ALIGN(4, 4,
			*dest = addColorsCrude_8_4(*dest, scaleColor_8_4(*src, fact))
		,
			*dest = addColorsCrude_8_4(*dest, scaleColor_8_4(*src, fact))
		)
	}
}

} //namespace Blitters

#endif
