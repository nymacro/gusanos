// test_tcp.cpp
// Tests for TCP socket mock layer and socket error handling.

#include <boost/test/unit_test.hpp>
#include "tests/tcp_mock.h"

BOOST_AUTO_TEST_SUITE(tcp)

BOOST_AUTO_TEST_CASE(mock_socket_connects_on_think) {
	TCP::MockSocket sock;
	// Initially not connected
	BOOST_CHECK(!sock.isConnected());

	// First think() should complete the simulated connection
	bool done = sock.think();
	BOOST_CHECK(!done);
	BOOST_CHECK(sock.isConnected());
}

BOOST_AUTO_TEST_CASE(mock_socket_connect_error) {
	TCP::MockSocket sock;
	sock.setConnectionState(TCP::MockSocket::ErrorConnect);

	bool done = sock.think();
	BOOST_CHECK(done);
	BOOST_CHECK_EQUAL(sock.getError(), TCP::Socket::ErrorConnect);
}

BOOST_AUTO_TEST_CASE(mock_socket_disconnect) {
	TCP::MockSocket sock;
	sock.setConnectionState(TCP::MockSocket::Disconnected);

	bool done = sock.think();
	BOOST_CHECK(done);
	BOOST_CHECK_EQUAL(sock.getError(), TCP::Socket::ErrorDisconnect);
}

BOOST_AUTO_TEST_CASE(mock_socket_inject_and_read_data) {
	TCP::MockSocket sock;
	sock.setConnectionState(TCP::MockSocket::Connected);
	sock.injectData("Hello, World!");

	// Use readChunk() to get data (think() only handles connection state)
	sock.readChunk();

	BOOST_CHECK(sock.getDataLength() > 0);
	BOOST_CHECK(std::string(sock.getDataBegin(), sock.getDataLength()).find("Hello") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(mock_socket_large_data_split) {
	TCP::MockSocket sock;
	sock.setConnectionState(TCP::MockSocket::Connected);

	// Inject more data than the buffer size (1024)
	std::string bigData(2048, 'A');
	sock.injectData(bigData.data(), bigData.size());

	int chunks = 0;
	while (!sock.readChunk()) {
		if (sock.getDataLength() > 0)
			chunks++;
	}

	// Should have gotten at least 2 chunks (buffer size is 1024)
	BOOST_CHECK(chunks >= 2);
}

BOOST_AUTO_TEST_CASE(mock_socket_empty_data) {
	TCP::MockSocket sock;
	sock.setConnectionState(TCP::MockSocket::Connected);
	sock.injectData("");

	bool done = sock.think();
	BOOST_CHECK(!done);
	BOOST_CHECK_EQUAL(sock.getDataLength(), 0);
}

BOOST_AUTO_TEST_CASE(base_socket_invalid_fd_error) {
	// A real Socket with fd=0 should be treated as an error
	// (fd=0 is stdin, not a valid network socket)
	TCP::Socket sock(0);
	bool done = sock.think();
	BOOST_CHECK(done);
	BOOST_CHECK_EQUAL(sock.getError(), TCP::Socket::ErrorConnect);
}

BOOST_AUTO_TEST_CASE(base_socket_negative_fd_error) {
	TCP::Socket sock(-1);
	bool done = sock.think();
	BOOST_CHECK(done);
	BOOST_CHECK_EQUAL(sock.getError(), TCP::Socket::ErrorConnect);
}

BOOST_AUTO_TEST_SUITE_END()
