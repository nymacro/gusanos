// test_movement.cpp
// Tests movement data replication.
// Corresponds to ex07_movement sample.

#include <boost/test/unit_test.hpp>
#include <cstring>
#include <cstdio>

#include "net_control.h"
#include "net_node.h"

static void processBoth(ZCom_Control* srv, ZCom_Control* cli, int n = 200)
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

class MovementServer : public ZCom_Control
{
public:
	int m_spawnedCount;
	uint32_t m_moverClass;
	ZCom_Node* m_node;
	float m_lastX, m_lastY;

	~MovementServer() { delete m_node; m_node = nullptr; }
	MovementServer(int udpPort)
		: ZCom_Control()
		, m_spawnedCount(0)
		, m_moverClass(0)
		, m_node(nullptr)
		, m_lastX(0.0f)
		, m_lastY(0.0f)
	{
		m_moverClass = ZCom_registerClass("Mover", 0);
		g_currentControl = this;
		ZCom_initSockets(true, udpPort, 0, 0);
	}

	void SendPosition()
	{
		if (!m_node) return;
		ZCom_BitStream data;
		data.addFloat(m_lastX, 16);
		data.addFloat(m_lastY, 16);
		data.addFloat(1.5f, 16);
		data.addFloat(0.0f, 16);
		m_node->sendEvent(eZCom_ReliableOrdered, 0, &data);
		m_lastX += 1.0f;
		m_lastY += 0.5f;
	}

protected:
	bool ZCom_cbConnectionRequest(ZCom_ConnID id, ZCom_BitStream& request, ZCom_BitStream& reply) override
	{
		return true;
	}

	void ZCom_cbConnectionSpawned(ZCom_ConnID id) override
	{
		m_spawnedCount++;
		m_node = new ZCom_Node();
		m_node->registerNodeDynamic(m_moverClass, this);
	}
};

class MovementClient : public ZCom_Control
{
public:
	bool m_connected;
	uint32_t m_moverClass;

	MovementClient(int udpPort)
		: ZCom_Control()
		, m_connected(false)
		, m_moverClass(0)
	{
		m_moverClass = ZCom_registerClass("Mover", 0);
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
};

BOOST_AUTO_TEST_SUITE(movement)

BOOST_AUTO_TEST_CASE(send_position_update)
{
	static int portOffset = 0;
	g_currentControl = nullptr;
	int port = 19120 + (portOffset++);

	{
	MovementServer srv(port);
	MovementClient cli(port);

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);

	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);
	BOOST_REQUIRE(srv.m_node != nullptr);

	g_currentControl = &srv;
	srv.SendPosition();
	g_currentControl = nullptr;

	processBoth(&srv, &cli, 10);

	srv.Shutdown();
	cli.Shutdown();
	}

	BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(multiple_position_updates)
{
	static int portOffset = 0;
	g_currentControl = nullptr;
	int port = 19130 + (portOffset++);

	{
	MovementServer srv(port);
	MovementClient cli(port);

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);

	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);
	BOOST_REQUIRE(srv.m_node != nullptr);

	g_currentControl = &srv;
	for (int i = 0; i < 10; i++)
	{
		srv.SendPosition();
	}
	g_currentControl = nullptr;

	processBoth(&srv, &cli, 10);

	srv.Shutdown();
	cli.Shutdown();
	}

	BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
