#include "bindings.h"

#include "../base_player.h"
#include "../base_worm.h"
#include "../gconsole.h"
#include "../game.h"
#include "../vec.h"
#include "../sprite_set.h"
#include "../sprite.h"
#include "../script.h"
#include "../font.h"
#include "../gfx.h"
#include "../viewport.h"
#include "../menu.h"
#include "../network.h"
#include "omfggui.h"
#include "omfggui_windows.h"
#include <cmath>
#include <string>
#include <list>
#include <iostream>
#include <vector>
using std::cerr;
using std::endl;
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
using boost::lexical_cast;

extern bool quit; // Extern this somewhere else (such as a gusanos.h)

namespace LuaBindings
{

int playerIterator = 0;
int playerMetaTable = 0;
int fontMetaTable = 0;
int wormMetaTable = 0;
int viewportMetaTable = 0;
std::vector<int> guiWndMetaTable;

template<class T>
inline void pushFullReference(T& x)
{
	T** i = (T **)lua_newuserdata (game.lua, sizeof(T*));
	*i = &x;
}

template<class T>
inline void pushFullReference(T& x, int metatable)
{
	T** i = (T **)lua_newuserdata (game.lua, sizeof(T*));
	*i = &x;
	game.lua.pushReference(metatable);
	if(!lua_istable(game.lua, -1))
		cerr << "Metatable is not a table!" << endl;
	if(!lua_setmetatable(game.lua, -2))
		cerr << "Couldn't set metatable!" << endl;
}

template<class T>
inline void pushObject(T const& x)
{
	T* i = (T *)lua_newuserdata (game.lua, sizeof(T));
	*i = x;
}

inline lua_Number luaL_checknumber(lua_State *L, int narg)
{
	lua_Number d = lua_tonumber(L, narg);
	if(d == 0 && !lua_isnumber(L, narg))
		; // TODO: tag_error(L, narg, LUA_TNUMBER);
	return d;
}

int print(lua_State* L)
{
	const char* s = lua_tostring(L, 1);
	console.addLogMsg(s);
	
	return 0;
}

int l_bind(lua_State* L)
{
/*
	const char* callback = lua_tostring(L, 1);
	const char* file = lua_tostring(L, 2);
	const char* function = lua_tostring(L, 3);
	game.luaCallbacks.bind(callback, file, function);*/
	
	char const* s = lua_tostring(L, 2);
	if(!s)
		return 0;
		
	lua_pushvalue(L, 3);
	int ref = game.lua.createReference();
	game.luaCallbacks.bind(s, ref);

	return 0;
}

int l_sqrt(lua_State* L)
{
	lua_pushnumber(L, sqrt(luaL_checknumber(L, 1)));
	return 1;
}

int l_abs(lua_State* L)
{
	lua_pushnumber(L, abs(luaL_checknumber(L, 1)));
	return 1;
}

int l_randomint(lua_State* L)
{
	int l = (int)luaL_checknumber(L, 1);
	int u = (int)luaL_checknumber(L, 2);
	
	lua_pushnumber(L, l + (unsigned int)(rndgen()) % (u - l + 1));
	
	return 1;
}

int l_randomfloat(lua_State* L)
{
	lua_Number l = luaL_checknumber(L, 1);
	lua_Number u = luaL_checknumber(L, 2);
	
	lua_pushnumber(L, l + rnd() * (u - l));
	
	return 1;
}

int l_vector(lua_State* L)
{
	Vec& vec = *(Vec *)lua_newuserdata (L, sizeof(Vec));
	vec.x = (float)luaL_checknumber(L, 1);
	vec.y = (float)luaL_checknumber(L, 1);
	// TODO: Set metatable
	return 1;
}

int l_vector_add(lua_State* L)
{
	Vec& vecA = *(Vec *)lua_touserdata(L, 1);
	Vec& vecB = *(Vec *)lua_touserdata(L, 2);
	
	Vec& vec = *(Vec *)lua_newuserdata (L, sizeof(Vec));
	vec = vecA + vecB;

	return 1;
}

int l_vector_tostring(lua_State* L)
{
	Vec& vec = *(Vec *)lua_touserdata(L, 1);

	std::string s = "(" + lexical_cast<std::string>(vec.x) + ", " + lexical_cast<std::string>(vec.y) + ")";
	lua_pushstring(L, s.c_str());

	return 1;
}

int l_vector_lengthSqr(lua_State* L)
{
	Vec& vec = *(Vec *)lua_touserdata(L, 1);

	lua_pushnumber(L, vec.lengthSqr());

	return 1;
}

int l_vector_length(lua_State* L)
{
	Vec& vec = *(Vec *)lua_touserdata(L, 1);

	lua_pushnumber(L, vec.length());

	return 1;
}

int l_sprites_load(lua_State* L)
{
	const char* n = lua_tostring(L, 1);

	SpriteSet *s = spriteList.load(n);
	lua_pushlightuserdata(L, s);

	return 1;
}

int l_sprites_render(lua_State* L)
{
	SpriteSet* s = (SpriteSet *)lua_touserdata(L, 1);
	if(!s)
		return 0;
		
	BITMAP* b = (BITMAP *)lua_touserdata(L, 2);
		
	int frame = (int)lua_tonumber(L, 3);
	int x = (int)lua_tonumber(L, 4);
	int y = (int)lua_tonumber(L, 5);
	s->getSprite(frame)->draw(b, x, y);

	return 0;
}

int l_font_load(lua_State* L)
{
	char const* n = lua_tostring(L, 1);

	Font *f = fontLocator.load(n);
	if(!f)
	{
		lua_pushnil(L);
		return 1;
	}
	pushFullReference(*f);
	game.lua.pushReference(LuaBindings::fontMetaTable);
	if(!lua_istable(L, -1))
		cerr << "Metatable is not a table!" << endl;
	if(!lua_setmetatable(L, -2))
		cerr << "Couldn't set player metatable!" << endl;

	return 1;
}

int l_font_render(lua_State* L)
{
	Font *f = *(Font **)lua_touserdata(L, 1);
	if(!f || lua_gettop(L) < 5)
		return 0;
		
	BITMAP* b = (BITMAP *)lua_touserdata(L, 2);
	
	char const* s = lua_tostring(L, 3);
	if(!s)
		return 0;
		
	int x = static_cast<int>(lua_tonumber(L, 4));
	int y = static_cast<int>(lua_tonumber(L, 5));
	
	if(lua_gettop(L) >= 8)
	{
		int cr = static_cast<int>(lua_tonumber(L, 6));
		int cg = static_cast<int>(lua_tonumber(L, 7));
		int cb = static_cast<int>(lua_tonumber(L, 8));
		f->draw(b, s, x, y, 0, cr, cg, cb);
	}
	else
		f->draw(b, s, x, y, 0);
	
	return 0;
}

int l_map_is_loaded(lua_State* L)
{
	lua_pushboolean(L, game.level.loaded);
	
	return 1;
}

int l_quit(lua_State* L)
{
	quit = true;
	return 0;
}

int l_game_players(lua_State* L)
{
	game.lua.pushReference(LuaBindings::playerIterator);
	typedef std::list<BasePlayer*>::iterator iter;
	iter& i = *(iter *)lua_newuserdata (L, sizeof(iter));
	i = game.players.begin();
	lua_pushnil(L);
	
	return 3;
}

int l_player_kills(lua_State* L)
{
	BasePlayer* p = *static_cast<BasePlayer **>(lua_touserdata (L, 1));
	lua_pushnumber(L, p->kills);

	return 1;
}

int l_player_deaths(lua_State* L)
{
	BasePlayer* p = *static_cast<BasePlayer **>(lua_touserdata (L, 1));
	lua_pushnumber(L, p->deaths);

	return 1;
}

int l_player_name(lua_State* L)
{
	BasePlayer* p = *static_cast<BasePlayer **>(lua_touserdata (L, 1));
	lua_pushstring(L, p->m_name.c_str());

	return 1;
}

void pushPlayer(BasePlayer* player)
{
	pushFullReference(*player, LuaBindings::playerMetaTable);
}

int l_worm_getPlayer(lua_State* L)
{
	BaseWorm* p = *static_cast<BaseWorm **>(lua_touserdata (L, 1));
	if(!p->getOwner())
		return 0;
	game.lua.pushReference(p->getOwner()->luaReference);
	return 1;
}


int l_worm_getHealth(lua_State* L)
{
	BaseWorm* p = *static_cast<BaseWorm **>(lua_touserdata (L, 1));
	lua_pushnumber(L, p->getHealth());
	return 1;
}

void pushWorm(BaseWorm* worm)
{
	pushFullReference(*worm, LuaBindings::wormMetaTable);
}

int l_viewport_getBitmap(lua_State* L)
{
	Viewport* p = *static_cast<Viewport **>(lua_touserdata (L, 1));

	lua_pushlightuserdata(L, (void *)p->getBitmap());
	return 1;
}

void pushViewport(Viewport* viewport)
{
	pushFullReference(*viewport, LuaBindings::viewportMetaTable);
}

int l_game_playerIterator(lua_State* L)
{
	typedef std::list<BasePlayer*>::iterator iter;
	iter& i = *(iter *)lua_touserdata(L, 1);
	if(i == game.players.end())
		lua_pushnil(L);
	else
	{
		//lua_pushlightuserdata(L, *i);
		game.lua.pushReference((*i)->luaReference);
		/*
		BasePlayer** p = (BasePlayer **)lua_newuserdata(L, sizeof(BasePlayer *));
		*p = *i;
		game.lua.pushReference(LuaBindings::playerMetaTable);
		if(!lua_istable(L, -1))
			cerr << "Metatable is not a table!" << endl;
		if(!lua_setmetatable(L, -2))
			cerr << "Couldn't set player metatable!" << endl;*/
		++i;
	}
	
	return 1;
}

int l_gui_find(lua_State* L)
{
	char const* s = lua_tostring(L, 1);
	OmfgGUI::Wnd* w = OmfgGUI::menu.findNamedWindow(s);
	if(!w)
	{
		lua_pushnil(L);
		return 1;
	}
	
	cerr << "Window " << s << " @ " << w << endl;
	
	OmfgGUI::Wnd** wp = (OmfgGUI::Wnd **)lua_newuserdata(L, sizeof(OmfgGUI::Wnd *));
	*wp = w;
	game.lua.pushReference(LuaBindings::guiWndMetaTable[w->classID()]);
	if(!lua_setmetatable(L, -2))
	{
		cerr << "Failed to set metatable for window " << s << "!" << endl;
	}
	
	return 1;
}

int l_gui_wnd_attribute(lua_State* L)
{
	OmfgGUI::Wnd* p = *static_cast<OmfgGUI::Wnd **>(lua_touserdata (L, 1));
	char const* name = lua_tostring(L, 2);
	std::string res;
	if(p->getAttrib(name, res))
		lua_pushstring(L, res.c_str());
	else
		lua_pushnil(L);
	
	return 1;
}

int l_gui_wnd_set_visibility(lua_State* L)
{
	OmfgGUI::Wnd* p = *static_cast<OmfgGUI::Wnd **>(lua_touserdata (L, 1));
	
	p->setVisibility(lua_toboolean(L, 2));

	return 0;
}

int l_gui_wnd_get_text(lua_State* L)
{
	OmfgGUI::Wnd* p = *static_cast<OmfgGUI::Wnd **>(lua_touserdata (L, 1));

	if(p)
		lua_pushstring(L, p->getText().c_str());
	else
		lua_pushnil(L);
	
	return 1;
}

int l_gui_wnd_focus(lua_State* L)
{
	OmfgGUI::Wnd* p = *static_cast<OmfgGUI::Wnd **>(lua_touserdata (L, 1));

	OmfgGUI::menu.setFocus(p);
	
	return 0;
}

int l_gui_list_insert(lua_State* L)
{
	OmfgGUI::List* p = *static_cast<OmfgGUI::List **>(lua_touserdata (L, 1));
	
	int c = lua_gettop(L);
	OmfgGUI::List::Node* n = new OmfgGUI::List::Node("");
	p->push_back(n);
	for(int i = 2; i <= c; ++i)
		n->setText(i - 2, lua_tostring(L, i));
	
	//TODO: Push reference to element
	
	return 0;
}

int l_gui_list_clear(lua_State* L)
{
	OmfgGUI::List* p = *static_cast<OmfgGUI::List **>(lua_touserdata (L, 1));
	
	p->clear();

	return 0;
}

int l_gui_list_sort(lua_State* L)
{
	OmfgGUI::List* p = *static_cast<OmfgGUI::List **>(lua_touserdata (L, 1));
	unsigned int column = static_cast<unsigned int>(lua_tonumber(L, 2));
	
	p->sortNumerically(column);

	return 0;
}

int l_gui_list_add_column(lua_State* L)
{
	OmfgGUI::List* p = *static_cast<OmfgGUI::List **>(lua_touserdata (L, 1));
	char const* name = lua_tostring(L, 2);
	lua_Number widthFactor = lua_tonumber(L, 3);
	
	p->addColumn(OmfgGUI::List::ColumnHeader(name, widthFactor));

	return 0;
}

int l_connect(lua_State* L)
{
	char const* s = lua_tostring(L, 1);
	if(!s)
		return 0;
	network.connect(s);
	return 0;
}

int l_console_register_command(lua_State* L)
{
	char const* name = lua_tostring(L, 1);
	lua_pushvalue(L, 2);
	int ref = game.lua.createReference();
	
	console.registerCommands()
			(name, boost::bind(LuaBindings::runLua, ref, _1), true);
			
	return 0;
}

int l_load_script(lua_State* L)
{
	char const* s = lua_tostring(L, 2);
	if(!s)
		return 0;
	Script* script = scriptLocator.load(s);
	
	if(!script)
		return 0;
	
	// Return the allocated table
	lua_pushvalue(L, 2);
	lua_rawget(L, LUA_GLOBALSINDEX);
	return 1;
}

int l_gfx_draw_box(lua_State* L)
{
	BITMAP* b = (BITMAP *)lua_touserdata(L, 1);
	
	int x1 = (int)lua_tonumber(L, 2);
	int y1 = (int)lua_tonumber(L, 3);
	int x2 = (int)lua_tonumber(L, 4);
	int y2 = (int)lua_tonumber(L, 5);
	int cr = static_cast<int>(lua_tonumber(L, 6));
	int cg = static_cast<int>(lua_tonumber(L, 7));
	int cb = static_cast<int>(lua_tonumber(L, 8));
	
	rectfill(b, x1, y1, x2, y2, makecol(cr, cg, cb));
	
	return 0;
}

int l_gfx_set_alpha(lua_State* L)
{
	int alpha = (int)lua_tonumber(L, 1);
	gfx.setBlender(ALPHA, alpha);
	
	return 0;
}

int l_gfx_reset_blending(lua_State* L)
{
	solid_mode();
	
	return 0;
}

std::string runLua(int ref, std::list<std::string> const& args)
{
	game.lua.pushReference(ref);
	int params = 0;
	for(std::list<std::string>::const_iterator i = args.begin();
		i != args.end();
		++i)
	{
		lua_pushstring(game.lua, i->c_str());
		++params;
	}
	
	int r = game.lua.call(params, 1);
	if(r != 1)
		return "";
		
	char const* s = lua_tostring(game.lua, -1);
	if(s)
	{
		std::string ret(s);
		lua_settop(game.lua, -2);
		return ret;
	}
	
	lua_settop(game.lua, -2);
	return "";
}

void addGUIWndFunctions(LuaContext& context)
{
	lua_pushstring(context, "attribute");
	lua_pushcfunction(context, l_gui_wnd_attribute);
	lua_rawset(context, -3);
	
	lua_pushstring(context, "set_visibility");
	lua_pushcfunction(context, l_gui_wnd_set_visibility);
	lua_rawset(context, -3);
	
	lua_pushstring(context, "get_text");
	lua_pushcfunction(context, l_gui_wnd_get_text);
	lua_rawset(context, -3);
	
	lua_pushstring(context, "focus");
	lua_pushcfunction(context, l_gui_wnd_focus);
	lua_rawset(context, -3);
}

void addGUIListFunctions(LuaContext& context)
{
	lua_pushstring(context, "insert");
	lua_pushcfunction(context, l_gui_list_insert);
	lua_rawset(context, -3);
	
	lua_pushstring(context, "clear");
	lua_pushcfunction(context, l_gui_list_clear);
	lua_rawset(context, -3);

	lua_pushstring(context, "add_column");
	lua_pushcfunction(context, l_gui_list_add_column);
	lua_rawset(context, -3);
	
	lua_pushstring(context, "sort");
	lua_pushcfunction(context, l_gui_list_sort);
	lua_rawset(context, -3);
}


void init()
{
	LuaContext& context = game.lua;
	
	context.function("print", print);
	context.function("sqrt", l_sqrt);
	context.function("abs", l_abs);
	
	context.function("randomint", l_randomint);
	context.function("randomfloat", l_randomfloat);
	
	context.function("vector", l_vector);
	context.function("vector_add", l_vector_add);
	context.function("vector_tostring", l_vector_tostring);
	context.function("vector_lengthSqr", l_vector_lengthSqr);
	context.function("vector_length", l_vector_length);
	
	context.function("sprites_load", l_sprites_load);
	context.function("sprites_render", l_sprites_render);
	
	context.function("font_load", l_font_load);
	
	context.function("map_is_loaded", l_map_is_loaded);
	context.function("console_register_command", l_console_register_command);
	
	context.function("game_players", l_game_players);
	lua_pushcfunction(context, l_game_playerIterator);
	playerIterator = context.createReference();
	
	context.function("quit", l_quit);
	
	//context.function("player_kills", l_player_kills);
	//context.function("player_name", l_player_name);
	
	context.function("gui_find", l_gui_find);
	
	context.function("gfx_draw_box", l_gfx_draw_box);
	context.function("gfx_set_alpha", l_gfx_set_alpha);
	context.function("gfx_reset_blending", l_gfx_reset_blending);
	
	context.function("bind", l_bind);
	
	context.function("connect", l_connect);
	
	
	
	/*
	lua_newtable(context);
	lua_pushstring(context, "__add");
	lua_pushcfunction(context, l_vector_add);
	lua_rawset(context, -3);*/
	
	// Player method and metatable
	
	lua_newtable(context); 
	lua_pushstring(context, "__index");
	
	lua_newtable(context);
	
	lua_pushstring(context, "kills");
	lua_pushcfunction(context, l_player_kills);
	lua_rawset(context, -3);
	
	lua_pushstring(context, "deaths");
	lua_pushcfunction(context, l_player_deaths);
	lua_rawset(context, -3);
	
	lua_pushstring(context, "name");
	lua_pushcfunction(context, l_player_name);
	lua_rawset(context, -3);
	
	lua_rawset(context, -3);
	playerMetaTable = context.createReference();
	
	// Worm method and metatable
	
	lua_newtable(context); 
	lua_pushstring(context, "__index");
	
	lua_newtable(context);
	
	lua_pushstring(context, "get_player");
	lua_pushcfunction(context, l_worm_getPlayer);
	lua_rawset(context, -3);
	
	lua_pushstring(context, "get_health");
	lua_pushcfunction(context, l_worm_getHealth);
	lua_rawset(context, -3);
		
	lua_rawset(context, -3);
	wormMetaTable = context.createReference();
	
	// Viewport method and metatable
	
	lua_newtable(context); 
	lua_pushstring(context, "__index");
	
	lua_newtable(context);
	
	lua_pushstring(context, "get_bitmap");
	lua_pushcfunction(context, l_viewport_getBitmap);
	lua_rawset(context, -3);
	
	lua_rawset(context, -3);
	viewportMetaTable = context.createReference();

	// Font method and metatable
	
	lua_newtable(context); 
	lua_pushstring(context, "__index");
	
	lua_newtable(context);
	
	lua_pushstring(context, "render");
	lua_pushcfunction(context, l_font_render);
	lua_rawset(context, -3);
		
	lua_rawset(context, -3);
	fontMetaTable = context.createReference();
	
	// GUI Wnd method and metatable
	
	guiWndMetaTable.resize(OmfgGUI::Context::WndCount);
	
	lua_newtable(context); 
	lua_pushstring(context, "__index");
	
	lua_newtable(context);
	
	addGUIWndFunctions(context);

	lua_rawset(context, -3);
	int ref = context.createReference();
	guiWndMetaTable[OmfgGUI::Context::Unknown] = ref;
	guiWndMetaTable[OmfgGUI::Context::Button] = ref;
	guiWndMetaTable[OmfgGUI::Context::Edit] = ref;
	guiWndMetaTable[OmfgGUI::Context::Group] = ref;
	
	// GUI List method and metatable
	
	lua_newtable(context);
	lua_pushstring(context, "__index");
	
	lua_newtable(context);
	
	addGUIWndFunctions(context);
	addGUIListFunctions(context);

	lua_rawset(context, -3);
	guiWndMetaTable[OmfgGUI::Context::List] = context.createReference();
	
	// Global metatable
	
	lua_pushvalue(context, LUA_GLOBALSINDEX);
	
	lua_newtable(context);
	lua_pushstring(context, "__index");
	lua_pushcfunction(context, l_load_script);
	lua_rawset(context, -3);
	
	lua_setmetatable(context, -2);
	
	// Bindings table and metatable
	lua_pushstring(context, "bindings");
	lua_newtable(context); // Bindings table
	
	lua_newtable(context); // Bindings metatable
	lua_pushstring(context, "__newindex");
	lua_pushcfunction(context, l_bind);
	lua_rawset(context, -3);
	
	lua_setmetatable(context, -2);

	lua_rawset(context, LUA_GLOBALSINDEX);
	
	cerr << "LuaBindings::init() done." << endl;
}

}