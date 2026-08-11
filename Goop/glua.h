#ifndef GUSANOS_GLUA_H
#define GUSANOS_GLUA_H

#include <string>
#include <vector>
#include "luaapi/context.h"
#include "luaapi/types.h"

// Callback iteration: use dispatchCallbacks(type, args...) (fire-and-forget)
// or dispatchCallbacksVeto(type, args...) (returns true if any callback
// returned a truthy value) defined below. For loops that need per-iteration
// control (e.g. a nested loop whose args vary each iteration), obtain the
// vector explicitly with LuaCallbacks::callbacksFor(type_).

struct LuaCallbacks {
	enum {
		atGameStart = 0,
		afterRender = 1,
		afterUpdate = 2,
		wormRender = 3,
		viewportRender = 4,
		wormDeath = 5,
		wormRemoved = 6,
		playerUpdate = 7,
		playerInit = 8,
		playerRemoved = 9,
		playerNetworkInit = 10,
		gameNetworkInit = 11,
		gameEnded = 12,
		localplayerEventAny = 13,
		localplayerInit = 14,
		localplayerEvent = 15,
		// One Lua callback slot per Player::Actions value (LEFT..NINJAROPE,
		// 10 total). Must cover every action dispatched as
		// `localplayerEvent + action` in player_input.cpp; the old count of
		// 7 (BasePlayer::ACTION_COUNT, where UP/DOWN/CHANGE are commented
		// out) left WEAPON_NEXT/WEAPON_PREV/NINJAROPE aliasing the transfer
		// and network buckets below. Kept in sync with Player::ACTION_COUNT
		// via the static_assert in glua.cpp.
		localplayerEventCount = 10,
		transferUpdate = localplayerEvent + localplayerEventCount,
		transferFinished = transferUpdate + 1,
		networkStateChange = transferFinished + 1,
		gameError = networkStateChange + 1,
		max
	};
	void bind(std::string callback, LuaReference ref);
	/*
	std::vector<LuaReference> atGameStart;
	std::vector<LuaReference> afterRender;
	std::vector<LuaReference> afterUpdate;
	std::vector<LuaReference> wormRender;
	std::vector<LuaReference> viewportRender;
	std::vector<LuaReference> wormDeath;
	std::vector<LuaReference> wormRemoved;
	std::vector<LuaReference> playerUpdate;
	std::vector<LuaReference> playerInit;
	std::vector<LuaReference> playerRemoved;
	std::vector<LuaReference> playerNetworkInit;
	std::vector<LuaReference> gameNetworkInit;
	std::vector<LuaReference> gameEnded;
	//TODO: std::vector<LuaReference> connectionRequest;

	std::vector<LuaReference> localplayerEvent[7];
	std::vector<LuaReference> localplayerEventAny;
	std::vector<LuaReference> localplayerInit;
	*/
	std::vector<LuaReference> callbacks[max];

	// Accessor for the callback vector of a given type. Use this directly for
	// loops that need per-iteration control (e.g. wormRender's nested per-worm
	// loop in viewport.cpp); prefer the dispatchCallbacks / dispatchCallbacksVeto
	// helpers below for the common fire-and-forget and veto patterns.
	std::vector<LuaReference> &callbacksFor(int type_) {
		return callbacks[type_];
	}
};

extern LuaCallbacks luaCallbacks;

