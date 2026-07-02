// test_zoidlevel.cpp
// Tests Zoid mode (encryption/authorization) API.
// Corresponds to ex02_zoidlevel sample.

#include <boost/test/unit_test.hpp>
#include <cstring>
#include <cstdio>

#include "net_control.h"

class ZoidServer : public ZCom_Control
{
public:
	int m_connectionSpawnedCount;
	bool m_zoidRequestReceived;
	uint8_t m_lastRequestedLevel;
	bool m_acceptZoid;

	ZoidServer(int udpPort, bool acceptZoid = true)
		: ZCom_Control()
		, m_connectionSpawnedCount(0)
		, m_zoidRequestReceived(false)
		, m_lastRequestedLevel(0)
		, m_acceptZoid(acceptZoid)
	{
		g_currentControl = this;
		ZCom_initSockets(true, udpPort, 0, 0);
	}

protected:
	bool ZCom_cbConnectionRequest(ZCom_ConnID id, ZCom_BitStream& request, ZCom_BitStream& reply) override
	{
		return true;
	}

	void ZCom_cbConnectionSpawned(ZCom_ConnID id) override
	{
		m_connectionSpawnedCount++;
	}

	bool ZCom_cbZoidRequest(ZCom_ConnID id, uint8_t requested_level, ZCom_BitStream& reason) override
	{
		m_zoidRequestReceived = true;
		m_lastRequestedLevel = requested_level;
		return m_acceptZoid;
	}
};

class ZoidClient : public ZCom_Control
{
public:
	bool m_connected;
	uint8_t m_zoidResultLevel;
	eZCom_ZoidResult m_zoidResult;
	bool m_zoidResultReceived;

	ZoidClient()
		: ZCom_Control()
		, m_connected(false)
		, m_zoidResultLevel(0)
		, m_zoidResult(eZCom_ZoidDisabled)
		, m_zoidResultReceived(false)
	{
		g_currentControl = this;
		ZCom_initSockets(false, 0, 0, 0);
	}

	uint32_t ConnectTo(const char* host, int port)
	{
		ZCom_Address addr;
		char hostport[64];
		snprintf(hostport, sizeof(hostport), "%s:%d", host, port);
		addr.setAddress(ZCom_Control::eZCom_AddressUDP, 0, hostport);
		return ZCom_Connect(addr, nullptr);
	}

protected:
	void ZCom_cbConnectResult(ZCom_ConnID id, eZCom_ConnectResult result, ZCom_BitStream& reply) override
	{
		m_connected = (result == eZCom_ConnAccepted);
	}

	void ZCom_cbZoidResult(ZCom_ConnID id, eZCom_ZoidResult result, uint8_t new_level, ZCom_BitStream& reason) override
	{
		m_zoidResultReceived = true;
		m_zoidResult = result;
		m_zoidResultLevel = new_level;
	}
};

static void processBoth(ZCom_Control* srv, ZCom_Control* cli, int n)
{
	for (int i = 0; i < n; i++)
	{
		g_currentControl = srv;
		srv->ZCom_processInput(eZCom_NoBlock);
		srv->ZCom_processOutput();
		g_currentControl = cli;
		cli->ZCom_processInput(eZCom_NoBlock);
		cli->ZCom_processOutput();
	}
	g_currentControl = nullptr;
}

BOOST_AUTO_TEST_SUITE(zoidlevel)

BOOST_AUTO_TEST_CASE(zoid_request_accepted)
{
	static int portOffset = 0;
	g_currentControl = nullptr;
	int port = 19010 + (portOffset++);

	{
	ZoidServer srv(port, true);
	ZoidClient cli;

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);

	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);

	// ZCom_requestZoidMode is a global; set g_currentControl to the client
	g_currentControl = &cli;
	ZCom_requestZoidMode(cid, 1);
	g_currentControl = nullptr;

	processBoth(&srv, &cli, 20);

	BOOST_CHECK(srv.m_zoidRequestReceived);
	BOOST_CHECK_EQUAL(srv.m_lastRequestedLevel, 1);
	BOOST_CHECK(cli.m_zoidResultReceived);
	BOOST_CHECK(cli.m_zoidResult == eZCom_ZoidEnabled);

	srv.Shutdown();
	cli.Shutdown();
	}
}

BOOST_AUTO_TEST_CASE(zoid_request_denied)
{
	static int portOffset = 0;
	g_currentControl = nullptr;
	int port = 19020 + (portOffset++);

	{
	ZoidServer srv(port, false);
	ZoidClient cli;

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);

	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);

	g_currentControl = &cli;
	ZCom_requestZoidMode(cid, 2);
	g_currentControl = nullptr;

	processBoth(&srv, &cli, 20);

	BOOST_CHECK(srv.m_zoidRequestReceived);
	BOOST_CHECK(cli.m_zoidResultReceived);
	BOOST_CHECK(cli.m_zoidResult == eZCom_ZoidDisabled);

	srv.Shutdown();
	cli.Shutdown();
	}
}

BOOST_AUTO_TEST_SUITE_END()
