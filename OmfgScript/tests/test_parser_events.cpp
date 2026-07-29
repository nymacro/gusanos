// Tests for OmfgScript event and action parsing.

#include <boost/test/unit_test.hpp>

#include "omfg_script.h"
#include "base_action.h"

#include <sstream>
#include <vector>

using namespace OmfgScript;

namespace {

class TestAction : public BaseAction {
  public:
	TestAction(std::vector<TokenBase *> const &params)
		: value(params[0]->toInt()) {}

	void run(ActionParams const &) override {
		// No-op for parser tests.
	}

	int value;
};

BaseAction *createTestAction(std::vector<TokenBase *> const &params) {
	return new TestAction(params);
}

struct Fixture {
	ActionFactory af;
	std::istringstream stream;
	Parser parser;

	Fixture(std::string const &script)
		: stream(script), parser(stream, af, "test.obj") {
		af.add("test_action", createTestAction, 0)("value", false);
	}
};

} // namespace

BOOST_AUTO_TEST_SUITE(parser_events)

BOOST_AUTO_TEST_CASE(parse_simple_action) {
	Fixture f(
		"on creation()\n"
		"    test_action(123)\n"
	);
	f.parser.addEvent("creation", 0, 0);
	BOOST_REQUIRE(f.parser.run());

	bool foundAction = false;
	for (Parser::EventIter it(f.parser); it; ++it) {
		std::vector<BaseAction *> const &actions = it.actions();
		BOOST_REQUIRE_EQUAL(actions.size(), 1u);
		TestAction const *ta = dynamic_cast<TestAction const *>(actions[0]);
		BOOST_REQUIRE(ta);
		BOOST_CHECK_EQUAL(ta->value, 123);
		foundAction = true;
	}
	BOOST_CHECK(foundAction);
}

BOOST_AUTO_TEST_CASE(parse_event_with_param) {
	Fixture f(
		"on timer(delay = 10)\n"
		"    test_action(20)\n"
	);
	f.parser.addEvent("timer", 1, 0)("delay", false);
	BOOST_REQUIRE(f.parser.run());

	bool foundAction = false;
	for (Parser::EventIter it(f.parser); it; ++it) {
		BOOST_REQUIRE_EQUAL(it.type(), 1);
		std::vector<TokenBase *> const &params = it.params();
		BOOST_REQUIRE_EQUAL(params.size(), 1u);
		BOOST_CHECK_EQUAL(params[0]->toInt(), 10);

		std::vector<BaseAction *> const &actions = it.actions();
		BOOST_REQUIRE_EQUAL(actions.size(), 1u);
		TestAction const *ta = dynamic_cast<TestAction const *>(actions[0]);
		BOOST_REQUIRE(ta);
		BOOST_CHECK_EQUAL(ta->value, 20);
		foundAction = true;
	}
	BOOST_CHECK(foundAction);
}

BOOST_AUTO_TEST_CASE(parse_multiple_actions) {
	Fixture f(
		"on creation()\n"
		"    test_action(1)\n"
		"    test_action(2)\n"
	);
	f.parser.addEvent("creation", 0, 0);
	BOOST_REQUIRE(f.parser.run());

	for (Parser::EventIter it(f.parser); it; ++it) {
		std::vector<BaseAction *> const &actions = it.actions();
		BOOST_REQUIRE_EQUAL(actions.size(), 2u);
		BOOST_CHECK_EQUAL(dynamic_cast<TestAction const *>(actions[0])->value, 1);
		BOOST_CHECK_EQUAL(dynamic_cast<TestAction const *>(actions[1])->value, 2);
	}
}

// Unknown events are a semantic error and cause parsing to fail.
BOOST_AUTO_TEST_CASE(parse_unknown_event_fails) {
	Fixture f(
		"on not_registered()\n"
		"    test_action(1)\n"
	);
	f.parser.addEvent("creation", 0, 0);
	BOOST_CHECK(!f.parser.run());
}

BOOST_AUTO_TEST_SUITE_END()