// dispatchCallbacks(type, args...): invoke every registered callback of the
// given type in registration order, passing args... to each. This centralizes
// the per-callback
//   (lua.call(*i), args...)()
// call idiom (formerly spelled as an EACH_CALLBACK for-loop; that macro was
// removed once all call sites routed through these helpers / callbacksFor).
// The iteration order (begin -> end) is preserved exactly, so callbacks
// that fire inside a deterministic GameplayRngScope (notably wormDeath) keep
// drawing from the scope in the same sequence on every peer -- reordering
// would desync clients. The fixed-arity templates keep the exact
// (lua.call(*i), args...)() expression so the same LuaContext::push overload
// is selected for each argument as in the original code.
inline void dispatchCallbacks(int type) {
	std::vector<LuaReference> &cbs = luaCallbacks.callbacksFor(type);
	for (std::vector<LuaReference>::iterator i = cbs.begin(); i != cbs.end(); ++i)
		(lua.call(*i))();
}
template <class A1>
inline void dispatchCallbacks(int type, A1 const &a1) {
	std::vector<LuaReference> &cbs = luaCallbacks.callbacksFor(type);
	for (std::vector<LuaReference>::iterator i = cbs.begin(); i != cbs.end(); ++i)
		(lua.call(*i), a1)();
}
template <class A1, class A2>
inline void dispatchCallbacks(int type, A1 const &a1, A2 const &a2) {
	std::vector<LuaReference> &cbs = luaCallbacks.callbacksFor(type);
	for (std::vector<LuaReference>::iterator i = cbs.begin(); i != cbs.end(); ++i)
		(lua.call(*i), a1, a2)();
}
template <class A1, class A2, class A3>
inline void dispatchCallbacks(int type, A1 const &a1, A2 const &a2, A3 const &a3) {
	std::vector<LuaReference> &cbs = luaCallbacks.callbacksFor(type);
	for (std::vector<LuaReference>::iterator i = cbs.begin(); i != cbs.end(); ++i)
		(lua.call(*i), a1, a2, a3)();
}
template <class A1, class A2, class A3, class A4>
inline void dispatchCallbacks(int type, A1 const &a1, A2 const &a2, A3 const &a3, A4 const &a4) {
	std::vector<LuaReference> &cbs = luaCallbacks.callbacksFor(type);
	for (std::vector<LuaReference>::iterator i = cbs.begin(); i != cbs.end(); ++i)
		(lua.call(*i), a1, a2, a3, a4)();
}

// dispatchCallbacksVeto(type, args...): invoke every registered callback of
// the given type the way localplayerEvent / localplayerEventAny are dispatched
// in player_input.cpp -- each callback is called requesting one return value,
// and if it returns a truthy boolean the action is vetoed (returns true). Like
// dispatchCallbacks, iteration order is begin->end exactly and the
// (lua.call(*i, 1), args...)() expression is preserved verbatim, so the same
// LuaContext::push overload resolves for each argument as in the original
// code. Every callback is invoked unconditionally -- callbacks may have side
// effects -- and the return value only ORs into the veto flag (no short-circuit,
// per callback or across the two buckets the caller dispatches).
template <class A1, class A2>
inline bool dispatchCallbacksVeto(int type, A1 const &a1, A2 const &a2) {
	bool veto = false;
	std::vector<LuaReference> &cbs = luaCallbacks.callbacksFor(type);
	for (std::vector<LuaReference>::iterator i = cbs.begin(); i != cbs.end(); ++i) {
		int n = (lua.call(*i, 1), a1, a2)();
		if (n > 0 && lua.get<bool>(-1))
			veto = true;
		lua.pop(n);
	}
	return veto;
}
template <class A1, class A2, class A3>
inline bool dispatchCallbacksVeto(int type, A1 const &a1, A2 const &a2, A3 const &a3) {
	bool veto = false;
	std::vector<LuaReference> &cbs = luaCallbacks.callbacksFor(type);
	for (std::vector<LuaReference>::iterator i = cbs.begin(); i != cbs.end(); ++i) {
		int n = (lua.call(*i, 1), a1, a2, a3)();
		if (n > 0 && lua.get<bool>(-1))
			veto = true;
		lua.pop(n);
	}
	return veto;
}

/*
// This is GCC specific, because I can't find a way to do it in standard C++ :/
#define LUA_NEW(t_, param_) \
({ \
	void* space = lua.pushObject(t_::metaTable(), sizeof(t_)); \
	t_* p = new (space) t_ param_; \
	p->luaReference = lua.createReference(); \
	p; \
})


#define LUA_DELETE(t_, p_) { \
	t_* p = (p_); \
	p->~t_(); \
	lua.destroyReference(p->luaReference); \
}*/

struct LuaObject {
	LuaObject() : deleted(false) {}

	void pushLuaReference();

	LuaReference getLuaReference();

	virtual void makeReference();

	virtual void finalize() {}

	void deleteThis();

	virtual ~LuaObject() {}

	LuaReference luaReference;
	bool deleted;
};

template <class T>
inline void luaDelete(T *p) {
	if (p)
		p->deleteThis();
}

#endif // GUSANOS_GLUA_H
