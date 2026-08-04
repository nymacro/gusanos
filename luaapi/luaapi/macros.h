#ifndef LUA_MACROS_H
#define LUA_MACROS_H

// METHOD: define a Lua method `name_` on userdata type `type_`. `p` is the object
// read from stack 1; `body_` is the implementation; returns 0 if self is nil.
#define METHOD(type_, name_, body_...)                                                                                 \
	int l_##name_(lua_State *L_) {                                                                                     \
		LuaContext context(L_);                                                                                        \
		void *p2 = lua_touserdata(context, 1);                                                                         \
		if (!p2)                                                                                                       \
			return 0;                                                                                                  \
		type_ *p = *static_cast<type_ **>(p2);                                                                         \
		body_                                                                                                          \
	}

// METHODC: like METHOD, but raises a Lua error on an invalid self, which
// catches the common '.'-vs-':' call misuse.
#define METHODC(type_, name_, body_...)                                                                                \
	int l_##name_(lua_State *L_) {                                                                                     \
		LuaContext context(L_);                                                                                        \
		if (type_ *p = getObject<type_>(context, 1)) {                                                                 \
			body_                                                                                                      \
		} else {                                                                                                       \
			lua_pushstring(context, "Method called on invalid object. Did you use '.' instead of ':'?");               \
			lua_error(context);                                                                                        \
			return 0;                                                                                                  \
		}                                                                                                              \
	}

// LMETHOD: like METHOD for light userdata (no LuaObject wrapper); `p` is the
// raw pointer read from stack 1.
#define LMETHOD(type_, name_, body_...)                                                                                \
	int l_##name_(lua_State *L_) {                                                                                     \
		LuaContext context(L_);                                                                                        \
		type_ *p = static_cast<type_ *>(lua_touserdata(context, 1));                                                   \
		if (!p)                                                                                                        \
			return 0;                                                                                                  \
		body_                                                                                                          \
	}

// LMETHODC: LMETHOD with an invalid-self Lua error.
#define LMETHODC(type_, name_, body_...)                                                                               \
	int l_##name_(lua_State *L_) {                                                                                     \
		LuaContext context(L_);                                                                                        \
		if (type_ *p = getLObject<type_>(context, 1)) {                                                                \
			body_                                                                                                      \
		} else {                                                                                                       \
			lua_pushstring(context, "Method called on invalid object. Did you use '.' instead of ':'?");               \
			lua_error(context);                                                                                        \
			return 0;                                                                                                  \
		}                                                                                                              \
	}

// BINOP: define a Lua binary operator `name_` on `type_`; `a`/`b` are the
// operands from stack 1/2; returns 0 if either operand is nil.
#define BINOP(type_, name_, body_...)                                                                                  \
	int l_##name_(lua_State *L_) {                                                                                     \
		LuaContext context(L_);                                                                                        \
		void *a_ = lua_touserdata(context, 1);                                                                         \
		if (!a_)                                                                                                       \
			return 0;                                                                                                  \
		void *b_ = lua_touserdata(context, 2);                                                                         \
		if (!b_)                                                                                                       \
			return 0;                                                                                                  \
		type_ *a = *static_cast<type_ **>(a_);                                                                         \
		type_ *b = *static_cast<type_ **>(b_);                                                                         \
		body_                                                                                                          \
	}

// LBINOP: BINOP for light userdata operands.
#define LBINOP(type_, name_, body_...)                                                                                 \
	int l_##name_(lua_State *L_) {                                                                                     \
		LuaContext context(L_);                                                                                        \
		type_ *a = static_cast<type_ *>(lua_touserdata(context, 1));                                                   \
		if (!a)                                                                                                        \
			return 0;                                                                                                  \
		type_ *b = static_cast<type_ *>(lua_touserdata(context, 2));                                                   \
		if (!b)                                                                                                        \
			return 0;                                                                                                  \
		body_                                                                                                          \
	}

// CLASS: build a Lua metatable for `name_` (methods in `body_`), register it,
// and store its registry ref in `name_##MetaTable`.
#define CLASS(name_, body_...)                                                                                         \
	{                                                                                                                  \
		lua_newtable(context);                                                                                         \
		lua_pushstring(context, "__index");                                                                            \
		lua_newtable(context);                                                                                         \
		context.tableFunctions() body_;                                                                                \
		lua_rawset(context, -3);                                                                                       \
		context.tableSetField(LuaID<name_>::value);                                                                    \
		name_##MetaTable = context.createReference();                                                                  \
	}

