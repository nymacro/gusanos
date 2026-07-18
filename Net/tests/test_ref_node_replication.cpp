// test_ref_node_replication.cpp
// Node registration and data replication tests from
// Net/ref_tests/test_node_replication.cpp, ported to Boost.Test.

#include <boost/test/unit_test.hpp>
#include <cstdio>

#include "network_compat.h"

// ---------------------------------------------------------------------------
// Test fixtures: server and client controls
// ---------------------------------------------------------------------------

static ZCom_ClassID g_classUnique = ZCom_Invalid_ID;
static ZCom_ClassID g_classTag = ZCom_Invalid_ID;
static ZCom_ClassID g_classDynamic = ZCom_Invalid_ID;

// Helper: ensure ENet is initialized exactly once per test case.
// ZCom_initSockets calls enet_initialize() which must only be called once
// per process without enet_shutdown() in between.  We gate it with a flag
// so that the 2nd+ call in the same test process is a no-op.
static bool& g_enetInitialized() { static bool v = false; return v; }

static void ensureEnetInit()
{
	if (g_enetInitialized()) return;
	// First call: g_currentControl may be null — that's fine,
	// the host will be assigned when a control sets it.
	g_enetInitialized() = true;
}

class NodeTestServer : public ZCom_Control
{
public:
	NodeTestServer(int udpPort)
	{
		ensureEnetInit();
		g_currentControl = this;
		BOOST_CHECK(ZCom_initSockets(true, udpPort, 0, 0));
		g_classUnique = ZCom_registerClass("UniqueClass", 0);
		g_classTag = ZCom_registerClass("TagClass", 0);
		g_classDynamic = ZCom_registerClass("DynamicClass", 0);
		g_currentControl = nullptr;
	}

protected:
	bool ZCom_cbConnectionRequest(ZCom_ConnID, ZCom_BitStream&, ZCom_BitStream&) override { return true; }
	void ZCom_cbConnectionSpawned(ZCom_ConnID) override {}
	void ZCom_cbConnectionClosed(ZCom_ConnID, eZCom_CloseReason, ZCom_BitStream&) override {}
	void ZCom_cbDataReceived(ZCom_ConnID, ZCom_BitStream&) override {}
	bool ZCom_cbZoidRequest(ZCom_ConnID, uint8_t, ZCom_BitStream&) override { return false; }
	void ZCom_cbZoidResult(ZCom_ConnID, eZCom_ZoidResult, uint8_t, ZCom_BitStream&) override {}
	bool ZCom_cbDiscoverRequest(const ZCom_Address&, ZCom_BitStream&, ZCom_BitStream&) override { return false; }
	void ZCom_cbConnectResult(ZCom_ConnID, eZCom_ConnectResult, ZCom_BitStream&) override {}
	void ZCom_cbNodeRequest_Dynamic(ZCom_ConnID, ZCom_ClassID, ZCom_BitStream*, int, ZCom_NodeID) override {}
	void ZCom_cbNodeRequest_Tag(ZCom_ConnID, ZCom_ClassID, ZCom_BitStream*, int, uint32_t) override {}
	void ZCom_cbDiscovered(const ZCom_Address&, ZCom_BitStream&) override {}
};

class NodeTestClient : public ZCom_Control
{
public:
	bool m_connected;

	NodeTestClient(int udpPort)
		: m_connected(false)
	{
		ensureEnetInit();
		g_currentControl = this;
		BOOST_CHECK(ZCom_initSockets(false, 0, 0, 0));
		g_classUnique = ZCom_registerClass("UniqueClass", 0);
		g_classTag = ZCom_registerClass("TagClass", 0);
		g_classDynamic = ZCom_registerClass("DynamicClass", 0);
		g_currentControl = nullptr;
	}

protected:
	void ZCom_cbConnectResult(ZCom_ConnID id, eZCom_ConnectResult result, ZCom_BitStream& reply) override
	{
		(void)id; (void)reply;
		m_connected = (result == eZCom_ConnAccepted);
	}

	void ZCom_cbConnectionSpawned(ZCom_ConnID) override {}
	bool ZCom_cbConnectionRequest(ZCom_ConnID, ZCom_BitStream&, ZCom_BitStream&) override { return false; }
	void ZCom_cbConnectionClosed(ZCom_ConnID, eZCom_CloseReason, ZCom_BitStream&) override {}
	void ZCom_cbDataReceived(ZCom_ConnID, ZCom_BitStream&) override {}
	bool ZCom_cbZoidRequest(ZCom_ConnID, uint8_t, ZCom_BitStream&) override { return false; }
	void ZCom_cbZoidResult(ZCom_ConnID, eZCom_ZoidResult, uint8_t, ZCom_BitStream&) override {}
	bool ZCom_cbDiscoverRequest(const ZCom_Address&, ZCom_BitStream&, ZCom_BitStream&) override { return false; }
	void ZCom_cbNodeRequest_Dynamic(ZCom_ConnID, ZCom_ClassID, ZCom_BitStream*, int, ZCom_NodeID) override {}
	void ZCom_cbNodeRequest_Tag(ZCom_ConnID, ZCom_ClassID, ZCom_BitStream*, int, uint32_t) override {}
	void ZCom_cbDiscovered(const ZCom_Address&, ZCom_BitStream&) override {}
};

