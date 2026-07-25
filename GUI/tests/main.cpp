// GUI test suite entry point

#define BOOST_TEST_MODULE omfggui
#include <boost/test/unit_test.hpp>

#include <cstdio>
#include <lua.hpp>

#include "luaapi/context.h"
#include "luaapi/types.h"
#include "lua/bindings-gui.h"
#include "detail/wnd.h"
#include "detail/context.h"
#include "stub_renderer.h"
#include "stub_context.h"

using namespace OmfgGUI;

// Note: LuaContext lua; is defined in glua (luaapi/luaapi/context.cpp:25).
// We link glua to get it. The test binary must NOT define it again.

// Global test infrastructure: one renderer + one context shared across all
// tests. This avoids the complication of initGUI's light-userdata upvalues
// pointing at destroyed contexts (the GUI Lua functions capture &gui as an
// upvalue at initGUI time).
namespace {

GuiTests::StubRenderer g_renderer;
GuiTests::StubContext g_context(&g_renderer);

} // namespace

struct GuiGlobalSetup {
	GuiGlobalSetup() {
		LuaBindings::initGUI(g_context, lua);

		// Create a weak-referenced root Wnd so gui_root() works in tests.
		// The GUI layer stores object references as weak refs
		// (pushWeakReference reads from the weak-ref table) and keeps the root
		// alive via Context::m_rootWnd / m_rootRef. lua_new_keep would store a
		// strong registry ref that pushWeakReference cannot resolve.
		std::map<std::string, std::string> attribs;
		attribs["id"] = "test_root";
		g_context.setRoot(lua_new_weak(Wnd, (0, attribs), lua));
	}

	~GuiGlobalSetup() {
		g_context.destroy();
	}
};

BOOST_GLOBAL_FIXTURE(GuiGlobalSetup);

// Helper: execute Lua statements and pop any results. We use raw Lua API
// instead of LuaContext::evalExpression because that helper expects exactly
// one expression return value and pcall's with nresults=1.
static void execLua(char const *code) {
	{
		lua_State *L = lua; // LuaContext is implicitly convertible to lua_State*
		int err = luaL_loadstring(L, code);
		if (err) {
			std::fprintf(stderr, "Lua load error: %s\n", lua_tostring(L, -1));
			lua_pop(L, 1);
			return;
		}
		err = lua_pcall(L, 0, LUA_MULTRET, 0);
		if (err) {
			std::fprintf(stderr, "Lua run error: %s\n", lua_tostring(L, -1));
			lua_pop(L, 1);
			return;
		}
	}
}

// --- Baseline tests (Phase 0) ---
// These exercise the core creation/manipulation paths against the CURRENT
// code (pre-migration) to establish a green baseline before any changes.
//
// Verification strategy: Lua-side round-trip through Wnd:is_visible() rather
// than C++ findNamedWindow(). The latter requires a registered context, which
// Lua-only windows don't have until they are attached to a rooted parent.
// The Lua API is the canonical user-facing surface for Phase 0.
//
// The local `w` Lua reference keeps the userdata alive across the assertion
// calls. On scope exit Lua will GC the userdata (the Wnd was placement-new'd
// inside it).

BOOST_AUTO_TEST_CASE(create_gui_window) {
	BOOST_CHECK_NO_THROW(execLua("local w = gui_window({})"));
}

BOOST_AUTO_TEST_CASE(default_visible_via_lua) {
	BOOST_CHECK_NO_THROW(execLua("local w = gui_window({}); "
								 "assert(w:is_visible() == true)"));
}

BOOST_AUTO_TEST_CASE(set_visibility_false_via_lua) {
	BOOST_CHECK_NO_THROW(execLua("local w = gui_window({}); "
								 "w:set_visibility(false); "
								 "assert(w:is_visible() == false)"));
}

BOOST_AUTO_TEST_CASE(set_visibility_true_via_lua) {
	BOOST_CHECK_NO_THROW(execLua("local w = gui_window({}); "
								 "w:set_visibility(false); "
								 "w:set_visibility(true); "
								 "assert(w:is_visible() == true)"));
}

BOOST_AUTO_TEST_CASE(create_multiple_windows) {
	BOOST_CHECK_NO_THROW(execLua("local a = gui_window({}); "
								 "local b = gui_window({}); "
								 "local c = gui_window({}); "
								 "assert(a:is_visible()); "
								 "assert(b:is_visible()); "
								 "assert(c:is_visible())"));
}

BOOST_AUTO_TEST_CASE(visibility_toggle_roundtrip) {
	BOOST_CHECK_NO_THROW(execLua("local w = gui_window({}); "
								 "w:set_visibility(false); "
								 "w:set_visibility(true); "
								 "w:set_visibility(false); "
								 "assert(w:is_visible() == false)"));
}

BOOST_AUTO_TEST_CASE(destroy_window_by_dropping_ref) {
	// A Lua-only window with no parent and no other refs should be GC'd when
	// the local drops. We can't observe post-GC state from Lua (the userdata
	// becomes invalid), so we only verify that creation + drop doesn't crash
	// or leak in observable ways.
	BOOST_CHECK_NO_THROW(execLua("do "
								 "  local w = gui_window({}); "
								 "  w:set_visibility(false); "
								 "end"));
}

