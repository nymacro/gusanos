#include "bindings-gfx.h"

#include "luaapi/types.h"
#include "luaapi/macros.h"
#include "luaapi/classes.h"

#include "../glua.h"
#include "util/log.h"

#ifndef DEDSERV
#include "../viewport.h"
#include "../gfx.h"
#endif

#include <cmath>
#include <iostream>
#include <allegro.h>
using std::cerr;
using std::endl;
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
using boost::lexical_cast;

namespace LuaBindings
{
	
#ifndef DEDSERV
LuaReference ViewportMetaTable;
LuaReference BITMAPMetaTable;
BlitterContext blitter;
#endif

/*! gfx_draw_box(bitmap, x1, y1, x2, y2, r, g, b)

	Draws a filled box with the corners (x1, y1) and (x2, y2)
	with the color (r, g, b) using the currently selected blender.
*/
int l_gfx_draw_box(lua_State* L)
{
#ifndef DEDSERV
	LuaContext context(L);
	//BITMAP* b = *static_cast<BITMAP **>(lua_touserdata(L, 1));
	BITMAP* b = getObject<BITMAP>(context, 1);
	
	int x1 = lua_tointeger(L, 2);
	int y1 = lua_tointeger(L, 3);
	int x2 = lua_tointeger(L, 4);
	int y2 = lua_tointeger(L, 5);
	int cr = lua_tointeger(L, 6);
	int cg = lua_tointeger(L, 7);
	int cb = lua_tointeger(L, 8);
	
	blitter.rectfill(b, x1, y1, x2, y2, makecol(cr, cg, cb));
#endif
	return 0;
}

/*! gfx_set_alpha(alpha)

	Activates the alpha blender.
	//alpha// is a value in [0, 255] that specifies the opacity
	of things drawn after this is called.
*/
int l_gfx_set_alpha(lua_State* L)
{
#ifndef DEDSERV
	int alpha = lua_tointeger(L, 1);
	blitter.set(BlitterContext::alpha(), alpha);
#endif
	return 0;
}

/*! gfx_set_add(alpha)

	Activates the add blender.
	//alpha// is a value in [0, 255] that specifies the scaling factor
	of things drawn after this is called.
*/
int l_gfx_set_add(lua_State* L)
{
#ifndef DEDSERV
	int alpha = lua_tointeger(L, 1);
	blitter.set(BlitterContext::add(), alpha);
#endif
	return 0;
}

/*! gfx_reset_blending()

	Deactivates any blender that was active.
	Everything drawn after this is called will be drawn solid.
*/
int l_gfx_reset_blending(lua_State* L)
{
#ifndef DEDSERV
	blitter.set(BlitterContext::none());
#endif
	return 0;
}

#ifndef DEDSERV

/*! Viewport:get_bitmap()

	Returns the HUD bitmap of this viewport.
*/
METHOD(Viewport, viewport_getBitmap,
	context.pushFullReference(*p->getBitmap(), BITMAPMetaTable);
	return 1;
)

METHOD(Viewport, viewport_fromMap,
	lua_Integer x = lua_tointeger(context, 2);
	lua_Integer y = lua_tointeger(context, 3);
	
	IVec v(p->convertCoords(IVec(x, y)));
	context.push(v.x);
	context.push(v.y);
	return 2;
)

/*! Bitmap:w()

	Returns the width of this bitmap.
*/
METHOD(BITMAP, bitmap_w,
	lua_pushinteger(context, p->w);
	return 1;
)

/*! Bitmap:h()

	Returns the width of this bitmap.
*/
METHOD(BITMAP, bitmap_h,
	lua_pushinteger(context, p->h);
	return 1;
)

#endif


void initGfx()
{
	LuaContext& context = lua;

	context.functions()
		("gfx_draw_box", l_gfx_draw_box)
		("gfx_set_alpha", l_gfx_set_alpha)
		("gfx_set_add", l_gfx_set_add)
		("gfx_reset_blending", l_gfx_reset_blending)
	;


#ifndef DEDSERV
	// Viewport method and metatable
	
	CLASS(Viewport,
		("get_bitmap", l_viewport_getBitmap)
		("from_map", l_viewport_fromMap)
	)

	// Bitmap method and metatable
	
	CLASS(BITMAP,
		("w", l_bitmap_w)
		("h", l_bitmap_h)
	)
	
#endif	
}

}
