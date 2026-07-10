// Tests for basic console functionality: variable registration,
// parsing, log management, and error handling.

#include <boost/test/unit_test.hpp>
#include "console.h"
#include "variables.h"

#include <sstream>

using namespace std;

BOOST_AUTO_TEST_SUITE(console_basic)

// --- Variable registration and retrieval ---

BOOST_AUTO_TEST_CASE(int_variable_default)
{
	Console c;
	int val = 0;
	c.registerVariables()("my_int", &val, 42);

	string result = c.invoke("my_int", list<string>(), false);
	BOOST_CHECK_EQUAL(result, "42");
}

BOOST_AUTO_TEST_CASE(int_variable_set)
{
	Console c;
	int val = 0;
	c.registerVariables()("my_int", &val, 42);

	c.invoke("my_int", {"100"}, false);
	BOOST_CHECK_EQUAL(val, 100);
}

BOOST_AUTO_TEST_CASE(int_variable_set_via_parseLine)
{
	Console c;
	int val = 0;
	c.registerVariables()("my_int", &val, 42);

	c.parseLine("my_int 77");
	BOOST_CHECK_EQUAL(val, 77);
}

BOOST_AUTO_TEST_CASE(float_variable_default)
{
	Console c;
	float val = 0.0f;
	c.registerVariables()("my_float", &val, 3.14f);

	string result = c.invoke("my_float", list<string>(), false);
	BOOST_CHECK_CLOSE(stof(result), 3.14f, 0.01f);
}

BOOST_AUTO_TEST_CASE(float_variable_set)
{
	Console c;
	float val = 0.0f;
	c.registerVariables()("my_float", &val, 1.0f);

	c.invoke("my_float", {"2.5"}, false);
	BOOST_CHECK_CLOSE(val, 2.5f, 0.01f);
}

BOOST_AUTO_TEST_CASE(string_variable_default)
{
	Console c;
	string val;
	c.registerVariables()("my_str", &val, string("hello"));

	string result = c.invoke("my_str", list<string>(), false);
	BOOST_CHECK_EQUAL(result, "hello");
}

BOOST_AUTO_TEST_CASE(string_variable_set)
{
	Console c;
	string val;
	c.registerVariables()("my_str", &val, string(""));

	c.invoke("my_str", {"world"}, false);
	BOOST_CHECK_EQUAL(val, "world");
}

// --- Quoted string arguments ---

BOOST_AUTO_TEST_CASE(string_with_spaces_via_quotes)
{
	Console c;
	string val;
	c.registerVariables()("my_str", &val, string(""));

	c.parseLine("my_str \"hello world\"");
	BOOST_CHECK_EQUAL(val, "hello world");
}

// --- Callback invocation ---

BOOST_AUTO_TEST_CASE(int_variable_callback)
{
	Console c;
	int val = 0;
	bool callbackFired = false;

	c.registerVariables()("my_int", &val, 0,
		[&](int const&) { callbackFired = true; });

	BOOST_CHECK_EQUAL(callbackFired, false);

	c.invoke("my_int", {"42"}, false);
	BOOST_CHECK_EQUAL(callbackFired, true);
	BOOST_CHECK_EQUAL(val, 42);
}

BOOST_AUTO_TEST_CASE(float_variable_callback)
{
	Console c;
	float val = 0.0f;
	int callbackCount = 0;

	c.registerVariables()("my_float", &val, 0.0f,
		[&](float const&) { callbackCount++; });

	c.invoke("my_float", {"1.5"}, false);
	c.invoke("my_float", {"2.5"}, false);
	BOOST_CHECK_EQUAL(callbackCount, 2);
}

// --- Log management ---

BOOST_AUTO_TEST_CASE(log_messages)
{
	Console c;
	c.addLogMsg("first");
	c.addLogMsg("second");
	c.addLogMsg("third");

	BOOST_CHECK_EQUAL(c.getLog().size(), 3u);
	BOOST_CHECK_EQUAL(c.getLog().front(), "first");
	BOOST_CHECK_EQUAL(c.getLog().back(), "third");
}

BOOST_AUTO_TEST_CASE(log_empty_message_ignored)
{
	Console c;
	c.addLogMsg("first");
	c.addLogMsg("");
	c.addLogMsg("second");

	BOOST_CHECK_EQUAL(c.getLog().size(), 2u);
}

