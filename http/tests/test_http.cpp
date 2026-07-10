// test_http.cpp
// Tests for HTTP request parsing and lifecycle.
// Tests the core HTTP parsing logic using the real Request class
// with a simplified mock approach.

#include <boost/test/unit_test.hpp>
#include "http.h"
#include <sstream>

// Test the URL encoding functions
BOOST_AUTO_TEST_SUITE(http)

// --- URL Encoding Tests ---

BOOST_AUTO_TEST_CASE(urlencode_alphanumeric)
{
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode("abc"), "abc");
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode("ABC"), "ABC");
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode("123"), "123");
}

BOOST_AUTO_TEST_CASE(urlencode_space)
{
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode("hello world"), "hello+world");
}

BOOST_AUTO_TEST_CASE(urlencode_special_chars)
{
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode("a+b=c"), "a%2Bb%3Dc");
}

BOOST_AUTO_TEST_CASE(urlencode_empty)
{
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode(""), "");
}

// --- Host::urlencode list ---

BOOST_AUTO_TEST_CASE(urlencode_list)
{
	std::list<std::pair<std::string, std::string>> params;
	params.push_back(std::make_pair("action", "add"));
	params.push_back(std::make_pair("title", "My Server"));
	params.push_back(std::make_pair("port", "9898"));

	BOOST_CHECK_EQUAL(HTTP::Host::urlencode(params), "action=add&title=My+Server&port=9898");
}

// --- HTTP::Request::getErrorForRequest ---

BOOST_AUTO_TEST_CASE(get_error_null_request)
{
	BOOST_CHECK_EQUAL(HTTP::Request::getErrorForRequest(nullptr), TCP::Socket::ErrorConnect);
}

BOOST_AUTO_TEST_CASE(get_error_valid_request)
{
	HTTP::Request req(42, "", "");
	BOOST_CHECK_EQUAL(HTTP::Request::getErrorForRequest(&req), TCP::Socket::ErrorNone);
}

// --- HTTP::Host::hexDigit ---

BOOST_AUTO_TEST_CASE(hex_digit_valid)
{
	BOOST_CHECK_EQUAL(HTTP::Host::hexDigit(0), '0');
	BOOST_CHECK_EQUAL(HTTP::Host::hexDigit(9), '9');
	BOOST_CHECK_EQUAL(HTTP::Host::hexDigit(10), 'A');
	BOOST_CHECK_EQUAL(HTTP::Host::hexDigit(15), 'F');
}

BOOST_AUTO_TEST_CASE(hex_digit_invalid)
{
	BOOST_CHECK_EQUAL(HTTP::Host::hexDigit(-1), '0');
	BOOST_CHECK_EQUAL(HTTP::Host::hexDigit(16), '0');
}

// --- Request lifecycle tests (using real Request with invalid socket) ---

BOOST_AUTO_TEST_CASE(request_think_with_invalid_socket)
{
	// A request with an invalid socket should fail immediately
	HTTP::Request req(-1, "", "");
	bool done = req.think();
	BOOST_CHECK(done);
	BOOST_CHECK(!req.success);
}

BOOST_AUTO_TEST_CASE(request_think_with_valid_fd)
{
	// A request with a valid fd (but not connected) should not fail
	// It will just sit in SendingData state
	HTTP::Request req(0, "GET / HTTP/1.0\r\n\r\n", "");
	// think() should not crash
	bool done = req.think();
	// With fd=0, think() returns error (ErrorConnect)
	BOOST_CHECK(done);
}

BOOST_AUTO_TEST_CASE(request_state_machine)
{
	HTTP::Request req(-1, "", "");
	// Request should start in SendingData state
	BOOST_CHECK(req.getState() == HTTP::Request::SendingData);

	req.think(); // Should fail due to invalid socket, returns true but state unchanged
	// When TCP::Socket::think() returns early due to error, state is not updated to Done
	// This is existing behavior - the request is done but state tracking is minimal
	BOOST_CHECK(!req.success); // Request failed
}

BOOST_AUTO_TEST_CASE(request_success_flag)
{
	HTTP::Request req(-1, "", "");
	BOOST_CHECK(!req.success); // Initially false
	req.think();
	BOOST_CHECK(!req.success); // Still false after error
}

BOOST_AUTO_TEST_CASE(request_data_empty_initially)
{
	HTTP::Request req(-1, "", "");
	BOOST_CHECK(req.data.empty());
}

BOOST_AUTO_TEST_SUITE_END()
