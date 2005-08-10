#ifndef LUA_BINDINGS_H
#define LUA_BINDINGS_H

#include "context.h"
#include <vector>
#include <string>
#include <list>

namespace LuaBindings
{
	void init();

	int print(lua_State* state);
	
	std::string runLua(int ref, std::list<std::string> const& args);
	void addGUIWndFunctions(LuaContext& context);
	void addGUIListFunctions(LuaContext& context);
	
	extern int playerIterator;
	extern int playerMetaTable;
	extern int fontMetaTable;
	extern std::vector<int> guiWndMetaTable;
}

#endif //LUA_BINDINGS_H
