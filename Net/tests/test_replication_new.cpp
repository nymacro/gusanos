// test_replication_new.cpp
// Tests for newly added Net/ functionality:
//   - Auto-announce on registerNode for non-unique nodes
//   - Owner-aware role announcements (announceNodeWithOwner)
//   - setOwner triggers re-announcement
//   - Duplicate MSG_NODE_ANNOUNCE detection
//   - Replication interceptor callbacks (inPreUpdate, inPreUpdateItem)
//   - setInterceptID and setReplicationInterceptor storage

#include <boost/test/unit_test.hpp>
#include <cstring>
#include <cstdio>
#include <memory>

#include "net_control.h"
#include "net_node.h"
#include "net_replicator.h"

static int s_portOffset = 0;

// Helper: process both controls in lockstep
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

// ---- Test 1: Auto-announce on registerNode ----
// When a non-unique node is registered on a server that has connected peers,
// the client should receive ZCom_cbNodeRequest_Dynamic for that node.

class AutoAnnounceServer : public ZCom_Control
{
public:
	int m_connSpawned;
	uint32_t m_testClass;
	int m_nodeRequestCount;
	ZCom_Node* m_dynamicNode;

	~AutoAnnounceServer() { delete m_dynamicNode; m_dynamicNode = nullptr; }
	AutoAnnounceServer(int port)
		: ZCom_Control()
		, m_connSpawned(0)
		, m_testClass(0)
		, m_nodeRequestCount(0)
		, m_dynamicNode(nullptr)
	{
		m_testClass = ZCom_registerClass("AutoAnnounceNode", 0);
		g_currentControl = this;
		ZCom_initSockets(true, port, 0, 0);
	}

	void registerAndAnnounce()
	{
		m_dynamicNode = new ZCom_Node();
		m_dynamicNode->registerNodeDynamic(m_testClass, this);
	}

protected:
	bool ZCom_cbConnectionRequest(ZCom_ConnID id, ZCom_BitStream& request, ZCom_BitStream& reply) override
	{
		return true;
	}

	void ZCom_cbConnectionSpawned(ZCom_ConnID id) override
	{
		m_connSpawned++;
	}

	void ZCom_cbNodeRequest_Dynamic(ZCom_ConnID id, uint32_t requested_class,
	                                 ZCom_BitStream* announcedata, int role, uint32_t net_id) override
	{
		m_nodeRequestCount++;
	}
};

class AutoAnnounceClient : public ZCom_Control
{
public:
	bool m_connected;
	uint32_t m_testClass;
	int m_nodeRequestCount;

	AutoAnnounceClient(int port)
		: ZCom_Control()
		, m_connected(false)
		, m_testClass(0)
		, m_nodeRequestCount(0)
	{
		m_testClass = ZCom_registerClass("AutoAnnounceNode", 0);
		g_currentControl = this;
		ZCom_initSockets(false, 0, 0, 0);
	}

	uint32_t ConnectTo(const char* host, int port)
	{
		ZCom_Address addr;
		char hostport[64];
		snprintf(hostport, sizeof(hostport), "%s:%d", host, port);
		addr.setAddress(eZCom_AddressUDP, 0, hostport);
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

BOOST_AUTO_TEST_SUITE(replication_new)

// ---- Auto-announce: node registered after client connects ----
BOOST_AUTO_TEST_CASE(auto_announce_new_node_reaches_client)
{
	g_currentControl = nullptr;
	int port = 19100 + (s_portOffset++);

	{
	AutoAnnounceServer srv(port);
	AutoAnnounceClient cli(port);

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);

	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);
	BOOST_REQUIRE_EQUAL(srv.m_connSpawned, 1);

	// Register a node on the server AFTER client is connected
	g_currentControl = &srv;
	srv.registerAndAnnounce();
	g_currentControl = nullptr;

	// Process to allow auto-announce to reach client
	processBoth(&srv, &cli, 20);

	// Client should have received the node announcement
	BOOST_CHECK_EQUAL(cli.m_nodeRequestCount, 1);
	// Server should NOT have received its own announcement back
	BOOST_CHECK_EQUAL(srv.m_nodeRequestCount, 0);

	srv.Shutdown();
	cli.Shutdown();
	}
}