BOOST_AUTO_TEST_CASE(log_max_size)
{
	Console c(3); // max 3 messages
	c.addLogMsg("one");
	c.addLogMsg("two");
	c.addLogMsg("three");
	c.addLogMsg("four"); // should push out "one"

	BOOST_CHECK_EQUAL(c.getLog().size(), 3u);
	BOOST_CHECK_EQUAL(c.getLog().front(), "two");
	BOOST_CHECK_EQUAL(c.getLog().back(), "four");
}

// --- Error handling ---

BOOST_AUTO_TEST_CASE(unknown_command)
{
	Console c;
	string result = c.invoke("nonexistent", list<string>(), false);
	BOOST_CHECK(result.find("UNKNOWN COMMAND") != string::npos);
}

BOOST_AUTO_TEST_CASE(syntax_error_in_parseLine)
{
	Console c;
	c.parseLine("my_int {"); // malformed syntax

	// Should have error in log (original text + caret + error message)
	BOOST_CHECK(c.getLog().size() >= 1u);
	BOOST_CHECK_EQUAL(c.getLog().front(), "my_int {");
}

// --- Multiple commands with semicolons ---

BOOST_AUTO_TEST_CASE(multiple_commands_semicolon)
{
	Console c;
	int a = 0, b = 0;
	c.registerVariables()("var_a", &a, 0);
	c.registerVariables()("var_b", &b, 0);

	c.parseLine("var_a 10; var_b 20");
	BOOST_CHECK_EQUAL(a, 10);
	BOOST_CHECK_EQUAL(b, 20);
}

BOOST_AUTO_TEST_CASE(multiple_commands_semicolon_one_error)
{
	Console c;
	int a = 0;
	c.registerVariables()("var_a", &a, 0);

	// Second command is unknown, but first should still execute
	c.parseLine("var_a 5; unknown_cmd");

	BOOST_CHECK_EQUAL(a, 5);
	// Log should have error for the unknown command
	BOOST_CHECK(c.getLog().size() >= 1u);
}

// --- Empty and whitespace lines ---

BOOST_AUTO_TEST_CASE(empty_line)
{
	Console c;
	c.parseLine("");
	BOOST_CHECK_EQUAL(c.getLog().size(), 0u);
}

// --- Strip whitespace from line ends ---

static string trim(string const& s)
{
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == string::npos) return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

BOOST_AUTO_TEST_CASE(whitespace_line)
{
	Console c;
	c.parseLine("   ");
	BOOST_CHECK_EQUAL(c.getLog().size(), 0u);
}

// --- Case insensitivity ---

BOOST_AUTO_TEST_CASE(case_insensitive_command)
{
	Console c;
	int val = 0;
	c.registerVariables()("MyVar", &val, 0);

	c.parseLine("myvar 42");
	BOOST_CHECK_EQUAL(val, 42);
}

BOOST_AUTO_TEST_CASE(case_insensitive_command_upper)
{
	Console c;
	int val = 0;
	c.registerVariables()("myvar", &val, 0);

	c.parseLine("MYVAR 99");
	BOOST_CHECK_EQUAL(val, 99);
}

// --- Direct invoke with empty args returns value ---

BOOST_AUTO_TEST_CASE(invoke_empty_args_returns_value)
{
	Console c;
	int val = 0;
	c.registerVariables()("my_int", &val, 0);

	// Set the value first
	c.invoke("my_int", {"42"}, false);
	BOOST_CHECK_EQUAL(val, 42);

	// Now invoke with no args to get the value
	string result = c.invoke("my_int", list<string>(), false);
	BOOST_CHECK_EQUAL(result, "42");
}

// --- Release mode parsing ---

BOOST_AUTO_TEST_CASE(release_mode_parse)
{
	Console c;
	int val = 0;
	c.registerVariables()("my_int", &val, 0);

	// In release mode (parseRelease=true), '+' prefixed commands are
	// looked up with '-' prefix. Regular commands should still work.
	c.parseLine("my_int 10", false);
	BOOST_CHECK_EQUAL(val, 10);
}

BOOST_AUTO_TEST_SUITE_END()
