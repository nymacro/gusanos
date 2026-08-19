// Tests that verify resource cleanup in the OmfgScript parser.
// When run under AddressSanitizer / LeakSanitizer these cases exercise
// ownership paths that previously leaked (e.g. duplicate property keys).

#include <boost/test/unit_test.hpp>

#include "omfg_script.h"

#include <sstream>

using namespace OmfgScript;

namespace {

struct Fixture {
	ActionFactory af;
	std::istringstream stream;
	Parser parser;

	Fixture(std::string const &script) : stream(script), parser(stream, af, "test.obj") {}
};

} // namespace

BOOST_AUTO_TEST_SUITE(parser_leaks)

// Reassigning the same property must replace the previous value without
// leaking the old token/property wrapper. This was the 72-byte leak seen
// when loading scripts with duplicate keys such as "gravity = ...".
BOOST_AUTO_TEST_CASE(duplicate_property_replaces_value) {
	Fixture f("gravity = 0.00\n"
			  "gravity = 0.01\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK_CLOSE(f.parser.getDouble("gravity"), 0.01, 0.0001);
}

BOOST_AUTO_TEST_CASE(duplicate_property_multiple_times) {
	Fixture f("value = 1\n"
			  "value = 2\n"
			  "value = 3\n"
			  "value = 4\n"
			  "value = 5\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK_EQUAL(f.parser.getInt("value"), 5);
}

BOOST_AUTO_TEST_CASE(duplicate_property_in_block) {
	Fixture f("block {\n"
			  "    item = alpha\n"
			  "    item = beta\n"
			  "}\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK_EQUAL(f.parser.getString("block_item"), "beta");
}

// Mix of duplicate and unique properties should also be safe.
BOOST_AUTO_TEST_CASE(mixed_duplicate_and_unique_properties) {
	Fixture f("gravity = 0.00\n"
			  "sprite = foo.png\n"
			  "gravity = 0.01\n"
			  "speed = 10\n");
	BOOST_REQUIRE(f.parser.run());
	BOOST_CHECK_CLOSE(f.parser.getDouble("gravity"), 0.01, 0.0001);
	BOOST_CHECK_EQUAL(f.parser.getString("sprite"), "foo.png");
	BOOST_CHECK_EQUAL(f.parser.getInt("speed"), 10);
}

BOOST_AUTO_TEST_SUITE_END()