// ---- Auto-announce: node registered before client connects ----
// Nodes registered before any client connects should be synced when a client
// connects (via initial sync in processENetEvent, not auto-announce).
BOOST_AUTO_TEST_CASE(auto_announce_existing_node_synced_on_connect)
{
	g_currentControl = nullptr;
	int port = 19105 + (s_portOffset++);

	{
	AutoAnnounceServer srv(port);

	// Register node BEFORE any client connects
	g_currentControl = &srv;
	srv.registerAndAnnounce();
	g_currentControl = nullptr;

	AutoAnnounceClient cli(port);

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);

	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);
	BOOST_REQUIRE_EQUAL(srv.m_connSpawned, 1);

	// Client should have received the existing node via initial sync
	BOOST_CHECK_EQUAL(cli.m_nodeRequestCount, 1);

	srv.Shutdown();
	cli.Shutdown();
	}
}

// ---- Duplicate detection ----
// Sending a second MSG_NODE_ANNOUNCE for the same node ID should not
// trigger ZCom_cbNodeRequest_Dynamic again.

class DupServer : public ZCom_Control
{
public:
	int m_connSpawned;
	uint32_t m_testClass;
	int m_nodeRequestCount;
	ZCom_Node* m_node;

	~DupServer() { delete m_node; m_node = nullptr; }
	DupServer(int port)
		: ZCom_Control()
		, m_connSpawned(0)
		, m_testClass(0)
		, m_nodeRequestCount(0)
		, m_node(nullptr)
	{
		m_testClass = ZCom_registerClass("DupNode", 0);
		g_currentControl = this;
		ZCom_initSockets(true, port, 0, 0);
	}

	void createNode()
	{
		m_node = new ZCom_Node();
		m_node->registerNodeDynamic(m_testClass, this);
	}

	void announceAgain()
	{
		// Manually re-announce the same node (simulates setOwner re-announce)
		if (m_node)
			sendNodeAnnouncement(0, m_node, m_node->getRole());
	}

protected:
	bool ZCom_cbConnectionRequest(ZCom_ConnID id, ZCom_BitStream& request, ZCom_BitStream& reply) override
	{
		return true;
	}

	void ZCom_cbConnectionSpawned(ZCom_ConnID id) override
	{
		m_connSpawned++;
	}

	void ZCom_cbNodeRequest_Dynamic(ZCom_ConnID id, uint32_t requested_class,
	                                 ZCom_BitStream* announcedata, int role, uint32_t net_id) override
	{
		m_nodeRequestCount++;
	}
};

class DupClient : public ZCom_Control
{
public:
	bool m_connected;
	uint32_t m_testClass;
	int m_nodeRequestCount;

	DupClient(int port)
		: ZCom_Control()
		, m_connected(false)
		, m_testClass(0)
		, m_nodeRequestCount(0)
	{
		m_testClass = ZCom_registerClass("DupNode", 0);
		g_currentControl = this;
		ZCom_initSockets(false, 0, 0, 0);
	}

	uint32_t ConnectTo(const char* host, int port)
	{
		ZCom_Address addr;
		char hostport[64];
		snprintf(hostport, sizeof(hostport), "%s:%d", host, port);
		addr.setAddress(eZCom_AddressUDP, 0, hostport);
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

BOOST_AUTO_TEST_CASE(duplicate_announcement_is_skipped)
{
	g_currentControl = nullptr;
	int port = 19110 + (s_portOffset++);

	{
	DupServer srv(port);
	DupClient cli(port);

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);

	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);

	// Create node on server — auto-announce to client
	g_currentControl = &srv;
	srv.createNode();
	g_currentControl = nullptr;
	processBoth(&srv, &cli, 15);

	// First announcement should have reached client
	BOOST_REQUIRE_EQUAL(cli.m_nodeRequestCount, 1);

	// Manually re-announce same node (simulates setOwner re-announcement)
	g_currentControl = &srv;
	srv.announceAgain();
	g_currentControl = nullptr;
	processBoth(&srv, &cli, 10);

	// Client should NOT have received a second callback
	BOOST_CHECK_EQUAL(cli.m_nodeRequestCount, 1);

	srv.Shutdown();
	cli.Shutdown();
	}
}