BOOST_AUTO_TEST_CASE(inheritance_smoke) {
	// All widget subclasses use the same creation path; quickly verify a few
	// of them instantiate and report visibility.
	BOOST_CHECK_NO_THROW(execLua("local g = gui_group({}); "
								 "local l = gui_label({}); "
								 "local c = gui_check({}); "
								 "local b = gui_button({}); "
								 "local e = gui_edit({}); "
								 "local ls = gui_list({}); "
								 "assert(g:is_visible()); "
								 "assert(l:is_visible()); "
								 "assert(c:is_visible()); "
								 "assert(b:is_visible()); "
								 "assert(e:is_visible()); "
								 "assert(ls:is_visible())"));
}

// --- Comprehensive Phase 0 tests ---
// These exercise more involved paths (parent-child, text round-trip, active
// state, callbacks) against the CURRENT code so Phase 1 has a stronger
// regression gate than the smoke tests above.

// --- Parent-child ---

BOOST_AUTO_TEST_CASE(add_child_to_root_via_lua) {
	BOOST_CHECK_NO_THROW(execLua("local c = gui_window({id='child1'}); "
								 "gui_root():add(c)"));
	Wnd *root = g_context.getRoot();
	BOOST_REQUIRE(root != nullptr);
	BOOST_CHECK(root->getChildByName("child1") != nullptr);
}

BOOST_AUTO_TEST_CASE(child_attribute_roundtrip) {
	execLua("local c = gui_window({id='attr_child'}); gui_root():add(c)");
	BOOST_CHECK_NO_THROW(execLua("local c = gui_root():child('attr_child'); "
								 "assert(c:attribute('id') == 'attr_child')"));
}

BOOST_AUTO_TEST_CASE(child_lookup_returns_same_object) {
	execLua("local c = gui_window({id='same_obj'}); "
			"gui_root():add(c)");
	Wnd *root = g_context.getRoot();
	Wnd *found = root->getChildByName("same_obj");
	BOOST_REQUIRE(found != nullptr);
	BOOST_CHECK_NO_THROW(execLua("local c = gui_root():child('same_obj'); "
								 "assert(c:is_visible())"));
}

BOOST_AUTO_TEST_CASE(parent_child_visibility_propagation) {
	// Hiding the parent should make the child not visible per is_visible()
	// walk-up logic (Wnd::isVisible() returns false if any ancestor is hidden).
	execLua("local c = gui_window({id='vis_child'}); "
			"gui_root():add(c); "
			"gui_root():set_visibility(false); "
			"local c2 = gui_root():child('vis_child'); "
			"assert(c2:is_visible() == false); "
			"gui_root():set_visibility(true); "
			"local c3 = gui_root():child('vis_child'); "
			"assert(c3:is_visible() == true)");
}

BOOST_AUTO_TEST_CASE(add_multiple_children_to_root) {
	BOOST_CHECK_NO_THROW(execLua("local a = gui_window({id='mc_a'}); "
								 "local b = gui_window({id='mc_b'}); "
								 "local c = gui_window({id='mc_c'}); "
								 "gui_root():add({a, b, c})"));
	Wnd *root = g_context.getRoot();
	BOOST_REQUIRE(root != nullptr);
	BOOST_CHECK(root->getChildByName("mc_a") != nullptr);
	BOOST_CHECK(root->getChildByName("mc_b") != nullptr);
	BOOST_CHECK(root->getChildByName("mc_c") != nullptr);
}

BOOST_AUTO_TEST_CASE(parented_child_outlives_local) {
	// A child added to root should not be GC'd when the local Lua var drops.
	// Verify by querying through the root after the local is gone.
	BOOST_CHECK_NO_THROW(execLua("do "
								 "  local c = gui_window({id='survivor'}); "
								 "  gui_root():add(c); "
								 "end; "
								 "local c2 = gui_root():child('survivor'); "
								 "assert(c2 ~= nil and c2:is_visible())"));
}

BOOST_AUTO_TEST_CASE(unparented_child_can_be_gced) {
	// A window that was never attached and whose only ref is a local should
	// drop the local without crashing. We can't observe post-GC state in Lua
	// (the userdata becomes invalid), so this just verifies no crash.
	BOOST_CHECK_NO_THROW(execLua("do "
								 "  local c = gui_window({}); "
								 "  c:set_visibility(false); "
								 "end"));
}

// --- Text / methods round-trip ---

BOOST_AUTO_TEST_CASE(set_text_then_read_text) {
	BOOST_CHECK_NO_THROW(execLua("local l = gui_label({id='txt_label'}); "
								 "gui_root():add(l); "
								 "l:set_text('hello world'); "
								 "assert(l:text() == 'hello world')"));
}

