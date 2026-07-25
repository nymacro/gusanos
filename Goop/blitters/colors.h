#ifndef OMFG_BLITTERS_COLORS_H
#define OMFG_BLITTERS_COLORS_H

#ifdef DEDSERV
#error "Can't use this in dedicated server"
#endif // DEDSERV

#include "allegro_compat.h"
#include "types.h"

namespace Blitters {

const Pixel maskcolor_32 = 0xFFFF00FF;

/*
	Naming:

	function - bitdepth [ - parallelism]

	e.g.:
	scaleColor_32

	defaults:
		parallelism = 1
*/

inline Pixel scaleColor_32(Pixel color, int fact) {
	Pixel temp1 = color & 0xFF00FF;
	Pixel temp2 = color & 0x00FF00;

	temp1 = ((temp1 * fact) >> 8) & 0xFF00FF;
	temp2 = ((temp2 * fact) >> 8) & 0x00FF00;

	return temp1 | temp2 | (color & 0xFF000000);
}

inline Pixel scaleColorHalf_32(Pixel color) {
	return (color & 0xFEFEFE) >> 1;
}

inline Pixel scaleColor_8_4(Pixel color, int fact) {
	Pixel temp1 = (color & 0x00FF00FF);
	Pixel temp2 = (color & 0xFF00FF00) >> 8;

	temp1 = ((temp1 * fact) >> 8) & 0x00FF00FF;
	temp2 = (temp2 * fact) & 0xFF00FF00;

	return temp1 | temp2;
}

inline Pixel32 addColorsCrude_32(Pixel color1, Pixel color2) {
	color1 = (color1 & 0xFEFEFF) + (color2 & 0xFEFEFF);
	Pixel32 temp1 = (color1 & 0x01010100) >> 7;
	color1 |= 0x010101 - temp1;
	return (color1 & 0xFFFFFF) | (color2 & 0xFF000000);
}

inline Pixel addColorsCrude_8_4(Pixel color1, Pixel color2) {
	color1 = ((color1 >> 1) & 0x7F7F7F7F) + ((color2 >> 1) & 0x7F7F7F7F);
	Pixel temp1 = (color1 & 0x80808080) >> 6;
	color1 |= 0x01010101 - temp1;
	return color1 << 1;
}

inline Pixel32 blendColorsHalf_32(Pixel color1, Pixel color2) {
	return ((color1 & 0xFEFEFE) >> 1) + ((color2 & 0xFEFEFE) >> 1) + (color1 & color2 & 0x010101);
}

inline Pixel32 blendColorsHalfCrude_32(Pixel color1, Pixel color2) {
	return ((color1 & 0xFEFEFE) >> 1) + ((color2 & 0xFEFEFE) >> 1);
}

inline void prepareBlendColorsHalfCrude_32(Pixel color2, Pixel &color2halved) {
	color2halved = ((color2 & 0xFEFEFE) >> 1);
}

inline Pixel32 blendColorsHalfCrude_32_prepared(Pixel color1, Pixel color2halved) {
	return ((color1 & 0xFEFEFE) >> 1) + color2halved;
}

inline Pixel blendColorsFact_32(Pixel color1, Pixel color2, int fact) {
	Pixel alpha = color2 & 0xFF000000; // Preserve alpha from color2
	Pixel res = (((color2 & 0xFF00FF) - (color1 & 0xFF00FF)) * fact >> 8) + color1;
	color1 &= 0xFF00;
	color2 &= 0xFF00;
	Pixel g = ((color2 - color1) * fact >> 8) + color1;

	res &= 0xFF00FF;
	g &= 0xFF00;

	return (res | g) | alpha;
}

// Does precomputation for a color in preparation for
// the four parameter version of blendColorsFact_32.
inline void prepareBlendColorsFact_32(Pixel color2, Pixel &color2rb, Pixel &color2g) {
	color2rb = color2 & 0xFF00FF;
	color2g = color2 & 0x00FF00;
}

/*
inline Pixel32 blendColorsFact_32(Pixel32 color1, Pixel32 color2rb, Pixel32 color2g, int fact)
{
	Pixel32 temp2 = color1 & 0xFF00FF;
	Pixel32 temp1;
	temp1 = ((((color2rb - temp2) * fact) >> 8) + temp2) & 0xFF00FF;
	color1 &= 0xFF00;
	temp2 = ((((color2g - color1) * fact) >> 8) + color1) & 0xFF00;
	return temp1 | temp2;

	Pixel res = (((color1 & 0xFF00FF) - color2rb) * fact >> 8) + color2;
	color1 &= 0xFF00;
	Pixel g = ((color1 - color2g) * fact >> 8) + color2;

	res &= 0xFF00FF;
	g &= 0xFF00;

	return res | g;
}*/

} // namespace Blitters

#endif
