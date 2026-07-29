// test_authority_announce_role.cpp
// Regression test for T1.4 (removal of dead syncNodesToPeer).
//
// The dead syncNodesToPeer() used node->getRole() (= eZCom_RoleAuthority) as
// the announced role, which would have set the receiving client's node to
// Authority — breaking ownership. The live new-peer path
// (processENetEvent -> CONNECT case) and announceNodeWithOwner use
// eZCom_RoleProxy for non-owner clients. This test asserts that a freshly
// connected client sees a server-authority node with role == eZCom_RoleProxy
// (not eZCom_RoleAuthority), confirming the live path is used.

#include <boost/test/unit_test.hpp>
#include <cstdio>
#include <memory>

#include "net_control.h"
#include "net_node.h"
#include "net_replicator.h"

static int s_ar_port = 27200;

class AuthRoleServer : public ZCom_Control {
  public:
	uint32_t m_testClass;
	ZCom_Node *m_node;
	int32_t m_val;

	~AuthRoleServer() {
		delete m_node;
		m_node = nullptr;
	}
	AuthRoleServer(int port) : m_testClass(0), m_node(nullptr), m_val(0) {
		m_testClass = ZCom_registerClass("AuthRoleNode", 0);
		g_currentControl = this;
		ZCom_initSockets(true, port, 0, 0);
	}

	void createAuthorityNode() {
		m_node = new ZCom_Node();
		// registerNodeDynamic outside cbNodeRequest_Dynamic sets role=Authority.
		m_node->beginReplicationSetup(1);
		// Dummy NONE-rule replicator so the node announces cleanly (mirrors
		// the file-transfer / routing harnesses) without originating traffic.
		m_node->addReplicationInt((zS32 *)&m_val, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_NONE, 0);
		m_node->endReplicationSetup();
		m_node->registerNodeDynamic(m_testClass, this);
	}

  protected:
	bool ZCom_cbConnectionRequest(ZCom_ConnID, ZCom_BitStream &, ZCom_BitStream &) override {
		return true;
	}
	void ZCom_cbConnectionSpawned(ZCom_ConnID) override {}
};

class AuthRoleClient : public ZCom_Control {
  public:
	bool m_connected;
	uint32_t m_testClass;
	int m_receivedRole;
	uint32_t m_receivedNodeID;
	ZCom_Node *m_node;
	int32_t m_val;

	~AuthRoleClient() {
		delete m_node;
		m_node = nullptr;
	}
	AuthRoleClient()
		: m_connected(false), m_testClass(0), m_receivedRole(-1), m_receivedNodeID(0), m_node(nullptr), m_val(0) {
		m_testClass = ZCom_registerClass("AuthRoleNode", 0);
		g_currentControl = this;
		ZCom_initSockets(false, 0, 0, 0);
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
		m_connected = (result == eZCom_ConnAccepted);
	}

	void ZCom_cbNodeRequest_Dynamic(ZCom_ConnID, uint32_t, ZCom_BitStream *, int role, uint32_t net_id) override {
		m_receivedRole = role;
		m_receivedNodeID = net_id;
		m_node = new ZCom_Node();
		m_node->beginReplicationSetup(1);
		m_node->addReplicationInt((zS32 *)&m_val, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_NONE, 0);
		m_node->endReplicationSetup();
		m_node->registerRequestedNode(m_testClass, this);
	}
};

static void processBoth(ZCom_Control *s, ZCom_Control *c, int n) {
	for (int i = 0; i < n; i++) {
		g_currentControl = s;
		s->ZCom_processInput(eZCom_NoBlock);
		s->ZCom_processOutput();
		g_currentControl = c;
		c->ZCom_processInput(eZCom_NoBlock);
		c->ZCom_processOutput();
	}
	g_currentControl = nullptr;
}

BOOST_AUTO_TEST_SUITE(authority_announce_role)

// Server creates an authority node BEFORE the client connects. The new-peer
// sync path in processENetEvent (CONNECT case) must announce it to the client
// with role == eZCom_RoleProxy.
BOOST_AUTO_TEST_CASE(client_sees_server_authority_node_as_proxy) {
	g_currentControl = nullptr;
	int port = s_ar_port++;
	{
		AuthRoleServer srv(port);
		srv.createAuthorityNode(); // authority node exists before any client

		AuthRoleClient cli;
		uint32_t cid = cli.ConnectTo("127.0.0.1", port);
		BOOST_REQUIRE(cid != ZCom_Invalid_ID);
		processBoth(&srv, &cli, 40);
		BOOST_REQUIRE(cli.m_connected);

		// The new-peer sync path must have delivered the node announcement.
		BOOST_CHECK(cli.m_receivedNodeID != 0);
		// Regression for T1.4: the announced role must be Proxy, NOT Authority.
		// (The dead syncNodesToPeer would have announced Authority here.)
		BOOST_CHECK_EQUAL(cli.m_receivedRole, eZCom_RoleProxy);

		srv.Shutdown();
		cli.Shutdown();
	}
}

// Server creates the authority node AFTER the client connects. The
// registerNodeDynamic auto-announce path must also use eZCom_RoleProxy for a
// non-owner client.
BOOST_AUTO_TEST_CASE(auto_announce_after_connect_uses_proxy) {
	g_currentControl = nullptr;
	int port = s_ar_port++;
	{
		AuthRoleServer srv(port);

		AuthRoleClient cli;
		uint32_t cid = cli.ConnectTo("127.0.0.1", port);
		BOOST_REQUIRE(cid != ZCom_Invalid_ID);
		processBoth(&srv, &cli, 40);
		BOOST_REQUIRE(cli.m_connected);

		// Now create the authority node; auto-announce fires to the client.
		srv.createAuthorityNode();
		processBoth(&srv, &cli, 40);

		BOOST_CHECK(cli.m_receivedNodeID != 0);
		BOOST_CHECK_EQUAL(cli.m_receivedRole, eZCom_RoleProxy);

		srv.Shutdown();
		cli.Shutdown();
	}
}

BOOST_AUTO_TEST_SUITE_END()
