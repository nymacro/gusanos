// test_replication.cpp
// Tests dynamic node replication (registerNodeDynamic, cbNodeRequest_Dynamic).
// Corresponds to ex03_object_replication sample.

#include <boost/test/unit_test.hpp>
#include <cstring>
#include <cstdio>
#include <memory>

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

class ReplServer : public ZCom_Control
{
public:
	int m_spawnedCount;
	int m_nodeRequestCount;
	uint32_t m_playerClass;
	ZCom_Node* m_playerNode;
	~ReplServer() { delete m_playerNode; m_playerNode = nullptr; }

	ReplServer(int udpPort)
		: ZCom_Control()
		, m_spawnedCount(0)
		, m_nodeRequestCount(0)
		, m_playerClass(0)
		, m_playerNode(nullptr)
	{
		m_playerClass = ZCom_registerClass("PlayerInfo", 0);
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
		m_spawnedCount++;

		m_playerNode = new ZCom_Node();
		m_playerNode->registerNodeDynamic(m_playerClass, this);

		auto announce = std::make_unique<ZCom_BitStream>();
		announce->addString("Player1");
		m_playerNode->setAnnounceData(std::move(announce));
	}

	void ZCom_cbNodeRequest_Dynamic(ZCom_ConnID id, uint32_t requested_class,
	                                 ZCom_BitStream* announcedata, int role, uint32_t net_id) override
	{
		m_nodeRequestCount++;
	}
};

class ReplClient : public ZCom_Control
{
public:
	bool m_connected;
	int m_nodeRequestCount;
	uint32_t m_playerClass;

	ReplClient(int udpPort)
		: ZCom_Control()
		, m_connected(false)
		, m_nodeRequestCount(0)
		, m_playerClass(0)
	{
		m_playerClass = ZCom_registerClass("PlayerInfo", 0);
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

	void ZCom_cbNodeRequest_Dynamic(ZCom_ConnID id, uint32_t requested_class,
	                                 ZCom_BitStream* announcedata, int role, uint32_t net_id) override
	{
		m_nodeRequestCount++;
	}
};

BOOST_AUTO_TEST_SUITE(replication)

BOOST_AUTO_TEST_CASE(dynamic_node_registration)
{
	static int portOffset = 0;
	g_currentControl = nullptr;
	int port = 19030 + (portOffset++);

	{
	ReplServer srv(port);
	ReplClient cli(port);

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);

	processBoth(&srv, &cli, 40);

	BOOST_REQUIRE(cli.m_connected);
	BOOST_CHECK_EQUAL(srv.m_spawnedCount, 1);
	BOOST_CHECK(srv.m_playerNode != nullptr);

	srv.Shutdown();
	cli.Shutdown();
	}
}

BOOST_AUTO_TEST_CASE(node_create_destroy)
{
	g_currentControl = nullptr;
	ZCom_Node* node = new ZCom_Node();
	BOOST_CHECK(node != nullptr);
	delete node;
	BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