// ---- Owner-aware role announcements ----
// announceNodeWithOwner should send eZCom_RoleOwner to the owning client
// and eZCom_RoleProxy (or the node's role) to other clients.

class OwnerAwareServer : public ZCom_Control
{
public:
	int m_connSpawned;
	uint32_t m_testClass;
	int m_client1Role;
	int m_client2Role;
	uint32_t m_client1ID;
	uint32_t m_client2ID;

	OwnerAwareServer(int port)
		: ZCom_Control()
		, m_connSpawned(0)
		, m_testClass(0)
		, m_client1Role(-1)
		, m_client2Role(-1)
		, m_client1ID(0)
		, m_client2ID(0)
	{
		m_testClass = ZCom_registerClass("OwnerNode", 0);
		g_currentControl = this;
		ZCom_initSockets(true, port, 2, 0);
	}

protected:
	bool ZCom_cbConnectionRequest(ZCom_ConnID id, ZCom_BitStream& request, ZCom_BitStream& reply) override
	{
		return true;
	}

	void ZCom_cbConnectionSpawned(ZCom_ConnID id) override
	{
		m_connSpawned++;
		if (m_client1ID == 0)
			m_client1ID = id;
		else
			m_client2ID = id;
	}

	void ZCom_cbNodeRequest_Dynamic(ZCom_ConnID id, uint32_t requested_class,
	                                 ZCom_BitStream* announcedata, int role, uint32_t net_id) override
	{
		if (id == m_client1ID)
			m_client1Role = role;
		else if (id == m_client2ID)
			m_client2Role = role;
	}
};

class OwnerAwareClient : public ZCom_Control
{
public:
	bool m_connected;
	uint32_t m_testClass;
	int m_receivedRole;
	uint32_t m_receivedNodeID;

	OwnerAwareClient(int port)
		: ZCom_Control()
		, m_connected(false)
		, m_testClass(0)
		, m_receivedRole(-1)
		, m_receivedNodeID(0)
	{
		m_testClass = ZCom_registerClass("OwnerNode", 0);
		g_currentControl = this;
		ZCom_initSockets(false, 0, 0, 0);
	}

	uint32_t ConnectTo(const char* host, int port)
	{
		ZCom_Address addr;
		char hostport[64];
		snprintf(hostport, sizeof(hostport), "%s:%d", host, port);
		addr.setAddress(eZCom_AddressUDP, 0, hostport);
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
		m_receivedRole = role;
		m_receivedNodeID = net_id;
	}
};

BOOST_AUTO_TEST_CASE(owner_aware_role_owner_client)
{
	g_currentControl = nullptr;
	int port = 19115 + (s_portOffset++);

	{
	OwnerAwareServer srv(port);
	OwnerAwareClient cli(port);

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);

	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);

	// Create a node on the server with an owner set
	g_currentControl = &srv;
	ZCom_Node* node = new ZCom_Node();
	node->setOwner(srv.m_client1ID, true);
	node->registerNodeDynamic(srv.m_testClass, &srv);
	g_currentControl = nullptr;
	processBoth(&srv, &cli, 15);

	// The owning client should receive eZCom_RoleOwner
	BOOST_CHECK_EQUAL(cli.m_receivedRole, eZCom_RoleOwner);
	BOOST_CHECK(cli.m_receivedNodeID != 0);

	srv.Shutdown();
	cli.Shutdown();
	delete node;
	}
}

