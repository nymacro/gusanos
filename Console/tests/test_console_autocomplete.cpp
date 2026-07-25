// Tests for auto-completion in the console.

#include <boost/test/unit_test.hpp>
#include "console.h"

using namespace std;

BOOST_AUTO_TEST_SUITE(console_autocomplete)

// --- Command auto-completion ---

BOOST_AUTO_TEST_CASE(complete_command_prefix) {
	Console c;
	c.registerCommands()("mycommand", [](list<string> const &) { return ""; });
	c.registerCommands()("othercmd", [](list<string> const &) { return ""; });

	string result = c.autoComplete("my");
	BOOST_CHECK_EQUAL(result, "mycommand ");
}

BOOST_AUTO_TEST_CASE(complete_command_no_match) {
	Console c;
	c.registerCommands()("mycommand", [](list<string> const &) { return ""; });

	// "xyz" doesn't match any command
	string result = c.autoComplete("xyz");
	BOOST_CHECK_EQUAL(result, "xyz"); // unchanged
}

// --- Variable auto-completion ---

BOOST_AUTO_TEST_CASE(complete_variable_prefix) {
	Console c;
	int val = 0;
	c.registerVariables()("my_int", &val, 0);

	string result = c.autoComplete("my_int");
	BOOST_CHECK_EQUAL(result, "my_int ");
}

// --- Mixed commands and variables ---

BOOST_AUTO_TEST_CASE(complete_mixed_names) {
	Console c;
	int val = 0;
	c.registerVariables()("alpha", &val, 0);
	c.registerCommands()("beta", [](list<string> const &) { return ""; });
	c.registerCommands()("gamma", [](list<string> const &) { return ""; });

	string result = c.autoComplete("ga");
	BOOST_CHECK_EQUAL(result, "gamma ");
}

// --- Argument auto-completion ---

BOOST_AUTO_TEST_CASE(complete_command_name_and_arg) {
	Console c;
	c.registerCommands()("mycommand", [](list<string> const &) { return ""; });

	// Typing "mycommand " should complete the command name
	string result = c.autoComplete("mycommand ");
	BOOST_CHECK_EQUAL(result, "mycommand ");
}

// --- listItems ---

BOOST_AUTO_TEST_CASE(list_items_no_match) {
	Console c;
	c.listItems("xyz");
	// No items match "xyz", so nothing should be logged
	BOOST_CHECK_EQUAL(c.getLog().size(), 0u);
}

BOOST_AUTO_TEST_CASE(list_items_single_match) {
	Console c;
	int val = 0;
	c.registerVariables()("myvar", &val, 0);

	c.listItems("myvar");
	// Single match - no output (only shows when multiple match)
	BOOST_CHECK_EQUAL(c.getLog().size(), 0u);
}

BOOST_AUTO_TEST_CASE(list_items_multiple_matches) {
	Console c;
	int a = 0, b = 0;
	c.registerVariables()("alpha", &a, 0);
	c.registerVariables()("alpha2", &b, 0);

	c.listItems("alpha");
	// Should show "]" followed by matching items
	BOOST_CHECK_EQUAL(c.getLog().size(), 3u); // "]alpha", "alpha2"
}

// --- completeCommand ---

BOOST_AUTO_TEST_CASE(complete_command_direct) {
	Console c;
	c.registerCommands()("mycommand", [](list<string> const &) { return ""; });

	string result = c.completeCommand("myc");
	BOOST_CHECK_EQUAL(result, "mycommand ");
}

// --- Empty input ---

BOOST_AUTO_TEST_CASE(complete_empty_input) {
	Console c;
	string result = c.autoComplete("");
	BOOST_CHECK_EQUAL(result, "");
}

BOOST_AUTO_TEST_SUITE_END()
