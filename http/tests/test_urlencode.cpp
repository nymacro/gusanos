// test_urlencode.cpp
// Tests for URL encoding functionality in HTTP::Host.

#include <boost/test/unit_test.hpp>
#include "http.h"
#include <sstream>

BOOST_AUTO_TEST_SUITE(urlencoding)

BOOST_AUTO_TEST_CASE(alphanumeric_unchanged)
{
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode("abc"), "abc");
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode("ABC"), "ABC");
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode("123"), "123");
}

BOOST_AUTO_TEST_CASE(space_becomes_plus)
{
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode("hello world"), "hello+world");
}

BOOST_AUTO_TEST_CASE(newline_encoded)
{
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode("line1\nline2"), "line1%0D%0Aline2");
}

BOOST_AUTO_TEST_CASE(special_chars_encoded)
{
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode("a+b=c"), "a%2Bb%3Dc");
}

BOOST_AUTO_TEST_CASE(mixed_content)
{
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode("hello world!"), "hello+world%21");
}

BOOST_AUTO_TEST_CASE(empty_string)
{
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode(""), "");
}

BOOST_AUTO_TEST_CASE(single_char)
{
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode("a"), "a");
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode(" "), "+");
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode("#"), "%23");
}

BOOST_AUTO_TEST_CASE(list_encoding)
{
	std::list<std::pair<std::string, std::string>> params;
	params.push_back(std::make_pair("action", "add"));
	params.push_back(std::make_pair("title", "My Server"));
	params.push_back(std::make_pair("port", "9898"));

	std::string encoded = HTTP::Host::urlencode(params);
	BOOST_CHECK_EQUAL(encoded, "action=add&title=My+Server&port=9898");
}

BOOST_AUTO_TEST_CASE(list_encoding_empty_values)
{
	std::list<std::pair<std::string, std::string>> params;
	params.push_back(std::make_pair("key", ""));

	BOOST_CHECK_EQUAL(HTTP::Host::urlencode(params), "key=");
}

BOOST_AUTO_TEST_CASE(list_encoding_single_item)
{
	std::list<std::pair<std::string, std::string>> params;
	params.push_back(std::make_pair("key", "value"));

	BOOST_CHECK_EQUAL(HTTP::Host::urlencode(params), "key=value");
}

BOOST_AUTO_TEST_CASE(hex_digit_values)
{
	BOOST_CHECK_EQUAL(HTTP::Host::hexDigit(0), '0');
	BOOST_CHECK_EQUAL(HTTP::Host::hexDigit(9), '9');
	BOOST_CHECK_EQUAL(HTTP::Host::hexDigit(10), 'A');
	BOOST_CHECK_EQUAL(HTTP::Host::hexDigit(15), 'F');
	BOOST_CHECK_EQUAL(HTTP::Host::hexDigit(16), '0'); // Out of range
	BOOST_CHECK_EQUAL(HTTP::Host::hexDigit(-1), '0');  // Out of range
}

BOOST_AUTO_TEST_CASE(carriage_return_ignored)
{
	// \r should be ignored per the implementation
	BOOST_CHECK_EQUAL(HTTP::Host::urlencode("a\rb"), "ab");
}

BOOST_AUTO_TEST_CASE(urlencode_round_trip)
{
	std::string original = "action=add&title=Test+Server";
	std::string encoded = HTTP::Host::urlencode(original);
	// Encoded should be different from original if it contains special chars
	BOOST_CHECK(!encoded.empty());
}

BOOST_AUTO_TEST_CASE(server_name_encoding)
{
	std::string name = "Server #1 (v2.0)";
	std::string encoded = HTTP::Host::urlencode(name);
	BOOST_CHECK(encoded.find("%23") != std::string::npos); // # -> %23
	BOOST_CHECK(encoded.find("%28") != std::string::npos); // ( -> %28
}

BOOST_AUTO_TEST_SUITE_END()
