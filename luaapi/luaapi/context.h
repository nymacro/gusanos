#ifndef LUA_CONTEXT_H
#define LUA_CONTEXT_H

extern "C" {
#include "lua.h"
}

#include <cassert>
#include "types.h"
#include <iostream>
#include <vector>
#include <string>
#include <istream>
#include <ostream>
#include <utility>
#include <map>
using std::cerr;
using std::endl;
using std::istream;
using std::string;

class ZCom_BitStream;

class LuaContext {
  public:
	struct FunctionProxy {
		FunctionProxy(LuaContext &context) : m_context(context) {}

		FunctionProxy const &operator()(char const *name, lua_CFunction func) const {
			m_context.function(name, func);
			return *this;
		}

		LuaContext &m_context;
	};

	struct TableFunctionProxy {
		TableFunctionProxy(LuaContext &context) : m_context(context) {}

		TableFunctionProxy const &operator()(char const *name, lua_CFunction func) const {
			m_context.tableFunction(name, func);
			return *this;
		}

		LuaContext &m_context;
	};

	struct TableItemProxy {
		TableItemProxy(LuaContext &context) : m_context(context) {}

		template <class T>
		TableItemProxy const &operator()(char const *name, T const &v) const {
			lua_pushstring(m_context, name);
			m_context.push(v);
			lua_rawset(m_context, -3);
			return *this;
		}

		LuaContext &m_context;
	};

	struct CallProxy {
		CallProxy(LuaContext &context, LuaReference ref, int returns)
			: m_context(context), m_ref(ref), m_params(0), m_returns(returns) {
			m_context.push(errorReport);
			m_context.push(ref);
		}

		template <class T>
		CallProxy &operator,(T const &p) {
			++m_params;
			m_context.push(p);
			return *this;
		}

		CallProxy &operator,(LuaReferenceWeak const &p) {
			++m_params;
			m_context.pushWeakReference(p.ref);
			return *this;
		}

		int operator()() const {
			if (lua_isnil(m_context, -m_params - 1)) {
				m_context.pop(m_params + 2);
				return 0;
			}

			int r = m_context.call(m_params, m_returns, -m_params - 2);

			if (r < 0) {
				m_context.pop(1); // Pop error function
				// m_context.destroyReference(m_ref);
				lua_pushnil(m_context);
				m_context.assignReference(m_ref);
				return 0;
			}
			lua_remove(m_context, -m_returns - 1);
			return m_returns;
		}

		LuaContext &m_context;
		LuaReference m_ref;
		int m_params;
		int m_returns;
	};

	template <class T>
	struct FullReference {
		FullReference(T &x_, LuaReference metatable_) : x(x_), metatable(metatable_) {}

		T &x;
		LuaReference metatable;
	};

	static int errorReport(lua_State *L);

	bool logOnce(std::ostream &str);
	void log(std::ostream &str);

	LuaContext();

	LuaContext(LuaContext const &);

	LuaContext(lua_State *state_);

	void init();

	void reset();

	static const char *istreamChunkReader(lua_State *L, void *data, size_t *size);

	void load(std::string const &chunk, istream &stream);

	int evalExpression(std::string const &chunk, std::string const &data);
	int evalExpression(std::string const &chunk, istream &stream);
	/*
	void load(std::string const& chunk, istream& stream, string const& table);
	*/
	int call(int params = 0, int returns = 0, int errfunc = 0);

	LuaContext &push(lua_CFunction v) {
		lua_pushcfunction(m_State, v);
		return *this;
	}

	LuaContext &push(lua_Number v) {
		lua_pushnumber(m_State, v);
		return *this;
	}

	LuaContext &push(float v) {
		lua_pushnumber(m_State, static_cast<lua_Number>(v));
		return *this;
	}

	LuaContext &push(char const *v) {
		lua_pushstring(m_State, v);
		return *this;
	}

	LuaContext &push(std::string const &v) {
		push(v.c_str());
		return *this;
	}

	LuaContext &push(std::pair<char const *, char const *> v) {
		lua_pushlstring(m_State, v.first, v.second - v.first);
		return *this;
	}

	LuaContext &push(LuaReference v) {
		pushReference(v);
		return *this;
	}

	template <class T>
	LuaContext &push(FullReference<T> const &v) {
		T **i = (T **)lua_newuserdata(m_State, sizeof(T *));
		*i = &v.x;
		pushReference(v.metatable);

		lua_setmetatable(m_State, -2);
		return *this;
	}

