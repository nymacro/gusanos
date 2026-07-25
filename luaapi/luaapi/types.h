#ifndef LUA_TYPES_H
#define LUA_TYPES_H

struct LuaReference {
	LuaReference() : idx(0) {}

	explicit LuaReference(int idx_) : idx(idx_) {}

	operator bool() {
		return idx != 0;
	}

	void reset() {
		idx = 0;
	}

	int idx;
};

// A weak reference wrapper. Pushing it does not keep the referenced value
// alive for the garbage collector (see LuaContext::pushWeakReference). Used
// for GUI objects whose lifetime is managed by C++ ownership (parent/context)
// rather than by a strong Lua reference, so they can be collected and
// finalized instead of leaking.
struct LuaReferenceWeak {
	explicit LuaReferenceWeak(LuaReference ref_) : ref(ref_) {}

	LuaReference ref;
};

#define LUA_NEW_(t_, param_, lua_)                                                                                     \
	({                                                                                                                 \
		LuaContext &lua_once_ = lua_;                                                                                  \
		lua_once_.pushRegObject(t_::metaTable);                                                                        \
		void *space_ = lua_once_.pushObject(sizeof(t_));                                                               \
		t_ *p_ = new (space_) t_ param_;                                                                               \
		p_->luaReference = lua_once_.createReference();                                                                \
		p_;                                                                                                            \
	})

#define LUA_NEW_KEEP(t_, param_, lua_)                                                                                 \
	({                                                                                                                 \
		LuaContext &lua_once_ = lua_;                                                                                  \
		lua_once_.pushRegObject(t_::metaTable);                                                                        \
		void *space_ = lua_once_.pushObject(sizeof(t_));                                                               \
		t_ *p_ = new (space_) t_ param_;                                                                               \
		lua_pushvalue(lua_once_, -1);                                                                                  \
		p_->luaReference = lua_once_.createReference();                                                                \
		p_;                                                                                                            \
	})

#define lua_new(t_, param_, lua_)                                                                                      \
	({                                                                                                                 \
		LuaContext &lua_once_ = lua_;                                                                                  \
		lua_once_.push(t_::metaTable);                                                                                 \
		void *space_ = lua_once_.pushObject(sizeof(t_));                                                               \
		t_ *p_ = new (space_) t_ param_;                                                                               \
		p_->luaReference = lua_once_.createReference();                                                                \
		p_;                                                                                                            \
	})

#define lua_new_keep(t_, param_, lua_)                                                                                 \
	({                                                                                                                 \
		LuaContext &lua_once_ = lua_;                                                                                  \
		lua_once_.push(t_::metaTable);                                                                                 \
		void *space_ = lua_once_.pushObject(sizeof(t_));                                                               \
		t_ *p_ = new (space_) t_ param_;                                                                               \
		lua_pushvalue(lua_once_, -1);                                                                                  \
		p_->luaReference = lua_once_.createReference();                                                                \
		p_;                                                                                                            \
	})

// Like lua_new_keep but the registry reference is *weak*, so it does not pin
// the object alive. Required for GUI objects whose lifetime is otherwise
// managed by the garbage collector: without a weak reference the object would
// be permanently rooted (registry is a GC root) and never collected, leaking
// its C++ members' heap allocations.
#define lua_new_weak(t_, param_, lua_)                                                                                 \
	({                                                                                                                 \
		LuaContext &lua_once_ = lua_;                                                                                  \
		lua_once_.push(t_::metaTable);                                                                                 \
		void *space_ = lua_once_.pushObject(sizeof(t_));                                                               \
		t_ *p_ = new (space_) t_ param_;                                                                               \
		p_->luaReference = lua_once_.createWeakReference();                                                            \
		p_;                                                                                                            \
	})

#define lua_new_weak_keep(t_, param_, lua_)                                                                            \
	({                                                                                                                 \
		LuaContext &lua_once_ = lua_;                                                                                  \
		lua_once_.push(t_::metaTable);                                                                                 \
		void *space_ = lua_once_.pushObject(sizeof(t_));                                                               \
		t_ *p_ = new (space_) t_ param_;                                                                               \
		lua_pushvalue(lua_once_, -1);                                                                                  \
		p_->luaReference = lua_once_.createWeakReference();                                                            \
		p_;                                                                                                            \
	})

#define lua_new_m(t_, param_, lua_, metatable_)                                                                        \
	({                                                                                                                 \
		LuaContext &lua_once_ = lua_;                                                                                  \
		lua_once_.push(metatable_);                                                                                    \
		void *space_ = lua_once_.pushObject(sizeof(t_));                                                               \
		t_ *p_ = new (space_) t_ param_;                                                                               \
		p_->luaReference = lua_once_.createReference();                                                                \
		p_;                                                                                                            \
	})

#define lua_new_m_keep(t_, param_, lua_, metatable_)                                                                   \
	({                                                                                                                 \
		LuaContext &lua_once_ = lua_;                                                                                  \
		lua_once_.push(metatable_);                                                                                    \
		void *space_ = lua_once_.pushObject(sizeof(t_));                                                               \
		t_ *p_ = new (space_) t_ param_;                                                                               \
		lua_pushvalue(lua_once_, -1);                                                                                  \
		p_->luaReference = lua_once_.createReference();                                                                \
		p_;                                                                                                            \
	})

#endif // LUA_TYPES_H