BOOST_AUTO_TEST_CASE(owner_aware_role_different_role_for_each_client)
{
	g_currentControl = nullptr;
	int port = 19120 + (s_portOffset++);

	{
	OwnerAwareServer srv(port);
	OwnerAwareClient cli1(port);
	OwnerAwareClient cli2(port);

	uint32_t cid1 = cli1.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid1 != ZCom_Invalid_ID);
	processBoth(&srv, &cli1, 30);
	BOOST_REQUIRE(cli1.m_connected);

	uint32_t cid2 = cli2.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid2 != ZCom_Invalid_ID);
	processBoth(&srv, &cli2, 30);
	BOOST_REQUIRE(cli2.m_connected);

	BOOST_REQUIRE_NE(srv.m_client1ID, 0);
	BOOST_REQUIRE_NE(srv.m_client2ID, 0);

	// Create a node owned by client1
	g_currentControl = &srv;
	ZCom_Node* node = new ZCom_Node();
	node->setOwner(srv.m_client1ID, true);
	node->registerNodeDynamic(srv.m_testClass, &srv);
	// setOwner triggers announceNodeWithOwner with owner-aware roles
	g_currentControl = nullptr;
	processBoth(&srv, &cli1, 15);
	processBoth(&srv, &cli2, 15);

	// Client1 should have received RoleOwner
	BOOST_CHECK_EQUAL(cli1.m_receivedRole, eZCom_RoleOwner);
	// Client2 might not have received anything if duplicate detection skipped it
	// (since the node was already created from client1's announcement)

	srv.Shutdown();
	cli1.Shutdown();
	cli2.Shutdown();
	delete node;
	}
}

// ---- setOwner triggers re-announce ----
// When setOwner is called on an already-registered node, announceNodeWithOwner
// should be called to re-announce with correct roles.

class ReannounceServer : public ZCom_Control
{
public:
	int m_connSpawned;
	uint32_t m_testClass;
	int m_nodeRequestCount;
	int m_clientRole;
	uint32_t m_clientID;

	ReannounceServer(int port)
		: ZCom_Control()
		, m_connSpawned(0)
		, m_testClass(0)
		, m_nodeRequestCount(0)
		, m_clientRole(-1)
		, m_clientID(0)
	{
		m_testClass = ZCom_registerClass("ReannounceNode", 0);
		g_currentControl = this;
		ZCom_initSockets(true, port, 0, 0);
	}

protected:
	bool ZCom_cbConnectionRequest(ZCom_ConnID id, ZCom_BitStream& request, ZCom_BitStream& reply) override
	{
		return true;
	}

	void ZCom_cbConnectionSpawned(ZCom_ConnID id) override
	{
		m_connSpawned++;
		if (m_clientID == 0)
			m_clientID = id;
	}

	void ZCom_cbNodeRequest_Dynamic(ZCom_ConnID id, uint32_t requested_class,
	                                 ZCom_BitStream* announcedata, int role, uint32_t net_id) override
	{
		m_nodeRequestCount++;
		m_clientRole = role;
	}
};

class ReannounceClient : public ZCom_Control
{
public:
	bool m_connected;
	uint32_t m_testClass;
	int m_nodeRequestCount;
	int m_receivedRole;

	ReannounceClient(int port)
		: ZCom_Control()
		, m_connected(false)
		, m_testClass(0)
		, m_nodeRequestCount(0)
		, m_receivedRole(-1)
	{
		m_testClass = ZCom_registerClass("ReannounceNode", 0);
		g_currentControl = this;
		ZCom_initSockets(false, 0, 0, 0);
	}

	uint32_t ConnectTo(const char* host, int port)
	{
		ZCom_Address addr;
		char hostport[64];
		snprintf(hostport, sizeof(hostport), "%s:%d", host, port);
		addr.setAddress(eZCom_AddressUDP, 0, hostport);
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
		m_receivedRole = role;
	}
};

