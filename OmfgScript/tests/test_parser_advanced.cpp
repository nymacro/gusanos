// Tests for advanced OmfgScript parser features: lists, functions,
// nested property blocks, and CRC calculation.

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

BOOST_AUTO_TEST_SUITE(parser_advanced)

// --- Lists ---

BOOST_AUTO_TEST_CASE(list_property_integers) {
	Fixture f("my_list = [1, 2, 3]\n");
	BOOST_REQUIRE(f.parser.run());
	std::list<TokenBase *> const &list = f.parser.getList("my_list");
	BOOST_REQUIRE_EQUAL(list.size(), 3u);
	auto it = list.begin();
	BOOST_CHECK_EQUAL((*it++)->toInt(), 1);
	BOOST_CHECK_EQUAL((*it++)->toInt(), 2);
	BOOST_CHECK_EQUAL((*it++)->toInt(), 3);
}

BOOST_AUTO_TEST_CASE(list_property_mixed) {
	Fixture f("my_list = [42, 3.14, foo]\n");
	BOOST_REQUIRE(f.parser.run());
	std::list<TokenBase *> const &list = f.parser.getList("my_list");
	BOOST_REQUIRE_EQUAL(list.size(), 3u);
	auto it = list.begin();
	BOOST_CHECK_EQUAL((*it++)->toInt(), 42);
	BOOST_CHECK_CLOSE((*it++)->toDouble(), 3.14, 0.0001);
	BOOST_CHECK_EQUAL((*it++)->toString(), "foo");
}

BOOST_AUTO_TEST_CASE(list_default_when_missing) {
	Fixture f("other = 1\n");
	BOOST_REQUIRE(f.parser.run());
	std::list<TokenBase *> def;
	std::list<TokenBase *> const &list = f.parser.getList("missing", def);
	BOOST_CHECK(list.empty());
}

// --- Functions ---

BOOST_AUTO_TEST_CASE(function_property_with_args) {
	Fixture f("my_func = rgba(255, 128, 64, 32)\n");
	BOOST_REQUIRE(f.parser.run());
	Function const *func = f.parser.getFunction("my_func");
	BOOST_REQUIRE(func);
	BOOST_CHECK_EQUAL(func->name, "rgba");
	BOOST_REQUIRE_EQUAL(func->params.size(), 4u);
	BOOST_CHECK_EQUAL(func->params[0]->toInt(), 255);
	BOOST_CHECK_EQUAL(func->params[1]->toInt(), 128);
	BOOST_CHECK_EQUAL(func->params[2]->toInt(), 64);
	BOOST_CHECK_EQUAL(func->params[3]->toInt(), 32);
}

BOOST_AUTO_TEST_CASE(function_missing_returns_null) {
	Fixture f("other = 1\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK(f.parser.getFunction("missing") == nullptr);
}

// --- Nested property blocks ---

BOOST_AUTO_TEST_CASE(nested_block_properties) {
	Fixture f("collision {\n"
			  "    damage = 10\n"
			  "    push = 2.5\n"
			  "}\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK_EQUAL(f.parser.getInt("collision_damage"), 10);
	BOOST_CHECK_CLOSE(f.parser.getDouble("collision_push"), 2.5, 0.0001);
}

BOOST_AUTO_TEST_CASE(nested_block_empty) {
	Fixture f("empty_block {\n}\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK(f.parser.getRawProperty("empty_block_anything") == nullptr);
}

// --- CRC ---

BOOST_AUTO_TEST_CASE(crc_is_stable_for_same_input) {
	Fixture f1("gravity = 0.05\nspeed = 10\n");
	Fixture f2("gravity = 0.05\nspeed = 10\n");
	BOOST_REQUIRE(f1.parser.run());
	BOOST_REQUIRE(f2.parser.run());
	BOOST_CHECK_EQUAL(f1.parser.getCRC(), f2.parser.getCRC());
}

BOOST_AUTO_TEST_CASE(crc_changes_with_value) {
	Fixture f1("speed = 10\n");
	Fixture f2("speed = 11\n");
	BOOST_REQUIRE(f1.parser.run());
	BOOST_REQUIRE(f2.parser.run());
	BOOST_CHECK(f1.parser.getCRC() != f2.parser.getCRC());
}

BOOST_AUTO_TEST_SUITE_END()
