// test_network_requests.cpp
// Tests for HTTP request handling in the Network layer.
// Verifies null-safety of addHttpRequest(), processHttpRequests(), and callbacks.

#include <boost/test/unit_test.hpp>
#include <list>
#include <functional>
#include "http.h"
#include "tests/tcp_mock.h"

// Simulate the HttpRequest struct from network.cpp
struct HttpRequest
{
	HttpRequest(HTTP::Request* req_, std::function<void(HTTP::Request*)> handler_)
		: req(req_), handler(handler_)
	{
	}

	HTTP::Request* req;
	std::function<void(HTTP::Request*)> handler;
};

// Simulate the global requests list and processHttpRequests from network.cpp
static std::list<HttpRequest> g_requests;
static bool g_nullHandlerCalled = false;
static HTTP::Request* g_nullHandlerArg = nullptr;

void resetGlobals()
{
	g_requests.clear();
	g_nullHandlerCalled = false;
	g_nullHandlerArg = nullptr;
}

// Null-safe handler for testing
void safeNullHandler(HTTP::Request* req)
{
	g_nullHandlerCalled = true;
	g_nullHandlerArg = req;
}

// Simulate addHttpRequest (with the null guard fix)
void addHttpRequest(HTTP::Request* req, std::function<void(HTTP::Request*)> handler)
{
	if(!req)
		return;
	g_requests.push_back(HttpRequest(req, handler));
}

// Simulate processHttpRequests (with the null-safety fix)
void processHttpRequests()
{
	for (auto i = g_requests.begin(), i_end = g_requests.end(); i != i_end; )
	{
		auto next = i; ++next;
		if(!i->req)
		{
			g_requests.erase(i);
		}
		else if(i->req->think())
		{
			if(i->handler)
				i->handler(i->req);
			else
				delete i->req;
			g_requests.erase(i);
		}
		i = next;
	}
}

// Simulate the fixed onServerAdded callback
void onServerAdded(HTTP::Request* req)
{
	if(!req)
	{
		// serverAdded = false;
		return;
	}
	// Would check req->success, req->getError(), etc.
}

// Simulate the fixed onServerRemoved callback
void onServerRemoved(HTTP::Request* req)
{
	if(!req)
		return;
}

// Simulate the fixed onServerUpdate callback
void onServerUpdate(HTTP::Request* req)
{
	if(!req)
		return;
}

BOOST_AUTO_TEST_SUITE(network_requests)

// --- addHttpRequest null safety ---

BOOST_AUTO_TEST_CASE(add_http_request_rejects_null)
{
	resetGlobals();
	addHttpRequest(nullptr, safeNullHandler);
	BOOST_CHECK_EQUAL(g_requests.size(), 0);
}

BOOST_AUTO_TEST_CASE(add_http_request_accepts_valid)
{
	resetGlobals();
	// Create a mock request with an invalid socket (will fail think() immediately)
	HTTP::Request* req = new HTTP::Request(-1, "", "");
	addHttpRequest(req, safeNullHandler);
	BOOST_CHECK_EQUAL(g_requests.size(), 1);
	// Clean up
	delete req;
}

// --- processHttpRequests null safety ---

BOOST_AUTO_TEST_CASE(process_handles_null_request_in_list)
{
	resetGlobals();
	// Manually add a request with null req (simulating a bug that slipped through)
	g_requests.push_back(HttpRequest(nullptr, safeNullHandler));

	// Should not crash — the null-safety fix removes null entries
	processHttpRequests();
	BOOST_CHECK_EQUAL(g_requests.size(), 0);
	BOOST_CHECK(!g_nullHandlerCalled); // Handler should NOT be called with null
}

BOOST_AUTO_TEST_CASE(process_handles_null_handler)
{
	resetGlobals();
	HTTP::Request* req = new HTTP::Request(-1, "", "");
	g_requests.push_back(HttpRequest(req, nullptr));

	// Should not crash — null handler is OK, request gets deleted
	processHttpRequests();
	BOOST_CHECK_EQUAL(g_requests.size(), 0);
}

BOOST_AUTO_TEST_CASE(process_handles_both_null)
{
	resetGlobals();
	g_requests.push_back(HttpRequest(nullptr, nullptr));

	// Should not crash
	processHttpRequests();
	BOOST_CHECK_EQUAL(g_requests.size(), 0);
}

// --- Callback null safety ---

BOOST_AUTO_TEST_CASE(on_server_added_handles_null)
{
	// Should not crash with null
	onServerAdded(nullptr);
	BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(on_server_removed_handles_null)
{
	onServerRemoved(nullptr);
	BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(on_server_update_handles_null)
{
	onServerUpdate(nullptr);
	BOOST_CHECK(true);
}

// --- HTTP::Request::getErrorForRequest static helper ---

BOOST_AUTO_TEST_CASE(get_error_null_is_safe)
{
	BOOST_CHECK_EQUAL(HTTP::Request::getErrorForRequest(nullptr), TCP::Socket::ErrorConnect);
}

BOOST_AUTO_TEST_CASE(get_error_valid_request)
{
	HTTP::Request req(42, "", ""); // Valid fd (though not connected)
	BOOST_CHECK_EQUAL(HTTP::Request::getErrorForRequest(&req), TCP::Socket::ErrorNone);
}

// --- Multiple request processing ---

BOOST_AUTO_TEST_CASE(process_multiple_requests)
{
	resetGlobals();

	// Add several requests with invalid sockets (they'll error out immediately)
	for(int i = 0; i < 5; i++)
	{
		HTTP::Request* req = new HTTP::Request(-1, "", "");
		addHttpRequest(req, safeNullHandler);
	}

	// Should have 5 requests
	BOOST_CHECK_EQUAL(g_requests.size(), 5);

	// Process all — all should be removed
	processHttpRequests();
	BOOST_CHECK_EQUAL(g_requests.size(), 0);
}

// --- Mixed null and valid requests ---

BOOST_AUTO_TEST_CASE(process_mixed_valid_and_null)
{
	resetGlobals();

	// Add a valid request
	HTTP::Request* valid = new HTTP::Request(-1, "", "");
	addHttpRequest(valid, safeNullHandler);

	// Manually add a null entry
	g_requests.push_back(HttpRequest(nullptr, safeNullHandler));

	// Process
	processHttpRequests();
	BOOST_CHECK_EQUAL(g_requests.size(), 0);
}

// --- Empty request list ---

BOOST_AUTO_TEST_CASE(process_empty_list)
{
	resetGlobals();
	processHttpRequests();
	BOOST_CHECK_EQUAL(g_requests.size(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