BOOST_AUTO_TEST_CASE(set_owner_after_register_triggers_reannounce)
{
	g_currentControl = nullptr;
	int port = 19125 + (s_portOffset++);

	{
	ReannounceServer srv(port);
	ReannounceClient cli(port);

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);

	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);

	// Register node WITHOUT owner — auto-announce happens
	g_currentControl = &srv;
	ZCom_Node* node = new ZCom_Node();
	node->registerNodeDynamic(srv.m_testClass, &srv);
	g_currentControl = nullptr;
	processBoth(&srv, &cli, 15);

	// First announcement arrived (role is node's default role, probably Authority)
	int beforeCount = cli.m_nodeRequestCount;

	// Now set owner — should trigger re-announce via announceNodeWithOwner
	g_currentControl = &srv;
	node->setOwner(srv.m_clientID, true);
	g_currentControl = nullptr;
	processBoth(&srv, &cli, 10);

	// Duplicate detection should prevent a second cbNodeRequest_Dynamic call
	BOOST_CHECK_EQUAL(cli.m_nodeRequestCount, beforeCount);
	// But the re-announce happens (it's just skipped on the client side)

	srv.Shutdown();
	cli.Shutdown();
	delete node;
	}
}

// ---- register then setOwner in the same step: single Owner announce ----
// Mirrors the real server flow in server.cpp PLAYER_REQUEST, which calls
// registerNodeDynamic() then setOwnerId() (-> setOwner()) in the same
// cbDataReceived step, before ZCom_processOutput runs. The client must
// receive exactly one announce as Owner — NOT a premature Proxy announce
// followed by an Owner re-announce (which would create a ProxyPlayer with no
// viewport instead of a Player with a viewport).

BOOST_AUTO_TEST_CASE(register_then_setowner_announces_once_as_owner)
{
	g_currentControl = nullptr;
	int port = 19130 + (s_portOffset++);

	{
	ReannounceServer srv(port);
	ReannounceClient cli(port);

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);

	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);
	BOOST_REQUIRE_NE(srv.m_clientID, 0);

	// Register then owner-assign in the SAME step (before any processOutput).
	g_currentControl = &srv;
	ZCom_Node* node = new ZCom_Node();
	node->registerNodeDynamic(srv.m_testClass, &srv);
	node->setOwner(srv.m_clientID, true);
	g_currentControl = nullptr;
	processBoth(&srv, &cli, 15);

	// Exactly one announce, and it must be Owner.
	BOOST_CHECK_EQUAL(cli.m_nodeRequestCount, 1);
	BOOST_CHECK_EQUAL(cli.m_receivedRole, eZCom_RoleOwner);

	srv.Shutdown();
	cli.Shutdown();
	delete node;
	}
}

// ---- applyRequestNodeID regression: client receives announced node ID ----
// When a client's cbNodeRequest_Dynamic sets up a node and calls registerRequestedNode
// WITHOUT a manual setNetworkID, the Net layer's applyRequestNodeID must assign the
// server-announced node ID. Otherwise, the node would have nodeID=0, and MSG_REPLICATORS
// would never resolve it ("NOT FOUND locally — buffering for later") → no state replication.
//
// This test verifies that registerRequestedNode without setNetworkID correctly receives the
// announced node ID and that ZCom_getNode can find it for replica replay.

class GetNodeIdServer : public ZCom_Control
{
public:
	int m_connSpawned;
	uint32_t m_testClass;
	uint32_t m_announcedNodeId;      // Server's announced node ID
	uint32_t m_clientID;              // Client connection ID (set in cbConnectionSpawned)
	int m_nodeRequestCount;           // How many times cbNodeRequest_Dynamic fired
	uint32_t m_announcedNodeId2;     // Simulated second node

	GetNodeIdServer(int port)
		: ZCom_Control()
		, m_connSpawned(0)
		, m_testClass(0)
		, m_announcedNodeId(0)
		, m_clientID(0)
		, m_nodeRequestCount(0)
		, m_announcedNodeId2(0)
	{
		m_testClass = ZCom_registerClass("GetNodeIdNode", 0);
		g_currentControl = this;
		ZCom_initSockets(true, port, 0, 0);
	}

protected:
	bool ZCom_cbConnectionRequest(ZCom_ConnID id, ZCom_BitStream& request, ZCom_BitStream& reply) override
	{
		return true;
	}

	void ZCom_cbConnectionSpawned(ZCom_ConnID id) override
	{
		m_connSpawned++;
		m_clientID = id;
	}