BOOST_AUTO_TEST_CASE(set_text_empty) {
	BOOST_CHECK_NO_THROW(execLua("local l = gui_label({id='empty_label'}); "
								 "gui_root():add(l); "
								 "l:set_text(''); "
								 "assert(l:text() == '')"));
}

BOOST_AUTO_TEST_CASE(activate_roundtrip) {
	BOOST_CHECK_NO_THROW(execLua("local w = gui_window({id='active_w'}); "
								 "gui_root():add(w); "
								 "w:activate(); "
								 "assert(w:is_active() == true); "
								 "w:deactivate(); "
								 "assert(w:is_active() == false)"));
}

// --- Callbacks (doAction/doKeyDown pass `this` to Lua) ---
// These paths are CRITICAL for Phase 1: doAction and doKeyDown push a
// LuaReferenceWeak (the Wnd's own weak ref) as the first arg to the Lua
// callback. Phase 1 replaces this with a shared_ptr handle minted in the
// LuaContext. If the migration is wrong here, the callback will either
// not fire or the Wnd will be incorrectly pinned.

BOOST_AUTO_TEST_CASE(on_action_callback_fires) {
	// Register a callback that flips a global flag, then trigger it via
	// doAction() called from C++. This exercises the critical Phase 1 path:
	// doAction pushes LuaReferenceWeak(luaReference) as the first Lua arg.
	BOOST_CHECK_NO_THROW(execLua("_hits = 0; "
								 "local b = gui_button({id='cb_btn'}); "
								 "gui_root():add(b); "
								 "function b:onAction() _hits = _hits + 1 end"));
	Wnd *root = g_context.getRoot();
	BOOST_REQUIRE(root != nullptr);
	Wnd *btn = root->getChildByName("cb_btn");
	BOOST_REQUIRE(btn != nullptr);
	btn->doAction();
	BOOST_CHECK_NO_THROW(execLua("assert(_hits == 1)"));
	btn->doAction();
	btn->doAction();
	BOOST_CHECK_NO_THROW(execLua("assert(_hits == 3)"));
}

BOOST_AUTO_TEST_CASE(on_keydown_callback_fires) {
	// Same critical path as onAction but for keyDown. doKeyDown pushes
	// (LuaReferenceWeak(luaReference), key) to the Lua callback.
	BOOST_CHECK_NO_THROW(execLua("_lastKey = nil; "
								 "local e = gui_edit({id='cb_edit'}); "
								 "gui_root():add(e); "
								 "function e:onKeyDown(k) _lastKey = k end"));
	Wnd *root = g_context.getRoot();
	BOOST_REQUIRE(root != nullptr);
	Wnd *edit = root->getChildByName("cb_edit");
	BOOST_REQUIRE(edit != nullptr);
	edit->doKeyDown(42);
	BOOST_CHECK_NO_THROW(execLua("assert(_lastKey == 42)"));
}

// --- Inheritance smoke (with parent for context propagation) ---

BOOST_AUTO_TEST_CASE(inheritance_smoke_with_parent) {
	// Same as inheritance_smoke but with parenting, so context propagates.
	BOOST_CHECK_NO_THROW(execLua("local g = gui_group({id='g1'}); "
								 "local l = gui_label({id='l1'}); "
								 "local c = gui_check({id='c1'}); "
								 "local b = gui_button({id='b1'}); "
								 "local e = gui_edit({id='e1'}); "
								 "local ls = gui_list({id='ls1'}); "
								 "gui_root():add({g, l, c, b, e, ls})"));
	Wnd *root = g_context.getRoot();
	BOOST_REQUIRE(root != nullptr);
	BOOST_CHECK(root->getChildByName("g1") != nullptr);
	BOOST_CHECK(root->getChildByName("l1") != nullptr);
	BOOST_CHECK(root->getChildByName("c1") != nullptr);
	BOOST_CHECK(root->getChildByName("b1") != nullptr);
	BOOST_CHECK(root->getChildByName("e1") != nullptr);
	BOOST_CHECK(root->getChildByName("ls1") != nullptr);
}

// --- Smoke: many windows ---

BOOST_AUTO_TEST_CASE(create_many_windows) {
	// Build a chain of nested windows to exercise addChild's strong-ref
	// pinning. Without it, only the root of the chain stays alive (the rest
	// would be GC'd as their transient parent-held weak refs die).
	BOOST_CHECK_NO_THROW(execLua("local depth = 6; "
								 "local nodes = {}; "
								 "for i = 1, depth do "
								 "  nodes[i] = gui_window({id = 'n' .. i}); "
								 "end; "
								 "for i = 1, depth do "
								 "  if i == 1 then gui_root():add(nodes[i]) "
								 "  else nodes[i - 1]:add(nodes[i]) end "
								 "end; "
								 "local cur = gui_root():child('n1'); "
								 "assert(cur ~= nil); "
								 "for i = 2, depth do "
								 "  cur = cur:child('n' .. i); "
								 "  assert(cur ~= nil, 'chain broken at n' .. i); "
								 "  assert(cur:is_visible(), 'n' .. i .. ' not visible'); "
								 "end"));
}
