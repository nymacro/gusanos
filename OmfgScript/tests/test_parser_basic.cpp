// Tests for basic OmfgScript parser functionality: scalar property parsing
// and retrieval with default values.

#include <boost/test/unit_test.hpp>

#include "omfg_script.h"

#include <sstream>
#include <stdexcept>

using namespace OmfgScript;

namespace {

struct Fixture {
	ActionFactory af;
	std::istringstream stream;
	Parser parser;

	Fixture(std::string const &script) : stream(script), parser(stream, af, "test.obj") {}
};

} // namespace

BOOST_AUTO_TEST_SUITE(parser_basic)

// --- Integer properties ---

BOOST_AUTO_TEST_CASE(int_property) {
	Fixture f("my_int = 42\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK_EQUAL(f.parser.getInt("my_int"), 42);
}

BOOST_AUTO_TEST_CASE(int_negative_property) {
	Fixture f("my_int = -17\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK_EQUAL(f.parser.getInt("my_int"), -17);
}

BOOST_AUTO_TEST_CASE(int_default_when_missing) {
	Fixture f("other = 1\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK_EQUAL(f.parser.getInt("my_int", 99), 99);
}

// --- Double properties ---

BOOST_AUTO_TEST_CASE(double_property) {
	Fixture f("my_double = 3.14\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK_CLOSE(f.parser.getDouble("my_double"), 3.14, 0.0001);
}

BOOST_AUTO_TEST_CASE(double_default_when_missing) {
	Fixture f("other = 1\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK_CLOSE(f.parser.getDouble("my_double", 2.5), 2.5, 0.0001);
}

// --- String properties ---

BOOST_AUTO_TEST_CASE(string_property) {
	Fixture f("my_string = hello_world\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK_EQUAL(f.parser.getString("my_string"), "hello_world");
}

BOOST_AUTO_TEST_CASE(string_property_with_path) {
	Fixture f("sprite = default/sprites/foo.png\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK_EQUAL(f.parser.getString("sprite"), "default/sprites/foo.png");
}

BOOST_AUTO_TEST_CASE(string_default_when_missing) {
	Fixture f("other = 1\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK_EQUAL(f.parser.getString("my_string", "fallback"), "fallback");
}

// --- Boolean properties ---

BOOST_AUTO_TEST_CASE(bool_true_property) {
	Fixture f("my_bool = true\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK(f.parser.getBool("my_bool") == true);
}

BOOST_AUTO_TEST_CASE(bool_false_property) {
	Fixture f("my_bool = false\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK(f.parser.getBool("my_bool") == false);
}

BOOST_AUTO_TEST_CASE(bool_default_when_missing) {
	Fixture f("other = 1\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK(f.parser.getBool("my_bool", true) == true);
}

// --- Raw property access ---

BOOST_AUTO_TEST_CASE(raw_property_returns_token) {
	Fixture f("my_value = 123\n");
	BOOST_REQUIRE(f.parser.run());
	TokenBase *t = f.parser.getRawProperty("my_value");
	BOOST_REQUIRE(t);
	BOOST_CHECK_EQUAL(t->toInt(), 123);
}

BOOST_AUTO_TEST_CASE(raw_property_missing_returns_null) {
	Fixture f("other = 1\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK(f.parser.getRawProperty("missing") == nullptr);
}

// --- Type mismatch handling ---

BOOST_AUTO_TEST_CASE(double_on_string_throws) {
	Fixture f("my_prop = hello\n");
	BOOST_REQUIRE(f.parser.run());
	TokenBase *t = f.parser.getRawProperty("my_prop");
	BOOST_REQUIRE(t);
	BOOST_CHECK_THROW(t->toDouble(), std::runtime_error);
}

// --- Multiple properties ---

BOOST_AUTO_TEST_CASE(multiple_properties) {
	Fixture f("gravity = 0.05\n"
			  "speed = 12\n"
			  "name = fastball\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK_CLOSE(f.parser.getDouble("gravity"), 0.05, 0.0001);
	BOOST_CHECK_EQUAL(f.parser.getInt("speed"), 12);
	BOOST_CHECK_EQUAL(f.parser.getString("name"), "fastball");
}

BOOST_AUTO_TEST_SUITE_END()