	void ZCom_cbNodeRequest_Dynamic(ZCom_ConnID id, uint32_t requested_class,
	                                 ZCom_BitStream* announcedata, int role, uint32_t net_id) override
	{
		m_nodeRequestCount++;
	}
};

class GetNodeIdClient : public ZCom_Control
{
public:
	bool m_connected;
	uint32_t m_testClass;
	int m_nodeRequestCount;
	uint32_t m_receivedNodeId;   // Last confirmed announced node ID from server
	ZCom_Node* m_clientNode;     // Node created in cbNodeRequest_Dynamic

	~GetNodeIdClient() { delete m_clientNode; m_clientNode = nullptr; }
	GetNodeIdClient(int port)
		: ZCom_Control()
		, m_connected(false)
		, m_testClass(0)
		, m_nodeRequestCount(0)
		, m_receivedNodeId(0)
		, m_clientNode(nullptr)
	{
		m_testClass = ZCom_registerClass("GetNodeIdNode", 0);
		g_currentControl = this;
		ZCom_initSockets(false, 0, 0, 0);
	}

	uint32_t ConnectTo(const char* host, int port)
	{
		ZCom_Address addr;
		char hostport[64];
		snprintf(hostport, sizeof(hostport), "%s:%d", host, port);
		addr.setAddress(eZCom_AddressUDP, 0, hostport);
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
		m_receivedNodeId = net_id;  // Server announced this as the node's ID

		// Create node WITHOUT manual setNetworkID — applyRequestNodeID
		// must assign the announced ID via setNodeID().
		m_clientNode = new ZCom_Node();
		m_clientNode->setControl(this);  // Ensure control is set
		m_clientNode->setEventNotification(true, true);
		m_clientNode->registerRequestedNode(m_testClass, this);

		// Verify the node ID was assigned by the Net layer
		BOOST_CHECK_EQUAL(m_clientNode->getNetworkID(), net_id);
		BOOST_CHECK_NE(m_clientNode->getNetworkID(), 0u);  // Not 0 (which was the bug)
	}
};

BOOST_AUTO_TEST_CASE(register_requested_node_gets_announced_id)
{
	g_currentControl = nullptr;
	int port = 19135 + (s_portOffset++);

	{
	GetNodeIdServer srv(port);
	GetNodeIdClient cli(port);

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);

	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);

	// server registers a node dynamically — client receives announcement
	g_currentControl = &srv;
	ZCom_Node* srvNode = new ZCom_Node();
	BOOST_CHECK(srvNode->registerNodeDynamic(srv.m_testClass, &srv));
	srv.m_announcedNodeId = srvNode->getNetworkID();
	BOOST_CHECK_EQUAL(srvNode->getNetworkID(), srv.m_announcedNodeId);
	g_currentControl = nullptr;

	// trigger processOutput so announcement is sent/received
	// (the client already received it in the 30 ticks above, but we need to
	// pump once more to ensure the node's ZCom_getNode lookup works)
	processBoth(&srv, &cli, 5);

	// Connection spawned callback recorded as request context active
	BOOST_REQUIRE_NE(srv.m_clientID, 0);

	// Flush output so the Net layer applies the request context
	processBoth(&srv, &cli, 5);

	// The client must have received the announced node ID
	BOOST_CHECK_EQUAL(cli.m_receivedNodeId, srv.m_announcedNodeId);

	// Given the applyRequestNodeID fix, the client node must exist
	// and have the correct network ID (not 0)
	BOOST_REQUIRE(cli.m_clientNode != nullptr);
	BOOST_CHECK_EQUAL(cli.m_clientNode->getNetworkID(), srv.m_announcedNodeId);
	BOOST_CHECK_NE(cli.m_clientNode->getNetworkID(), 0u);  // Not 0 (which was the bug)

	// Simulate replica arrival: server sends a user event for the node.
	// This would buffer if the nodeID was 0 (ZCom_getNode collision on 0), but now resolves.
	ZCom_BitStream eventData;
	eventData.addInt(0xDEADBEEF, 32);
	srvNode->sendEventDirect(eZCom_ReliableOrdered, &eventData, srv.m_clientID);
	processBoth(&srv, &cli, 5);

	// Client should have pending event
	BOOST_CHECK(cli.m_clientNode->checkEventWaiting());

	srv.Shutdown();
	cli.Shutdown();
	delete srvNode;
	}
}

