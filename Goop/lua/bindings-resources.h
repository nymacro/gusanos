#ifndef LUA_BINDINGS_RESOURCES_H
#define LUA_BINDINGS_RESOURCES_H

#include "luaapi/types.h"

namespace LuaBindings {
void initResources();

extern LuaReference FontMetaTable;
extern LuaReference SpriteSetMetaTable;
} // namespace LuaBindings

#endif // LUA_BINDINGS_RESOURCES_H
