// Tests for comment handling in console parsing.
// '#' starts a comment (line or inline), respecting quoted strings.

#include <boost/test/unit_test.hpp>
#include "console.h"

using namespace std;

BOOST_AUTO_TEST_SUITE(comment_handling)

// --- Full-line comments ---

BOOST_AUTO_TEST_CASE(full_line_comment_stripped)
{
	Console c;
	c.parseLine("# this is a comment");
	// Should not produce any log entry or error
	BOOST_CHECK_EQUAL(c.getLog().size(), 0u);
}

BOOST_AUTO_TEST_CASE(full_line_comment_with_leading_whitespace)
{
	Console c;
	c.parseLine("  # comment with leading spaces");
	BOOST_CHECK_EQUAL(c.getLog().size(), 0u);
}

BOOST_AUTO_TEST_CASE(full_line_comment_with_tab)
{
	Console c;
	c.parseLine("\t# tab before comment");
	BOOST_CHECK_EQUAL(c.getLog().size(), 0u);
}

// --- Inline comments ---

BOOST_AUTO_TEST_CASE(inline_comment_strips_rest)
{
	Console c;
	int val = 0;
	c.registerVariables()("test_val", &val, 0);

	c.parseLine("test_val 42 # set to 42");
	BOOST_CHECK_EQUAL(val, 42);
}

BOOST_AUTO_TEST_CASE(inline_comment_no_args)
{
	Console c;
	int val = 0;
	c.registerVariables()("test_val", &val, 0);

	c.parseLine("test_val # just a comment");
	// No args means get the value, not set it
	BOOST_CHECK_EQUAL(val, 0);
}

BOOST_AUTO_TEST_CASE(inline_comment_multiple_args)
{
	Console c;
	int val = 0;
	c.registerVariables()("test_val", &val, 0);

	c.parseLine("test_val 100 # set to 100, not 200");
	BOOST_CHECK_EQUAL(val, 100);
}

// --- # inside quoted strings ---

BOOST_AUTO_TEST_CASE(hash_in_quoted_string_preserved)
{
	Console c;
	string val;
	c.registerVariables()("test_str", &val, string(""));

	c.parseLine("test_str \"hello # world\"");
	BOOST_CHECK_EQUAL(val, "hello # world");
}

BOOST_AUTO_TEST_CASE(hash_in_quoted_string_inline_comment)
{
	Console c;
	string val;
	c.registerVariables()("test_str", &val, string(""));

	// The # inside quotes is preserved; the # after closing quote is the comment
	c.parseLine("test_str \"hello # world\" # trailing comment");
	BOOST_CHECK_EQUAL(val, "hello # world");
}

BOOST_AUTO_TEST_CASE(hash_at_start_of_quoted_string)
{
	Console c;
	string val;
	c.registerVariables()("test_str", &val, string(""));

	c.parseLine("test_str \"#notacomment\"");
	BOOST_CHECK_EQUAL(val, "#notacomment");
}

// --- Escaped characters ---

BOOST_AUTO_TEST_CASE(escaped_backslash_preserves_quote_state)
{
	Console c;
	string val;
	c.registerVariables()("test_str", &val, string(""));

	// \\\\ is a valid escape (produces literal \\). The # after it
	// is still inside quotes because \\ is escaped, not a quote toggle.
	c.parseLine("test_str \"value with \\\\ # in string\"");
	BOOST_CHECK_EQUAL(val, "value with \\ # in string");
}

// --- # is treated as a comment, not a command ---

BOOST_AUTO_TEST_CASE(hash_is_comment_not_command)
{
	Console c;
	// '#' alone is a full-line comment, silently ignored
	c.parseLine("#");
	BOOST_CHECK_EQUAL(c.getLog().size(), 0u);
}

// --- Multiple comments ---

BOOST_AUTO_TEST_CASE(multiple_comments_in_sequence)
{
	Console c;
	c.parseLine("# first comment");
	c.parseLine("# second comment");
	c.parseLine("# third comment");
	BOOST_CHECK_EQUAL(c.getLog().size(), 0u);
}

// --- Comment after semicolon-separated commands ---

BOOST_AUTO_TEST_CASE(comment_after_semicolon)
{
	Console c;
	int a = 0, b = 0;
	c.registerVariables()("var_a", &a, 0);
	c.registerVariables()("var_b", &b, 0);

	c.parseLine("var_a 10; var_b 20 # both set");
	BOOST_CHECK_EQUAL(a, 10);
	BOOST_CHECK_EQUAL(b, 20);
}

// --- Edge cases ---

BOOST_AUTO_TEST_CASE(empty_line_after_stripping)
{
	Console c;
	c.parseLine("#");
	BOOST_CHECK_EQUAL(c.getLog().size(), 0u);
}

BOOST_AUTO_TEST_CASE(whitespace_only_after_stripping)
{
	Console c;
	c.parseLine("# comment");
	BOOST_CHECK_EQUAL(c.getLog().size(), 0u);
}

BOOST_AUTO_TEST_CASE(hash_at_end_of_line)
{
	Console c;
	int val = 0;
	c.registerVariables()("test_val", &val, 0);

	c.parseLine("test_val 5#");
	// The '#' at the end is treated as a comment, but there's no space before it
	// stripComment sees '5' then '#' - the '#' starts a comment
	// So the result is "test_val 5" which parses correctly
	BOOST_CHECK_EQUAL(val, 5);
}

BOOST_AUTO_TEST_SUITE_END()