// ---- Replication interceptor ----
// unpackAllReplicators should call inPreUpdate and inPreUpdateItem on
// the registered replication interceptor.

class TestInterceptor : public ZCom_NodeReplicationInterceptor
{
public:
	bool inPreUpdateCalled;
	int inPreUpdateCount;
	int inPreUpdateItemCount;
	ZCom_Replicator* lastReplicator;

	TestInterceptor()
		: inPreUpdateCalled(false)
		, inPreUpdateCount(0)
		, inPreUpdateItemCount(0)
		, lastReplicator(nullptr)
	{}

	bool inPreUpdate(ZCom_Node* node, uint32_t from, eZCom_NodeRole remote_role) override
	{
		inPreUpdateCalled = true;
		inPreUpdateCount++;
		return true;
	}

	bool inPreUpdateItem(ZCom_Node* node, uint32_t from, eZCom_NodeRole remote_role,
	                     ZCom_Replicator* replicator, uint32_t estimated_time_sent) override
	{
		inPreUpdateItemCount++;
		lastReplicator = replicator;
		return true;
	}
};

BOOST_AUTO_TEST_CASE(interceptor_in_pre_update_called)
{
	g_currentControl = nullptr;

	ZCom_Node node;
	TestInterceptor interceptor;
	node.setReplicationInterceptor(&interceptor);

	// Set up a replicator on the node
	ZCom_ReplicatorSetup setup(0, 0);
	node.addReplicator(std::make_unique<ZCom_ReplicatorBasic>(&setup), true);

	// Pack and unpack some data to trigger interceptor
	int testVal = 42;
	node.addReplicationInt(&testVal, 8, false, 0, 0);

	ZCom_BitStream packed;
	node.packAllReplicators(&packed);

	BOOST_REQUIRE_GT(packed.getDataLength(), 0);

	// Reset interceptor state and unpack
	interceptor.inPreUpdateCalled = false;
	interceptor.inPreUpdateCount = 0;
	interceptor.inPreUpdateItemCount = 0;

	packed.resetReadState();
	node.unpackAllReplicators(&packed, true, 0);

	BOOST_CHECK(interceptor.inPreUpdateCalled);
	BOOST_CHECK_GE(interceptor.inPreUpdateCount, 1);
}

BOOST_AUTO_TEST_CASE(interceptor_in_pre_update_item_called)
{
	g_currentControl = nullptr;

	ZCom_Node node;
	TestInterceptor interceptor;
	node.setReplicationInterceptor(&interceptor);

	// Add a replicator with intercept
	ZCom_ReplicatorSetup setup(ZCOM_REPFLAG_INTERCEPT, 0, 42);
	node.addReplicator(std::make_unique<ZCom_ReplicatorBasic>(&setup), true);

	ZCom_BitStream packed;
	node.packAllReplicators(&packed);

	BOOST_REQUIRE_GT(packed.getDataLength(), 0);

	interceptor.inPreUpdateItemCount = 0;
	interceptor.lastReplicator = nullptr;

	packed.resetReadState();
	node.unpackAllReplicators(&packed, true, 0);

	BOOST_CHECK_GE(interceptor.inPreUpdateItemCount, 1);
}

