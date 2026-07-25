// test_protocol_version.cpp
// Regression tests for the PROTOCOL_VERSION exchange in MSG_CONNECTION_REPLY
// (foundational infra for T2.6/T2.7/T3.2 wire-format versioning).
//   - version_match_accepted: server advertises the current PROTOCOL_VERSION,
//     client accepts (eZCom_ConnAccepted).
//   - version_mismatch_refused: server advertises a future version, client
//     refuses with eZCom_ConnWrongVersion and the connection tears down.

#include <boost/test/unit_test.hpp>
#include <cstdio>

#include "net_control.h"

static int s_pv_port = 27100;

// Server that advertises a configurable protocol version. Tests override
// getProtocolVersion() to simulate version skew without bumping the
// production constant.
class VersionServer : public ZCom_Control {
  public:
	int m_advertiseVersion;
	int m_spawned;
	int m_closed;

	VersionServer(int port, int advertiseVersion) : m_advertiseVersion(advertiseVersion), m_spawned(0), m_closed(0) {
		g_currentControl = this;
		ZCom_initSockets(true, port, 0, 0);
	}
	~VersionServer() {
		g_currentControl = nullptr;
	}

	int getProtocolVersion() const override {
		return m_advertiseVersion;
	}

  protected:
	bool ZCom_cbConnectionRequest(ZCom_ConnID, ZCom_BitStream &, ZCom_BitStream &) override {
		return true;
	}
	void ZCom_cbConnectionSpawned(ZCom_ConnID) override {
		m_spawned++;
	}
	void ZCom_cbConnectionClosed(ZCom_ConnID, eZCom_CloseReason, ZCom_BitStream &) override {
		m_closed++;
	}
};

class VersionClient : public ZCom_Control {
  public:
	bool m_gotResult;
	eZCom_ConnectResult m_result;

	VersionClient() : m_gotResult(false), m_result(eZCom_ConnAccepted) {
		g_currentControl = this;
		ZCom_initSockets(false, 0, 0, 0);
	}
	~VersionClient() {
		g_currentControl = nullptr;
	}

	uint32_t ConnectTo(const char *host, int port) {
		ZCom_Address a;
		char hp[64];
		snprintf(hp, sizeof hp, "%s:%d", host, port);
		a.setAddress(eZCom_AddressUDP, 0, hp);
		return ZCom_Connect(a, nullptr);
	}

  protected:
	void ZCom_cbConnectResult(ZCom_ConnID, eZCom_ConnectResult result, ZCom_BitStream &) override {
		m_gotResult = true;
		m_result = result;
	}
};

static void pumpBoth(ZCom_Control *s, ZCom_Control *c, int n) {
	for (int i = 0; i < n; ++i) {
		g_currentControl = s;
		s->ZCom_processInput(eZCom_NoBlock);
		s->ZCom_processOutput();
		g_currentControl = c;
		c->ZCom_processInput(eZCom_NoBlock);
		c->ZCom_processOutput();
	}
	g_currentControl = nullptr;
}

BOOST_AUTO_TEST_SUITE(protocol_version)

BOOST_AUTO_TEST_CASE(version_match_accepted) {
	g_currentControl = nullptr;
	int port = s_pv_port++;
	VersionServer srv(port, PROTOCOL_VERSION);
	VersionClient cli;
	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);
	pumpBoth(&srv, &cli, 200);
	BOOST_CHECK(cli.m_gotResult);
	BOOST_CHECK_EQUAL(cli.m_result, eZCom_ConnAccepted);
	BOOST_CHECK_EQUAL(srv.m_spawned, 1);
	srv.Shutdown();
	cli.Shutdown();
}

BOOST_AUTO_TEST_CASE(version_mismatch_refused) {
	g_currentControl = nullptr;
	int port = s_pv_port++;
	// Server speaks a future protocol version. The client must refuse with
	// eZCom_ConnWrongVersion rather than silently desyncing on the reply
	// payload that follows the version field.
	VersionServer srv(port, PROTOCOL_VERSION + 1);
	VersionClient cli;
	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);
	pumpBoth(&srv, &cli, 300);
	BOOST_CHECK(cli.m_gotResult);
	BOOST_CHECK_EQUAL(cli.m_result, eZCom_ConnWrongVersion);
	// The server spawns the connection before the client's version check
	// runs (the version is stamped into MSG_CONNECTION_REPLY and checked on
	// the client side), so m_spawned==1; the client then disconnects and
	// the server's closed callback fires once the disconnect propagates.
	BOOST_CHECK_EQUAL(srv.m_spawned, 1);
	BOOST_CHECK_EQUAL(srv.m_closed, 1);
	srv.Shutdown();
	cli.Shutdown();
}

BOOST_AUTO_TEST_SUITE_END()