	LuaContext &push(bool v) {
		lua_pushboolean(m_State, v);
		return *this;
	}

	LuaContext &push(int v) {
		lua_pushinteger(m_State, static_cast<lua_Integer>(v));
		return *this;
	}

	LuaContext &push(long v) {
		lua_pushinteger(m_State, static_cast<lua_Integer>(v));
		return *this;
	}

	LuaContext &push(unsigned int v) {
		lua_pushinteger(m_State, static_cast<lua_Integer>(v));
		return *this;
	}

	LuaContext &push(unsigned long v) {
		lua_pushinteger(m_State, static_cast<lua_Integer>(v));
		return *this;
	}

	template <class T>
	LuaContext &push(T *v) {
		v->pushLuaReference();
		return *this;
	}

	template <class T>
	LuaContext &push(std::vector<T> const &v) {
		lua_newtable(m_State);
		for (size_t n = 0; n < v.size(); ++n) {
			push(v[n]);
			lua_rawseti(m_State, -2, n + 1);
		}
		return *this;
	}

	template <class T>
	static FullReference<T> fullReference(T &x, LuaReference metatable) {
		return FullReference<T>(x, metatable);
	}

	template <class T>
	void pushFullReference(T &x, LuaReference metatable) {
		T **i = (T **)lua_newuserdata(m_State, sizeof(T *));
		*i = &x;
		pushReference(metatable);

		lua_setmetatable(m_State, -2);
	}

	template <class T>
	void pushLightReference(T *x, LuaReference metatable) {
		lua_pushlightuserdata(m_State, x);
		pushReference(metatable);
		lua_setmetatable(m_State, -2);
	}

	template <class T>
	void pushFullReference(T &x) {
		T **i = (T **)lua_newuserdata(m_State, sizeof(T *));
		*i = &x;
	}

	template <class T>
	void pushObject(T const &x) {
		T *i = (T *)lua_newuserdata(m_State, sizeof(T));
		*i = x;
	}

	/*
	template<class T>
	T* toObject(int idx)
	{
		if(!lua_setmetatable(m_State, idx))
			lua_error(m_State, "Invalid argument");
		push(T::metatable());

		T** p = lua_touserdata(m_State, idx);
		if(!p) lua_error(m_State, "Invalid argument");
		T* p2 = *p;
	}*/

	void *pushObject(size_t count) // Pops a metatable from the stack
	{
		void *p = lua_newuserdata(m_State, count); // <metatable> <object>

		lua_insert(m_State, -2); // <object> <metatable>
		lua_setmetatable(m_State, -2);

		return p;
	}

	void *pushObject(LuaReference metatable, size_t count) {
		void *p = lua_newuserdata(m_State, count);

		pushReference(metatable);
		lua_setmetatable(m_State, -2);

		return p;
	}

	LuaContext &pushvalue(int i) {
		lua_pushvalue(m_State, i);
		return *this;
	}

	LuaContext &rawgeti(int table, int key) {
		lua_rawgeti(m_State, table, key);
		return *this;
	}

	LuaContext &rawseti(int table, int key) {
		lua_rawseti(m_State, table, key);
		return *this;
	}

	char const *tostring(int i) {
		return lua_tostring(m_State, i);
	}

	LuaContext &newtable() {
		lua_newtable(m_State);
		return *this;
	}

	LuaContext &pop(int c = 1) {
		lua_settop(m_State, (-1) - c);
		return *this;
	}

	CallProxy call(LuaReference ref, int returns = 0) {
		return CallProxy(*this, ref, returns);
	}

	template <class T>
	inline T get(int idx) {
		return T();
	}

	/*
	template<class T>
	inline T* getObject(int idx)
	{
		void* p = lua_touserdata(m_State, idx);
		if(!p)
			return 0;
		lua_getmetatable(m_State, idx);
		push(T::metaTable);
		bool b = lua_rawequal(m_State, -1, -2);
		pop(2);
		if(b)
			return *static_cast<T**>(p);
		return 0;
	}
	*/

	template <class T>
	void tableToVector(std::vector<T> const &v) {
		v.clear();
		for (size_t n = 1;; ++n) {
			lua_rawgeti(m_State, -1, n + 1);
			if (lua_isnil(m_State, -1)) {
				lua_settop(m_State, -3); // Pop nil and table
				return;
			}
			// TODO: v.push_back(get<T>(-1));
			lua_settop(m_State, -2); // Pop value
		}
	}