/*
#define CLASSID(name_, id_, body_) { \
	lua_newtable(context); \
	lua_pushstring(context, "__index"); \
	lua_newtable(context); \
	context.tableFunctions() \
		body_ ; \
	context.tableSetField(id_); \
	lua_rawset(context, -3); \
	name_##MetaTable = context.createReference(); }
	*/
// CLASS_: like CLASS but store the metatable ref in `name_::metaTable` (static
// member form) instead of `name_##MetaTable`.
#define CLASS_(name_, body_...)                                                                                        \
	{                                                                                                                  \
		lua_newtable(context);                                                                                         \
		lua_pushstring(context, "__index");                                                                            \
		lua_newtable(context);                                                                                         \
		context.tableFunctions() body_;                                                                                \
		lua_rawset(context, -3);                                                                                       \
		context.tableSetField(LuaID<name_>::value);                                                                    \
		name_::metaTable = context.createReference();                                                                  \
	}

// CLASSM: like CLASS plus metamethods (`meta_`) bound before __index.
#define CLASSM(name_, meta_, body_...)                                                                                 \
	{                                                                                                                  \
		lua_newtable(context);                                                                                         \
		context.tableFunctions() meta_;                                                                                \
		lua_pushstring(context, "__index");                                                                            \
		lua_newtable(context);                                                                                         \
		context.tableFunctions() body_;                                                                                \
		lua_rawset(context, -3);                                                                                       \
		context.tableSetField(LuaID<name_>::value);                                                                    \
		name_##MetaTable = context.createReference();                                                                  \
	}

// CLASSM_: CLASSM with the `name_::metaTable` static-member form.
#define CLASSM_(name_, meta_, body_...)                                                                                \
	{                                                                                                                  \
		lua_newtable(context);                                                                                         \
		context.tableFunctions() meta_;                                                                                \
		lua_pushstring(context, "__index");                                                                            \
		lua_newtable(context);                                                                                         \
		context.tableFunctions() body_;                                                                                \
		lua_rawset(context, -3);                                                                                       \
		context.tableSetField(LuaID<name_>::value);                                                                    \
		name_::metaTable = context.createReference();                                                                  \
	}

// ENUM: create a Lua global table `name_` populated by context.tableItems()
// in `body_`.
#define ENUM(name_, body_)                                                                                             \
	{                                                                                                                  \
		lua_pushstring(context, #name_);                                                                               \
		lua_newtable(context);                                                                                         \
		context.tableItems() body_;                                                                                    \
		lua_rawset(context, LUA_GLOBALSINDEX);                                                                         \
	}

// REQUEST_TABLE: create a Lua global table `name_` whose __index metamethod
// calls the C function `func_`.
#define REQUEST_TABLE(name_, func_)                                                                                    \
	{                                                                                                                  \
		lua_pushstring(context, name_);                                                                                \
		lua_newtable(context);                                                                                         \
		lua_newtable(context);                                                                                         \
		lua_pushstring(context, "__index");                                                                            \
		lua_pushcfunction(context, func_);                                                                             \
		lua_rawset(context, -3);                                                                                       \
		lua_setmetatable(context, -2);                                                                                 \
		lua_rawset(context, LUA_GLOBALSINDEX);                                                                         \
	}

// SHADOW_TABLE: create a Lua global table `name_` with __index (`get_`) and
// __newindex (`set_`) metamethods.
#define SHADOW_TABLE(name_, get_, set_)                                                                                \
	{                                                                                                                  \
		lua_pushstring(context, name_);                                                                                \
		lua_newtable(context);                                                                                         \
		lua_newtable(context);                                                                                         \
		lua_pushstring(context, "__index");                                                                            \
		lua_pushcfunction(context, get_);                                                                              \
		lua_rawset(context, -3);                                                                                       \
		lua_pushstring(context, "__newindex");                                                                         \
		lua_pushcfunction(context, set_);                                                                              \
		lua_rawset(context, -3);                                                                                       \
		lua_setmetatable(context, -2);                                                                                 \
		lua_rawset(context, LUA_GLOBALSINDEX);                                                                         \
	}

#endif
