// test_deletion.cpp
// Tests node deletion via node destructor.
// Corresponds to ex04_object_deletion sample.

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

class DeletionServer : public ZCom_Control
{
public:
	int m_spawnedCount;
	uint32_t m_playerClass;

	DeletionServer(int udpPort)
		: ZCom_Control()
		, m_spawnedCount(0)
		, m_playerClass(0)
	{
		m_playerClass = ZCom_registerClass("Deletable", 0);
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
	}
};

class DeletionClient : public ZCom_Control
{
public:
	bool m_connected;
	uint32_t m_playerClass;

	DeletionClient(int udpPort)
		: ZCom_Control()
		, m_connected(false)
		, m_playerClass(0)
	{
		m_playerClass = ZCom_registerClass("Deletable", 0);
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

BOOST_AUTO_TEST_SUITE(deletion)

BOOST_AUTO_TEST_CASE(create_destroy_node)
{
	g_currentControl = nullptr;
	ZCom_Node* node = new ZCom_Node();
	delete node;
	BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(server_client_connect_and_delete_node)
{
	static int portOffset = 0;
	g_currentControl = nullptr;
	int port = 19050 + (portOffset++);

	{
	DeletionServer srv(port);
	DeletionClient cli(port);

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);

	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);
	BOOST_REQUIRE_EQUAL(srv.m_spawnedCount, 1);

	srv.Shutdown();
	cli.Shutdown();
	}

	BOOST_CHECK(true);
}

// Regression: a node registered with a control must survive the control's
// Shutdown() being called before the node is deleted. Mirrors the server-exit
// crash where Network::disconnect completion does `delete m_control` while
// BasePlayer::m_node / NetWorm::m_node still hold an m_control back-pointer;
// the later game.unload -> ~ZCom_Node -> unregisterNode -> removeNode would
// dereference the freed control and crash inside m_pendingRemove.insert.
BOOST_AUTO_TEST_CASE(shutdown_then_delete_node)
{
	g_currentControl = nullptr;
	int port = 19150;

	DeletionServer srv(port);
	srv.m_playerClass = srv.ZCom_registerClass("Deletable", 0);

	ZCom_Node* node = new ZCom_Node();
	node->registerNodeDynamic(srv.m_playerClass, &srv);
	BOOST_REQUIRE(node->getControl() == &srv);

	// Shutdown nulls node back-pointers (and frees the host). Without that,
	// the delete below dereferences the about-to-be-freed srv.
	srv.Shutdown();
	BOOST_REQUIRE(node->getControl() == nullptr);

	delete node; // must not crash (unregisterNode is a no-op when m_control==null)
	BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
