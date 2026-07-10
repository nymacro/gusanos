// Tests for key bindings and auto-completion in the console.

#include <boost/test/unit_test.hpp>
#include "console.h"

#include <sstream>

using namespace std;

BOOST_AUTO_TEST_SUITE(console_bindings)

// --- Basic binding ---

BOOST_AUTO_TEST_CASE(bind_and_get_action)
{
	Console c;
	c.bind('w', "+forward");

	BOOST_CHECK_EQUAL(c.getActionForBinding('w'), "+forward");
}

BOOST_AUTO_TEST_CASE(bind_overwrite)
{
	Console c;
	c.bind('w', "+forward");
	c.bind('w', "+back");

	BOOST_CHECK_EQUAL(c.getActionForBinding('w'), "+back");
}

BOOST_AUTO_TEST_CASE(get_key_for_action)
{
	Console c;
	c.bind('s', "+back");

	char key = c.getKeyForBinding("+back");
	BOOST_CHECK_EQUAL(key, 's');
}

// --- Unbind all (via BindTable) ---
// Note: unBind/unBindAll are on BindTable, not exposed on Console.
// The binding tests above cover the public API.

// --- Multiple bindings for same action ---

BOOST_AUTO_TEST_CASE(same_action_multiple_keys)
{
	Console c;
	c.bind('w', "+forward");
	c.bind('u', "+forward");

	BOOST_CHECK_EQUAL(c.getActionForBinding('w'), "+forward");
	BOOST_CHECK_EQUAL(c.getActionForBinding('u'), "+forward");
}

BOOST_AUTO_TEST_SUITE_END()