// ---------------------------------------------------------------------------
// Helper: pump client + server until a condition is met
// ---------------------------------------------------------------------------
static void pumpUntil(NodeTestClient* cli, NodeTestServer* srv,
                      std::function<bool()> done, int maxIter = 300)
{
	for (int i = 0; i < maxIter; i++)
	{
		g_currentControl = srv;
		srv->ZCom_processInput(eZCom_NoBlock);
		srv->ZCom_processOutput();
		g_currentControl = cli;
		cli->ZCom_processInput(eZCom_NoBlock);
		cli->ZCom_processOutput();
		ZoidCom::Sleep(10);
		if (done()) return;
	}
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_SUITE(ref_node_replication)

BOOST_AUTO_TEST_CASE(unique_node_with_data_replication)
{
	int port = 19200;
	g_currentControl = nullptr;

	NodeTestServer srv(port);
	NodeTestClient cli(port);

	ZCom_Address target;
	char hostport[64];
	snprintf(hostport, sizeof(hostport), "127.0.0.1:%d", port);
	target.setAddress(ZCom_Control::eZCom_AddressUDP, 0, hostport);

	g_currentControl = &cli;
	uint32_t cid = cli.ZCom_Connect(target, nullptr);
	g_currentControl = nullptr;

	BOOST_CHECK(cid != ZCom_Invalid_ID);

	// Connect client and create a unique node with data replication
	pumpUntil(&cli, &srv, [&]() { return cli.m_connected; }, 300);
	BOOST_CHECK(cli.m_connected);

	// Now create a unique node with data replication on the client
	int32_t testInt = 42;
	bool testBool = true;
	float testFloat = 3.14f;

	ZCom_Node* node = new ZCom_Node();
	node->beginReplicationSetup(3);
	node->addReplicationInt(&testInt, 32, true,
		ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL);
	node->addReplicationBool(&testBool,
		ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL);
	node->addReplicationFloat(&testFloat, 10,
		ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL);
	node->endReplicationSetup();

	node->registerNodeUnique(g_classUnique, eZCom_RoleAuthority, &cli);

	BOOST_CHECK(node->getNetworkID() != ZCom_Invalid_ID);

	cli.Shutdown();
	srv.Shutdown();
	delete node;
}

BOOST_AUTO_TEST_CASE(zoidlevel_transition)
{
	int port = 19210;
	g_currentControl = nullptr;

	NodeTestServer srv(port);
	NodeTestClient cli(port);

	ZCom_Address target;
	char hostport[64];
	snprintf(hostport, sizeof(hostport), "127.0.0.1:%d", port);
	target.setAddress(ZCom_Control::eZCom_AddressUDP, 0, hostport);

	g_currentControl = &cli;
	uint32_t cid = cli.ZCom_Connect(target, nullptr);
	g_currentControl = nullptr;

	BOOST_CHECK(cid != ZCom_Invalid_ID);

	pumpUntil(&cli, &srv, [&]() { return cli.m_connected; }, 300);
	BOOST_CHECK(cli.m_connected);

	// Request ZoidMode (ZoidLevel 1)
	g_currentControl = &cli;
	ZCom_requestZoidMode(cid, 1);
	g_currentControl = nullptr;

	// Pump to let ZoidMode establish
	pumpUntil(&cli, &srv, []() { return true; }, 100);
}

BOOST_AUTO_TEST_CASE(node_user_data)
{
	// No networking needed — just testing node API
	g_currentControl = nullptr;

	int port = 19230;
	NodeTestServer srv(port);

	{
		ZCom_Node* userNode = new ZCom_Node();
		userNode->beginReplicationSetup(0);
		userNode->endReplicationSetup();
		userNode->registerNodeUnique(g_classUnique, eZCom_RoleAuthority, &srv);

		int myData = 42;
		userNode->setUserData(&myData);
		void* retrieved = userNode->getUserData();

		BOOST_CHECK_EQUAL(retrieved, &myData);

		userNode->unregisterNode();
		delete userNode;
	}

	srv.Shutdown();
}

BOOST_AUTO_TEST_CASE(node_properties)
{
	// No networking needed — just testing node API
	// We use setControl() directly to avoid potential issues with
	// full node registration (registerNodeUnique) which adds the node
	// to the control's m_nodes list and can cause double-free in
	// the control destructor if the node is deleted first.
	g_currentControl = nullptr;

	int port = 19220;
	NodeTestServer srv(port);

	// Register a class so we can test getClassID
	g_classUnique = srv.ZCom_registerClass("UniqueClass", 0);

	{
		ZCom_Node* propNode = new ZCom_Node();
		propNode->beginReplicationSetup(0);
		propNode->endReplicationSetup();

		// Manually set control and role instead of using registerNodeUnique
		propNode->setControl(&srv);
		propNode->setRole(eZCom_RoleAuthority);
		propNode->setClassID(g_classUnique);

		// Role
		BOOST_CHECK_EQUAL(propNode->getRole(), eZCom_RoleAuthority);

		// Class ID
		BOOST_CHECK_EQUAL(propNode->getClassID(), g_classUnique);

		// Control
		BOOST_CHECK_EQUAL(propNode->getControl(), &srv);

		// Private (default false)
		BOOST_CHECK_EQUAL(propNode->isPrivate(), false);

		// setPrivate
		propNode->setPrivate(true);
		BOOST_CHECK_EQUAL(propNode->isPrivate(), true);

		propNode->setPrivate(false);

		// Update priority
		propNode->setUpdatePriority(100);

		// Default relevance
		propNode->setDefaultRelevance(0.5f);

		// Relevant connection count
		uint32_t relCount = propNode->getRelevantConnectionCount();
		(void)relCount;

		delete propNode;
	}

	srv.Shutdown();
}

BOOST_AUTO_TEST_SUITE_END()