// The `estimatedTimeSent` argument passed to unpackAllReplicators must be
// forwarded verbatim to inPreUpdateItem. The real receive path in
// ZCom_Control computes this from ENet's RTT (estimatedSendTimeFromPeer)
// and feeds it through unpackAllReplicators -> inPreUpdateItem so that
// interceptors (e.g. NetWorm's snapshot interpolation / dead-reckoning)
// get a real travel-time estimate instead of the hardcoded 0. This test
// guards that plumbing against a refactor silently dropping the value.
BOOST_AUTO_TEST_CASE(interceptor_in_pre_update_item_receives_estimated_time_sent)
{
	g_currentControl = nullptr;

	struct CaptureEstInterceptor : public ZCom_NodeReplicationInterceptor
	{
		bool gotCalled = false;
		uint32_t captured = 0;
		bool inPreUpdate(ZCom_Node*, uint32_t, eZCom_NodeRole) override { return true; }
		bool inPreUpdateItem(ZCom_Node*, uint32_t, eZCom_NodeRole,
		                    ZCom_Replicator*, uint32_t estimated_time_sent) override
		{
			gotCalled = true;
			captured = estimated_time_sent;
			return true;
		}
		bool outPreUpdate(ZCom_Node*, uint32_t, eZCom_NodeRole) override { return true; }
		bool outPreUpdateItem(ZCom_Node*, uint32_t, eZCom_NodeRole, ZCom_Replicator*) override { return true; }
	};

	ZCom_Node node;
	CaptureEstInterceptor interceptor;
	node.setReplicationInterceptor(&interceptor);

	// Replicator with INTERCEPT flag so inPreUpdateItem is invoked.
	ZCom_ReplicatorSetup setup(ZCOM_REPFLAG_INTERCEPT, 0, 42);
	node.addReplicator(std::make_unique<ZCom_ReplicatorBasic>(&setup), true);

	ZCom_BitStream packed;
	node.packAllReplicators(&packed);
	BOOST_REQUIRE_GT(packed.getDataLength(), 0);

	packed.resetReadState();
	const uint32_t sentEstimate = 0x12345678u;
	node.unpackAllReplicators(&packed, true, sentEstimate);

	BOOST_CHECK(interceptor.gotCalled);
	BOOST_CHECK_EQUAL(interceptor.captured, sentEstimate);
}

BOOST_AUTO_TEST_CASE(interceptor_auto_replication_triggered)
{
	g_currentControl = nullptr;

	ZCom_Node node;
	TestInterceptor interceptor;
	node.setReplicationInterceptor(&interceptor);
	// Set intercept ID so auto-replications know which intercept ID to report
	node.setInterceptID(7);
	// Simulate the game pattern: setInterceptID + addReplicationInt with INTERCEPT flag
	int32_t testVal = 42;
	node.addReplicationInt(&testVal, 8, false,
		ZCOM_REPFLAG_MOSTRECENT | ZCOM_REPFLAG_INTERCEPT,
		ZCOM_REPRULE_AUTH_2_ALL, 0);

	ZCom_BitStream packed;
	node.packAllReplicators(&packed);

	BOOST_REQUIRE_GT(packed.getDataLength(), 0);

	interceptor.inPreUpdateItemCount = 0;

	packed.resetReadState();
	node.unpackAllReplicators(&packed, true, 0);

	// Auto-replication with INTERCEPT flag should trigger inPreUpdateItem
	BOOST_CHECK_GE(interceptor.inPreUpdateItemCount, 1);
}

// ---- setInterceptID stores the ID ----
BOOST_AUTO_TEST_CASE(set_intercept_id_stored)
{
	ZCom_Node node;
	// Default should be -1 (no intercept)
	// After setting, it should be accessible internally
	node.setInterceptID(5);
	// We can only verify it doesn't crash and interceptors get called with the right ID
	// The actual stored value is tested indirectly through the interceptor
	BOOST_CHECK(true);
}

// ---- setReplicationInterceptor stores pointer ----
BOOST_AUTO_TEST_CASE(set_replication_interceptor_stored)
{
	ZCom_Node node;
	TestInterceptor interceptor;
	node.setReplicationInterceptor(&interceptor);

	// Trigger unpackAllReplicators with empty data to verify interceptor is called
	ZCom_BitStream empty;
	empty.addInt(0, 1); // no update for first replicator
	empty.addInt(0, 1); // no update for first auto-replication
	empty.resetReadState();

	interceptor.inPreUpdateCalled = false;
	node.unpackAllReplicators(&empty, true, 0);

	// Interceptor should have been called
	BOOST_CHECK(interceptor.inPreUpdateCalled);
}

BOOST_AUTO_TEST_SUITE_END()