	void function(char const *name, lua_CFunction func, int upvalues = 0);

	void tableFunction(char const *name, lua_CFunction func);

	void tableSetField(int id) {
		lua_pushboolean(m_State, true);
		lua_rawseti(m_State, -2, id);
	}

	FunctionProxy functions() {
		return FunctionProxy(*this);
	}

	TableFunctionProxy tableFunctions() {
		return TableFunctionProxy(*this);
	}

	TableItemProxy tableItems() {
		return TableItemProxy(*this);
	}

	LuaReference createReference();

	void assignReference(LuaReference ref);

	void destroyReference(LuaReference ref);

	void pushReference(LuaReference ref);

	// Creates a *weak* reference to the value on top of the stack: the
	// reference does not keep the value alive for the garbage collector.
	// Used for GUI objects so they can be collected (and finalized via __gc)
	// once Lua no longer holds them, instead of being pinned forever by a
	// strong registry reference to themselves.
	LuaReference createWeakReference();

	void destroyWeakReference(LuaReference ref);

	void pushWeakReference(LuaReference ref);

	void serialize(std::ostream &s, int i);
	void deserialize(std::istream &s);
	void serializeT(std::ostream &s, int i, int indent = 0);

	void serialize(ZCom_BitStream &s, int i);
	bool deserialize(ZCom_BitStream &s);

	void regObject(char const *name) {
		lua_setfield(m_State, LUA_REGISTRYINDEX, name);
	}

	void regObjectKeep(char const *name) {
		lua_pushvalue(m_State, -1);
		regObject(name);
	}

	void pushRegObject(char const *name) {
		lua_getfield(m_State, LUA_REGISTRYINDEX, name);
	}

	operator lua_State *() const {
		return m_State;
	}

	void close();

	~LuaContext();

  private:
	lua_State *m_State;
};

template <>
inline bool LuaContext::get<bool>(int idx) {
	return lua_toboolean(m_State, idx);
}

extern LuaContext lua;

// RAII owning wrapper around a LuaReference. Calls destroyReference() on the
// owning LuaContext when it goes out of scope, so a reference dropped without
// an explicit destroyReference cannot leak its registry slot or keep the
// referenced Lua value alive forever. Additive: the raw LuaReference (types.h)
// remains for registry internals and existing manual call sites; new code that
// owns a reference for the duration of a scope should prefer this wrapper.
class LuaScopedReference {
  public:
	LuaScopedReference() : m_ctx(nullptr) {}

	explicit LuaScopedReference(LuaContext &ctx, LuaReference ref) : m_ctx(&ctx), m_ref(ref) {}

	~LuaScopedReference() { reset(); }

	LuaScopedReference(LuaScopedReference &&o) noexcept : m_ctx(o.m_ctx), m_ref(o.m_ref) {
		o.m_ctx = nullptr;
		o.m_ref.reset();
	}

	LuaScopedReference &operator=(LuaScopedReference &&o) noexcept {
		if (this != &o) {
			reset();
			m_ctx = o.m_ctx;
			m_ref = o.m_ref;
			o.m_ctx = nullptr;
			o.m_ref.reset();
		}
		return *this;
	}

	LuaScopedReference(LuaScopedReference const &) = delete;
	LuaScopedReference &operator=(LuaScopedReference const &) = delete;

	LuaReference get() const { return m_ref; }

	LuaReference release() {
		LuaReference r = m_ref;
		m_ref.reset();
		m_ctx = nullptr;
		return r;
	}

	explicit operator bool() { return m_ctx && m_ref; }

	void reset() {
		if (m_ctx && m_ref)
			m_ctx->destroyReference(m_ref);
		m_ref.reset();
		m_ctx = nullptr;
	}

  private:
	LuaContext *m_ctx;
	LuaReference m_ref;
};

#ifndef NDEBUG

struct AssertStack {
	AssertStack(LuaContext const &c_) : c(c_) {
		stack = lua_gettop(c);
	}

	~AssertStack() {
		int newStack = lua_gettop(c);
		assert(stack == newStack);
	}

	LuaContext const &c;
	int stack;
};

#else

struct AssertStack {
	AssertStack(LuaContext const &c_) {}
};

#endif

#endif // LUA_CONTEXT_H
