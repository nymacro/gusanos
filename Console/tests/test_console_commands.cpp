// Tests for command registration, invocation, aliases, and special commands.

#include <boost/test/unit_test.hpp>
#include "console.h"
#include "command.h"
#include "alias.h"
#include "special_command.h"

#include <sstream>

using namespace std;

BOOST_AUTO_TEST_SUITE(console_commands)

// --- Simple command ---

BOOST_AUTO_TEST_CASE(simple_command_invocation)
{
	Console c;
	int callCount = 0;

	c.registerCommands()("mycmd", [&](list<string> const&) {
		callCount++;
		return "";
	});

	c.invoke("mycmd", list<string>(), false);
	BOOST_CHECK_EQUAL(callCount, 1);
}

BOOST_AUTO_TEST_CASE(simple_command_with_args)
{
	Console c;
	string receivedArg;

	c.registerCommands()("mycmd", [&](list<string> const& args) {
		BOOST_CHECK_EQUAL(args.size(), 2u);
		receivedArg = args.front();
		return "";
	});

	c.invoke("mycmd", {"hello", "world"}, false);
	BOOST_CHECK_EQUAL(receivedArg, "hello");
}

BOOST_AUTO_TEST_CASE(command_return_value_logged)
{
	Console c;

	c.registerCommands()("mycmd", [&](list<string> const&) {
		return "command output";
	});

	c.parseLine("mycmd");
	BOOST_CHECK_EQUAL(c.getLog().size(), 1u);
	BOOST_CHECK_EQUAL(c.getLog().front(), "<command output>");
}

// --- Command via parseLine ---

BOOST_AUTO_TEST_CASE(command_via_parseLine)
{
	Console c;
	int callCount = 0;

	c.registerCommands()("echo", [&](list<string> const& args) {
		callCount++;
		return args.empty() ? "empty" : args.front();
	});

	c.parseLine("echo hello");
	BOOST_CHECK_EQUAL(callCount, 1);
	BOOST_CHECK_EQUAL(c.getLog().front(), "<hello>");
}

// --- Command with completion ---

BOOST_AUTO_TEST_CASE(command_with_completion)
{
	Console c;

	c.registerCommands()("mycmd",
		[](list<string> const&) { return ""; },
		[](Console*, int, string const& begin) {
			return begin + "_completed";
		});

	// The completion callback receives the full input prefix and appends _completed
	string result = c.autoComplete("mycmd ar");
	BOOST_CHECK_EQUAL(result, "mycmd ar_completed");
}

// --- Alias registration and execution ---

BOOST_AUTO_TEST_CASE(alias_basic)
{
	Console c;
	int val = 0;
	c.registerVariables()("my_int", &val, 0);

	c.registerAlias("setval", "my_int 42");
	c.parseLine("setval");
	BOOST_CHECK_EQUAL(val, 42);
}

BOOST_AUTO_TEST_CASE(alias_with_args)
{
	Console c;
	int val = 0;
	c.registerVariables()("my_int", &val, 0);

	// Alias expands to "my_int" (no args forwarded).
	// The argument "99" is not processed separately.
	c.registerAlias("setv", "my_int");
	c.parseLine("setv 99");
	// my_int is invoked with no args, returns its current value "0"
	BOOST_CHECK_EQUAL(val, 0);
	BOOST_CHECK_EQUAL(c.getLog().size(), 1u);
	BOOST_CHECK_EQUAL(c.getLog().front(), "<0>");
}

BOOST_AUTO_TEST_CASE(alias_multiple_commands)
{
	Console c;
	int a = 0, b = 0;
	c.registerVariables()("var_a", &a, 0);
	c.registerVariables()("var_b", &b, 0);

	c.registerAlias("setboth", "var_a 10; var_b 20");
	c.parseLine("setboth");
	BOOST_CHECK_EQUAL(a, 10);
	BOOST_CHECK_EQUAL(b, 20);
}

BOOST_AUTO_TEST_CASE(alias_overwrite)
{
	Console c;

	c.registerAlias("myalias", "cmd1");
	c.registerAlias("myalias", "cmd2");

	// Second registration should overwrite the first
	// (Alias is not locked)
	c.parseLine("myalias");
	// cmd2 doesn't exist, so it's logged as unknown
	BOOST_CHECK_EQUAL(c.getLog().size(), 1u);
	BOOST_CHECK(c.getLog().front().find("UNKNOWN COMMAND: cmd2") != string::npos);
}

// --- Special command ---

// C-style function for special command registration
static string specialCmdFunc(int idx, list<string> const&)
{
	return to_string(idx);
}

BOOST_AUTO_TEST_CASE(special_command_invocation)
{
	Console c;

	c.registerSpecialCommand("spcmd", 42, specialCmdFunc);

	// invoke() returns the result directly (parseLine() would log it)
	string result = c.invoke("spcmd", list<string>(), false);
	BOOST_CHECK(result.find("UNKNOWN") == string::npos);
	BOOST_CHECK_EQUAL(result, "42");
}

// --- Duplicate registration ---

BOOST_AUTO_TEST_CASE(duplicate_command_rejected)
{
	Console c;

	c.registerCommands()("mycmd", [](list<string> const&) { return ""; });
	c.registerCommands()("mycmd", [](list<string> const&) { return ""; });

	// Second registration should be rejected (command already exists)
	// The command should still work (first registration)
	c.invoke("mycmd", list<string>(), false);
	BOOST_CHECK_EQUAL(c.getLog().size(), 0u); // No error logged
}

// --- Temp command ---

BOOST_AUTO_TEST_CASE(temp_command_removed)
{
	Console c;

	c.registerCommands()("tempcmd", [](list<string> const&) { return ""; }, true);
	c.registerCommands()("permcmd", [](list<string> const&) { return ""; });

	c.clearTemporaries();

	// tempcmd should be removed
	BOOST_CHECK_EQUAL(c.invoke("tempcmd", list<string>(), false).find("UNKNOWN"),
		string::npos ? false : true);
	// permcmd should still work
	c.invoke("permcmd", list<string>(), false);
	BOOST_CHECK_EQUAL(c.getLog().size(), 0u);
}

// --- Command that modifies state ---

BOOST_AUTO_TEST_CASE(command_modifies_variable)
{
	Console c;
	int val = 0;
	c.registerVariables()("my_int", &val, 0);

	c.registerCommands()("double", [&](list<string> const&) {
		val *= 2;
		return "";
	});

	c.parseLine("my_int 5");
	BOOST_CHECK_EQUAL(val, 5);

	c.parseLine("double");
	BOOST_CHECK_EQUAL(val, 10);

	c.parseLine("double");
	BOOST_CHECK_EQUAL(val, 20);
}

// --- Command with quoted args ---

BOOST_AUTO_TEST_CASE(command_with_quoted_args)
{
	Console c;
	string received;

	c.registerCommands()("say", [&](list<string> const& args) {
		received = args.front();
		return received;
	});

	c.parseLine("say \"hello world\"");
	BOOST_CHECK_EQUAL(received, "hello world");
}

// --- Nested command expansion in arguments ---

BOOST_AUTO_TEST_CASE(nested_command_in_argument)
{
	Console c;
	string received;

	c.registerCommands()("inner", [&](list<string> const&) {
		return "inner_result";
	});

	c.registerCommands()("outer", [&](list<string> const& args) {
		received = args.front();
		return "";
	});

	c.parseLine("outer {inner}");
	BOOST_CHECK_EQUAL(received, "inner_result");
}

BOOST_AUTO_TEST_SUITE_END()
