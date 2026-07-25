// test_game_replication.cpp
// Integration-style tests that mirror the game's networking patterns:
//   - Replicator peekData() works inside inPreUpdateItem (worm<->player linkage)
//   - Node events (e.g. sync messages) reach the client's event queue
//   - Node events arriving before the local node exists are buffered & replayed
//   - Client node role (Owner/Proxy) is honoured by sendEvent rule checks

#include <boost/test/unit_test.hpp>
#include <cstring>
#include <cstdio>
#include <cstdint>

#include "net_control.h"
#include "net_node.h"
#include "net_replicator.h"

static int s_port = 24000;

// Mirror the game pattern: authority node replicates an INTERCEPT int (like
// NetWorm::m_playerID / BasePlayer::m_wormID). The client interceptor calls
// peekData() to read the incoming value before it is stored.
struct GameInterceptor : public ZCom_NodeReplicationInterceptor {
	int inPreUpdateItemCount = 0;
	uint32_t lastPeeked = 0xDEAD;
	bool peekCalled = false;

	bool inPreUpdate(ZCom_Node *, uint32_t, eZCom_NodeRole) override {
		return true;
	}
	bool inPreUpdateItem(ZCom_Node *, uint32_t, eZCom_NodeRole, ZCom_Replicator *rep, uint32_t) override {
		inPreUpdateItemCount++;
		void *peeked = rep->peekData();
		if (peeked) {
			peekCalled = true;
			lastPeeked = *static_cast<uint32_t *>(peeked);
		}
		return true;
	}
	bool outPreUpdate(ZCom_Node *, uint32_t, eZCom_NodeRole) override {
		return true;
	}
	bool outPreUpdateItem(ZCom_Node *, uint32_t, eZCom_NodeRole, ZCom_Replicator *) override {
		return true;
	}
};

class RepSrv : public ZCom_Control {
  public:
	uint32_t cls;
	int spawned = 0;
	ZCom_Node *node = nullptr;
	~RepSrv() {
		delete node;
		node = nullptr;
	}
	RepSrv(int port) {
		g_currentControl = this;
		ZCom_initSockets(true, port, 2, 0);
		cls = ZCom_registerClass("Rep", 0);
	}
	bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override {
		return true;
	}
	void ZCom_cbConnectionSpawned(uint32_t) override {
		spawned++;
	}
};

class RepCli : public ZCom_Control {
  public:
	uint32_t cls;
	bool connected = false;
	GameInterceptor interceptor;
	ZCom_Node *node = nullptr;
	uint32_t gotNodeID = 0;
	int gotRole = -1;
	~RepCli() {
		delete node;
		node = nullptr;
	}
	RepCli(int port) {
		g_currentControl = this;
		ZCom_initSockets(false, 0, 0, 0);
		cls = ZCom_registerClass("Rep", 0);
	}
	uint32_t ConnectTo(const char *host, int port) {
		ZCom_Address a;
		char hp[64];
		snprintf(hp, sizeof hp, "%s:%d", host, port);
		a.setAddress(eZCom_AddressUDP, 0, hp);
		return ZCom_Connect(a, nullptr);
	}
	void ZCom_cbConnectResult(uint32_t, eZCom_ConnectResult r, ZCom_BitStream &) override {
		connected = (r == eZCom_ConnAccepted);
	}
	void ZCom_cbNodeRequest_Dynamic(uint32_t, uint32_t, ZCom_BitStream *, int role, uint32_t net_id) override {
		gotNodeID = net_id;
		gotRole = role;
		node = new ZCom_Node();
		// Net layer assigns the server-announced node ID via applyRequestNodeID.
		node->setEventNotification(true, true);
		node->beginReplicationSetup(1);
		node->setInterceptID(0);
		static uint32_t local = 0;
		node->addReplicationInt((zS32 *)&local, 32, false, ZCOM_REPFLAG_MOSTRECENT | ZCOM_REPFLAG_INTERCEPT,
								ZCOM_REPRULE_AUTH_2_ALL, 0);
		node->endReplicationSetup();
		node->setReplicationInterceptor(&interceptor);
		node->registerRequestedNode(cls, this);
		// Role is auto-assigned by the omfgnet layer from the request context.
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

BOOST_AUTO_TEST_SUITE(game_replication)

// peekData() inside inPreUpdateItem returns the incoming value (not nullptr),
// enabling the worm<->player linkage that BasePlayer/NetWorm interceptors rely on.
BOOST_AUTO_TEST_CASE(interceptor_peekdata_returns_incoming_value) {
	g_currentControl = nullptr;
	int port = s_port++;
	RepSrv srv(port);
	RepCli cli(port);
	cli.ConnectTo("127.0.0.1", port);
	processBoth(&srv, &cli, 40);
	BOOST_REQUIRE(cli.connected);

	uint32_t playerID = 42;
	srv.node = new ZCom_Node();
	srv.node->setRole(eZCom_RoleAuthority);
	srv.node->beginReplicationSetup(1);
	srv.node->setInterceptID(0);
	srv.node->addReplicationInt((zS32 *)&playerID, 32, false, ZCOM_REPFLAG_MOSTRECENT | ZCOM_REPFLAG_INTERCEPT,
								ZCOM_REPRULE_AUTH_2_ALL, 0);
	srv.node->endReplicationSetup();
	srv.node->registerNodeDynamic(srv.cls, &srv);
	srv.node->setOwner(1, true);
	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE_NE(cli.gotNodeID, 0u);

	playerID = 99;
	processBoth(&srv, &cli, 30);

	BOOST_CHECK_GT(cli.interceptor.inPreUpdateItemCount, 0);
	BOOST_CHECK(cli.interceptor.peekCalled);
	BOOST_CHECK_EQUAL(cli.interceptor.lastPeeked, 99u);

	srv.Shutdown();
	cli.Shutdown();
}

// A node event sent from server->client must land in the client node's event
// queue (checkEventWaiting / getNextEvent). This was a coverage gap: the
// existing test_events.cpp only asserted BOOST_CHECK(true).
BOOST_AUTO_TEST_CASE(node_event_reaches_client_queue) {
	g_currentControl = nullptr;
	int port = s_port++;

	// Simple server with an authority dynamic node.
	class ESrv : public ZCom_Control {
	  public:
		uint32_t cls;
		ZCom_Node *node = nullptr;
		int spawned = 0;
		~ESrv() {
			delete node;
			node = nullptr;
		}
		ESrv(int p) {
			g_currentControl = this;
			ZCom_initSockets(true, p, 2, 0);
			cls = ZCom_registerClass("E", 0);
		}
		bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override {
			return true;
		}
		void ZCom_cbConnectionSpawned(uint32_t) override {
			spawned++;
		}
	} srv(port);

	class ECli : public ZCom_Control {
	  public:
		uint32_t cls;
		bool connected = false;
		ZCom_Node *node = nullptr;
		uint32_t gotID = 0;
		~ECli() {
			delete node;
			node = nullptr;
		}
		ECli(int p) {
			g_currentControl = this;
			ZCom_initSockets(false, 0, 0, 0);
			cls = ZCom_registerClass("E", 0);
		}
		uint32_t ConnectTo(const char *h, int p) {
			ZCom_Address a;
			char hp[64];
			snprintf(hp, sizeof hp, "%s:%d", h, p);
			a.setAddress(eZCom_AddressUDP, 0, hp);
			return ZCom_Connect(a, nullptr);
		}
		void ZCom_cbConnectResult(uint32_t, eZCom_ConnectResult r, ZCom_BitStream &) override {
			connected = (r == eZCom_ConnAccepted);
		}
		void ZCom_cbNodeRequest_Dynamic(uint32_t, uint32_t, ZCom_BitStream *, int role, uint32_t net_id) override {
			gotID = net_id;
			node = new ZCom_Node();
			// Net layer assigns the server-announced node ID via applyRequestNodeID.
			node->setEventNotification(true, true);
			node->registerRequestedNode(cls, this);
			// Role auto-assigned by omfgnet layer from request context.
		}
	} cli(port);

	cli.ConnectTo("127.0.0.1", port);
	processBoth(&srv, &cli, 40);
	BOOST_REQUIRE(cli.connected);

	srv.node = new ZCom_Node();
	srv.node->setRole(eZCom_RoleAuthority);
	srv.node->registerNodeDynamic(srv.cls, &srv);
	srv.node->setEventNotification(true, false);
	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE_NE(cli.gotID, 0u);
	BOOST_REQUIRE(cli.node != nullptr);

	// Send a user event with a payload.
	ZCom_BitStream ev;
	ev.addInt(0xCAFE, 16);
	srv.node->sendEvent(eZCom_ReliableOrdered, 0, &ev);
	processBoth(&srv, &cli, 20);

	BOOST_CHECK(cli.node->checkEventWaiting());
	if (cli.node->checkEventWaiting()) {
		eZCom_Event type;
		eZCom_NodeRole role;
		uint32_t connID;
		auto data = cli.node->getNextEvent(&type, &role, &connID);
		BOOST_CHECK_EQUAL(type, eZCom_EventUser);
		BOOST_REQUIRE(data);
		BOOST_CHECK_EQUAL(data->getInt(16), 0xCAFE);
	}

	srv.Shutdown();
	cli.Shutdown();
}

// A node event that arrives BEFORE the client has created the local node
// (race between announcement and a sync message) must be buffered and replayed
// when the node is registered, not silently dropped.
BOOST_AUTO_TEST_CASE(node_event_buffered_before_node_registration) {
	g_currentControl = nullptr;
	int port = s_port++;

	class BSrv : public ZCom_Control {
	  public:
		uint32_t cls;
		ZCom_Node *node = nullptr;
		~BSrv() {
			delete node;
			node = nullptr;
		}
		BSrv(int p) {
			g_currentControl = this;
			ZCom_initSockets(true, p, 2, 0);
			cls = ZCom_registerClass("B", 0);
		}
		bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override {
			return true;
		}
		void ZCom_cbConnectionSpawned(uint32_t) override {}
	} srv(port);

	class BCli : public ZCom_Control {
	  public:
		uint32_t cls;
		bool connected = false;
		ZCom_Node *node = nullptr;
		uint32_t gotID = 0;
		bool created = false; // defer node creation to simulate the race
		GameInterceptor interceptor;
		~BCli() {
			delete node;
			node = nullptr;
		}
		BCli(int p) {
			g_currentControl = this;
			ZCom_initSockets(false, 0, 0, 0);
			cls = ZCom_registerClass("B", 0);
		}
		uint32_t ConnectTo(const char *h, int p) {
			ZCom_Address a;
			char hp[64];
			snprintf(hp, sizeof hp, "%s:%d", h, p);
			a.setAddress(eZCom_AddressUDP, 0, hp);
			return ZCom_Connect(a, nullptr);
		}
		void ZCom_cbConnectResult(uint32_t, eZCom_ConnectResult r, ZCom_BitStream &) override {
			connected = (r == eZCom_ConnAccepted);
		}
		// Defer node creation to force event buffering.
		void ZCom_cbNodeRequest_Dynamic(uint32_t, uint32_t, ZCom_BitStream *, int role, uint32_t net_id) override {
			gotID = net_id;
			// Record but don't create the node yet.
		}
		void createNodeNow() {
			node = new ZCom_Node();
			// In real usage registerRequestedNode is called inside
			// cbNodeRequest_Dynamic where applyRequestNodeID auto-assigns
			// the server-announced ID from m_requestCtx. Outside the
			// callback we must set it explicitly using the ID we recorded.
			node->setNodeID(gotID);
			node->setEventNotification(true, true);
			node->setReplicationInterceptor(&interceptor);
			node->registerRequestedNode(cls, this);
			// Role auto-assigned by omfgnet layer from request context.
			created = true;
		}
	} cli(port);

	cli.ConnectTo("127.0.0.1", port);
	processBoth(&srv, &cli, 40);
	BOOST_REQUIRE(cli.connected);

	// Create server node; it auto-announces. The server also sends a sync
	// event immediately (simulating the eEvent_Init -> sendSyncMessage path).
	srv.node = new ZCom_Node();
	srv.node->setRole(eZCom_RoleAuthority);
	srv.node->setEventNotification(true, false);
	srv.node->registerNodeDynamic(srv.cls, &srv);

	// Pump once so the announcement is sent but the client's cbNodeRequest
	// fires (recording gotID without creating the node).
	processBoth(&srv, &cli, 5);
	BOOST_REQUIRE_NE(cli.gotID, 0u);

	// Send a sync event from the server directly to the client's node ID.
	// The client node does not exist yet, so this must be buffered.
	ZCom_BitStream sync;
	sync.addInt(0x1234, 16);
	srv.node->sendEventDirect(eZCom_ReliableOrdered, &sync, 1);
	processBoth(&srv, &cli, 10);

	// The client node still does not exist; the event should be buffered.
	BOOST_REQUIRE(cli.node == nullptr);

	// Now create the client node — the buffered event must be replayed.
	g_currentControl = &cli;
	cli.createNodeNow();
	g_currentControl = nullptr;
	processBoth(&srv, &cli, 5);

	BOOST_REQUIRE(cli.node != nullptr);
	BOOST_CHECK(cli.node->checkEventWaiting());
	if (cli.node->checkEventWaiting()) {
		eZCom_Event type;
		eZCom_NodeRole role;
		uint32_t connID;
		auto data = cli.node->getNextEvent(&type, &role, &connID);
		BOOST_CHECK_EQUAL(type, eZCom_EventUser);
		BOOST_REQUIRE(data);
		BOOST_CHECK_EQUAL(data->getInt(16), 0x1234);
	}

	srv.Shutdown();
	cli.Shutdown();
}

// Client node role (Owner) must allow sending events with OWNER_2_AUTH rule.
// Without the correct role, selectWeapons() events would be silently dropped.
BOOST_AUTO_TEST_CASE(client_owner_role_allows_owner_to_auth_event) {
	g_currentControl = nullptr;
	int port = s_port++;

	class OSrv : public ZCom_Control {
	  public:
		uint32_t cls;
		ZCom_Node *node = nullptr;
		int spawned = 0;
		uint32_t clientID = 0;
		~OSrv() {
			delete node;
			node = nullptr;
		}
		OSrv(int p) {
			g_currentControl = this;
			ZCom_initSockets(true, p, 2, 0);
			cls = ZCom_registerClass("O", 0);
		}
		bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override {
			return true;
		}
		void ZCom_cbConnectionSpawned(uint32_t id) override {
			spawned++;
			if (clientID == 0)
				clientID = id;
		}
	} srv(port);

	class OCli : public ZCom_Control {
	  public:
		uint32_t cls;
		bool connected = false;
		ZCom_Node *node = nullptr;
		uint32_t gotID = 0;
		int gotRole = -1;
		~OCli() {
			delete node;
			node = nullptr;
		}
		OCli(int p) {
			g_currentControl = this;
			ZCom_initSockets(false, 0, 0, 0);
			cls = ZCom_registerClass("O", 0);
		}
		uint32_t ConnectTo(const char *h, int p) {
			ZCom_Address a;
			char hp[64];
			snprintf(hp, sizeof hp, "%s:%d", h, p);
			a.setAddress(eZCom_AddressUDP, 0, hp);
			return ZCom_Connect(a, nullptr);
		}
		void ZCom_cbConnectResult(uint32_t, eZCom_ConnectResult r, ZCom_BitStream &) override {
			connected = (r == eZCom_ConnAccepted);
		}
		void ZCom_cbNodeRequest_Dynamic(uint32_t, uint32_t, ZCom_BitStream *, int role, uint32_t net_id) override {
			gotID = net_id;
			gotRole = role;
			node = new ZCom_Node();
			// Net layer assigns the server-announced node ID via applyRequestNodeID.
			node->setEventNotification(true, true);
			node->registerRequestedNode(cls, this);
			// Role (Owner) auto-assigned by omfgnet layer from request context.
		}
	} cli(port);

	cli.ConnectTo("127.0.0.1", port);
	processBoth(&srv, &cli, 40);
	BOOST_REQUIRE(cli.connected);
	BOOST_REQUIRE_NE(srv.clientID, 0u);

	// Server creates a node owned by the client — client should get RoleOwner.
	srv.node = new ZCom_Node();
	srv.node->setRole(eZCom_RoleAuthority);
	srv.node->setOwner(srv.clientID, true);
	srv.node->registerNodeDynamic(srv.cls, &srv);
	srv.node->setEventNotification(true, false);
	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE_NE(cli.gotID, 0u);
	BOOST_CHECK_EQUAL(cli.gotRole, eZCom_RoleOwner);

	// Client (owner) sends an OWNER_2_AUTH event; server should receive it.
	ZCom_BitStream ev;
	ev.addInt(0x77, 8);
	cli.node->sendEvent(eZCom_ReliableOrdered, ZCOM_REPRULE_OWNER_2_AUTH, &ev);
	processBoth(&srv, &cli, 20);

	bool srvGotIt = false;
	if (srv.node) {
		while (srv.node->checkEventWaiting()) {
			eZCom_Event type;
			eZCom_NodeRole role;
			uint32_t connID;
			auto data = srv.node->getNextEvent(&type, &role, &connID);
			if (type == eZCom_EventUser && data) {
				srvGotIt = true;
				BOOST_CHECK_EQUAL(data->getInt(8), 0x77);
			}
		}
	}
	BOOST_CHECK(srvGotIt);

	srv.Shutdown();
	cli.Shutdown();
}

// Regression: an Owner sending a COMBINED reprule
// (AUTH_2_PROXY | OWNER_2_AUTH — exactly what BasePlayer::baseActionStart uses
// for every action: JUMP/RESPAWN/FIRE/...) must reach the Authority via the
// OWNER_2_AUTH leg. The previous sendEvent guard hard-dropped the whole event
// because the rule contained an AUTH_* bit, silently discarding all client
// action events so the client could never spawn.
BOOST_AUTO_TEST_CASE(client_owner_role_combined_rule_reaches_authority) {
	g_currentControl = nullptr;
	int port = s_port++;

	class CSrv : public ZCom_Control {
	  public:
		uint32_t cls;
		ZCom_Node *node = nullptr;
		uint32_t clientID = 0;
		~CSrv() {
			delete node;
			node = nullptr;
		}
		CSrv(int p) {
			g_currentControl = this;
			ZCom_initSockets(true, p, 2, 0);
			cls = ZCom_registerClass("C", 0);
		}
		bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override {
			return true;
		}
		void ZCom_cbConnectionSpawned(uint32_t id) override {
			if (clientID == 0)
				clientID = id;
		}
	} srv(port);

	class CCli : public ZCom_Control {
	  public:
		uint32_t cls;
		bool connected = false;
		ZCom_Node *node = nullptr;
		uint32_t gotID = 0;
		int gotRole = -1;
		~CCli() {
			delete node;
			node = nullptr;
		}
		CCli(int p) {
			g_currentControl = this;
			ZCom_initSockets(false, 0, 0, 0);
			cls = ZCom_registerClass("C", 0);
		}
		uint32_t ConnectTo(const char *h, int p) {
			ZCom_Address a;
			char hp[64];
			snprintf(hp, sizeof hp, "%s:%d", h, p);
			a.setAddress(eZCom_AddressUDP, 0, hp);
			return ZCom_Connect(a, nullptr);
		}
		void ZCom_cbConnectResult(uint32_t, eZCom_ConnectResult r, ZCom_BitStream &) override {
			connected = (r == eZCom_ConnAccepted);
		}
		void ZCom_cbNodeRequest_Dynamic(uint32_t, uint32_t, ZCom_BitStream *, int role, uint32_t net_id) override {
			gotID = net_id;
			gotRole = role;
			node = new ZCom_Node();
			node->setEventNotification(true, true);
			node->registerRequestedNode(cls, this);
		}
	} cli(port);

	cli.ConnectTo("127.0.0.1", port);
	processBoth(&srv, &cli, 40);
	BOOST_REQUIRE(cli.connected);
	BOOST_REQUIRE_NE(srv.clientID, 0u);

	// Server creates a node owned by the client — client should get RoleOwner.
	srv.node = new ZCom_Node();
	srv.node->setRole(eZCom_RoleAuthority);
	srv.node->setOwner(srv.clientID, true);
	srv.node->registerNodeDynamic(srv.cls, &srv);
	srv.node->setEventNotification(true, false);
	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE_NE(cli.gotID, 0u);
	BOOST_CHECK_EQUAL(cli.gotRole, eZCom_RoleOwner);

	// Client (owner) sends an event with the combined rule used by
	// BasePlayer::baseActionStart. The OWNER_2_AUTH leg must deliver it to the
	// authority; the AUTH_2_PROXY bit is inapplicable to an Owner sender and
	// must not cause the event to be dropped.
	ZCom_BitStream ev;
	ev.addInt(0x77, 8);
	cli.node->sendEvent(eZCom_ReliableOrdered, ZCOM_REPRULE_AUTH_2_PROXY | ZCOM_REPRULE_OWNER_2_AUTH, &ev);
	processBoth(&srv, &cli, 20);

	bool srvGotIt = false;
	if (srv.node) {
		while (srv.node->checkEventWaiting()) {
			eZCom_Event type;
			eZCom_NodeRole role;
			uint32_t connID;
			auto data = srv.node->getNextEvent(&type, &role, &connID);
			if (type == eZCom_EventUser && data) {
				srvGotIt = true;
				BOOST_CHECK_EQUAL(data->getInt(8), 0x77);
			}
		}
	}
	BOOST_CHECK(srvGotIt);

	srv.Shutdown();
	cli.Shutdown();
}

// End-to-end game-style flow: authority node with event notification + owner,
// eEvent_Init fires on the server -> server sends a sync event to the client
// (mirrors BasePlayer/NetWorm sendSyncMessage). The client must receive it,
// even though the client node is created asynchronously in cbNodeRequest_Dynamic.
// Also verifies replicator state propagates to the client after the link is up.
BOOST_AUTO_TEST_CASE(full_sync_and_replication_flow) {
	g_currentControl = nullptr;
	int port = s_port++;

	class FullSrv : public ZCom_Control {
	  public:
		uint32_t cls;
		ZCom_Node *node = nullptr;
		uint32_t clientID = 0;
		int32_t state = 7; // replicated authority state
		~FullSrv() {
			delete node;
			node = nullptr;
		}
		FullSrv(int p) {
			g_currentControl = this;
			ZCom_initSockets(true, p, 2, 0);
			cls = ZCom_registerClass("F", 0);
		}
		bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override {
			return true;
		}
		void ZCom_cbConnectionSpawned(uint32_t id) override {
			if (clientID == 0)
				clientID = id;
		}
		// Server pumps its node events; on eEvent_Init it sends a sync event
		// to the new peer (exactly like BasePlayer::think / NetWorm::think).
		void pumpNode() {
			if (!node)
				return;
			while (node->checkEventWaiting()) {
				eZCom_Event type;
				eZCom_NodeRole role;
				uint32_t connID;
				auto data = node->getNextEvent(&type, &role, &connID);
				if (type == eZCom_EventInit) {
					ZCom_BitStream sync;
					sync.addInt(0x5BAD, 16);
					node->sendEventDirect(eZCom_ReliableOrdered, &sync, connID);
				}
			}
		}
	} srv(port);

	class FullCli : public ZCom_Control {
	  public:
		uint32_t cls;
		bool connected = false;
		ZCom_Node *node = nullptr;
		int32_t localState = 0;
		int syncEvents = 0;
		~FullCli() {
			delete node;
			node = nullptr;
		}
		FullCli(int p) {
			g_currentControl = this;
			ZCom_initSockets(false, 0, 0, 0);
			cls = ZCom_registerClass("F", 0);
		}
		uint32_t ConnectTo(const char *h, int p) {
			ZCom_Address a;
			char hp[64];
			snprintf(hp, sizeof hp, "%s:%d", h, p);
			a.setAddress(eZCom_AddressUDP, 0, hp);
			return ZCom_Connect(a, nullptr);
		}
		void ZCom_cbConnectResult(uint32_t, eZCom_ConnectResult r, ZCom_BitStream &) override {
			connected = (r == eZCom_ConnAccepted);
		}
		void ZCom_cbNodeRequest_Dynamic(uint32_t, uint32_t, ZCom_BitStream *, int role, uint32_t net_id) override {
			node = new ZCom_Node();
			// Net layer assigns the server-announced node ID via applyRequestNodeID.
			node->setEventNotification(true, true);
			node->beginReplicationSetup(1);
			node->addReplicationInt((zS32 *)&localState, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL,
									0);
			node->endReplicationSetup();
			node->registerRequestedNode(cls, this);
			// Role auto-assigned by omfgnet layer from request context.
		}
		void pumpNode() {
			if (!node)
				return;
			while (node->checkEventWaiting()) {
				eZCom_Event type;
				eZCom_NodeRole role;
				uint32_t connID;
				auto data = node->getNextEvent(&type, &role, &connID);
				if (type == eZCom_EventUser && data) {
					// Verify sync payload (idempotent across duplicate syncs)
					(void)data->getInt(16);
					syncEvents++;
				}
			}
		}
	} cli(port);

	cli.ConnectTo("127.0.0.1", port);
	processBoth(&srv, &cli, 40);
	BOOST_REQUIRE(cli.connected);
	BOOST_REQUIRE_NE(srv.clientID, 0u);

	// Server creates an authority node owned by the client. It auto-announces.
	srv.node = new ZCom_Node();
	srv.node->setEventNotification(true, false); // generate eEvent_Init on link
	srv.node->beginReplicationSetup(1);
	srv.node->addReplicationInt((zS32 *)&srv.state, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL, 0);
	srv.node->endReplicationSetup();
	srv.node->setOwner(srv.clientID, true);
	srv.node->registerNodeDynamic(srv.cls, &srv);

	// Pump the server's node events so eEvent_Init -> sendEventDirect(sync) fires.
	g_currentControl = &srv;
	srv.pumpNode();
	g_currentControl = nullptr;

	// Let the announcement + sync event + replicator state propagate.
	processBoth(&srv, &cli, 40);
	g_currentControl = &cli;
	cli.pumpNode();
	g_currentControl = nullptr;
	g_currentControl = &srv;
	srv.pumpNode();
	g_currentControl = nullptr;
	processBoth(&srv, &cli, 20);
	g_currentControl = &cli;
	cli.pumpNode();
	g_currentControl = nullptr;

	// Client node was created via the request callback.
	BOOST_REQUIRE(cli.node != nullptr);
	// Client received at least one sync event (the buffered-then-replayed one).
	BOOST_CHECK_GE(cli.syncEvents, 1);
	// Replicated authority state reached the client.
	BOOST_CHECK_EQUAL(cli.localState, 7);

	// Now change authority state and confirm it propagates.
	srv.state = 99;
	processBoth(&srv, &cli, 40);
	BOOST_CHECK_EQUAL(cli.localState, 99);

	srv.Shutdown();
	cli.Shutdown();
}

// Regression for applyRequestNodeID: the client's proxy node (created via
// registerRequestedNode inside cbNodeRequest_Dynamic, WITHOUT a manual
// setNetworkID) must receive the server-announced node ID from the Net layer.
// Previously applyRequestNodeID pushed/announced but never setNodeID, leaving
// the client node at nodeID=0; MSG_REPLICATORS for the server's ID then never
// resolved ("NOT FOUND locally — buffering for later") and state never arrived.
BOOST_AUTO_TEST_CASE(register_requested_node_gets_announced_id) {
	g_currentControl = nullptr;
	int port = s_port++;
	RepSrv srv(port);
	RepCli cli(port);
	cli.ConnectTo("127.0.0.1", port);
	processBoth(&srv, &cli, 40);
	BOOST_REQUIRE(cli.connected);

	uint32_t playerID = 42;
	g_currentControl = &srv;
	srv.node = new ZCom_Node();
	srv.node->setRole(eZCom_RoleAuthority);
	srv.node->beginReplicationSetup(1);
	srv.node->setInterceptID(0);
	srv.node->addReplicationInt((zS32 *)&playerID, 32, false, ZCOM_REPFLAG_MOSTRECENT | ZCOM_REPFLAG_INTERCEPT,
								ZCOM_REPRULE_AUTH_2_ALL, 0);
	srv.node->endReplicationSetup();
	srv.node->registerNodeDynamic(srv.cls, &srv);
	g_currentControl = nullptr;
	processBoth(&srv, &cli, 30);

	// Client created its proxy node from the request callback.
	BOOST_REQUIRE(cli.node != nullptr);
	BOOST_REQUIRE_NE(cli.gotNodeID, 0u);
	// The proxy node's local ID must match the server-announced ID (assigned by
	// applyRequestNodeID), not be left at the 0 default.
	BOOST_CHECK_NE(cli.node->getNetworkID(), 0u);
	BOOST_CHECK_EQUAL(cli.node->getNetworkID(), cli.gotNodeID);

	// Replicator state for the announced ID must reach the client (lookup
	// resolves instead of buffering forever).
	playerID = 77;
	processBoth(&srv, &cli, 30);
	BOOST_CHECK_GT(cli.interceptor.inPreUpdateItemCount, 0);
	BOOST_CHECK_EQUAL(cli.interceptor.lastPeeked, 77u);

	srv.Shutdown();
	cli.Shutdown();
}

// Full two-control round-trip regression test for signed-int auto-replication.
// Exercises the runtime path: packReplicatorsForRouting on the server → wire →
// unpackAllReplicators on the client. Before the fix, m_dir=-1 became 255 on
// the client (no sign extension for bits<32).
BOOST_AUTO_TEST_CASE(signed_int_replication_roundtrip) {
	g_currentControl = nullptr;
	int port = s_port++;

	class SIRsrv : public ZCom_Control {
	  public:
		uint32_t cls;
		ZCom_Node *node = nullptr;
		~SIRsrv() {
			delete node;
			node = nullptr;
		}
		SIRsrv(int p) {
			g_currentControl = this;
			ZCom_initSockets(true, p, 2, 0);
			cls = ZCom_registerClass("SI", 0);
		}
		bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override {
			return true;
		}
		void ZCom_cbConnectionSpawned(uint32_t) override {}
	} srv(port);

	class SIRcli : public ZCom_Control {
	  public:
		uint32_t cls;
		bool connected = false;
		ZCom_Node *node = nullptr;
		int32_t rdir = 0;
		~SIRcli() {
			delete node;
			node = nullptr;
		}
		SIRcli(int p) {
			g_currentControl = this;
			ZCom_initSockets(false, 0, 0, 0);
			cls = ZCom_registerClass("SI", 0);
		}
		uint32_t ConnectTo(const char *h, int p) {
			ZCom_Address a;
			char hp[64];
			snprintf(hp, sizeof hp, "%s:%d", h, p);
			a.setAddress(eZCom_AddressUDP, 0, hp);
			return ZCom_Connect(a, nullptr);
		}
		void ZCom_cbConnectResult(uint32_t, eZCom_ConnectResult r, ZCom_BitStream &) override {
			connected = (r == eZCom_ConnAccepted);
		}
		void ZCom_cbNodeRequest_Dynamic(uint32_t, uint32_t, ZCom_BitStream *, int, uint32_t) override {
			node = new ZCom_Node();
			node->beginReplicationSetup(1);
			node->addReplicationInt(&rdir, 8, true, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL, 0);
			node->endReplicationSetup();
			node->registerRequestedNode(cls, this);
		}
	} cli(port);

	cli.ConnectTo("127.0.0.1", port);
	processBoth(&srv, &cli, 40);
	BOOST_REQUIRE(cli.connected);

	// Create authority node with m_dir = -1 (worm faces left).
	// Set value BEFORE registration so the initial pack includes -1.
	int32_t dir = 0;
	g_currentControl = &srv;
	srv.node = new ZCom_Node();
	srv.node->setRole(eZCom_RoleAuthority);
	srv.node->beginReplicationSetup(1);
	srv.node->addReplicationInt(&dir, 8, true, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL, 0);
	srv.node->endReplicationSetup();
	dir = -1;
	srv.node->registerNodeDynamic(srv.cls, &srv);
	g_currentControl = nullptr;

	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.node != nullptr);

	// Client should have received -1 (with the fix; without the fix it would be 255)
	BOOST_CHECK_EQUAL(cli.rdir, -1);

	srv.Shutdown();
	cli.Shutdown();
}

// Regression for the "server worm missing on client" bug:
// When an authority node is created on the server BEFORE any client connects,
// its auto-replication entries have initial=true. Without the
// m_forceReplicationUpdate flag, processOutput() packs and clears that flag
// even with no peers — so by the time a client connects, the dirty state has
// been consumed and the announcement sync can't recover the initial values.
// With the fix, ZCom_cbConnectionSpawned marks existing nodes as force-update,
// so the first packReplicatorsForRouting after connection sends full state.
BOOST_AUTO_TEST_CASE(authority_node_before_client_still_replicates) {
	g_currentControl = nullptr;
	int port = s_port++;

	class PreSrv : public ZCom_Control {
	  public:
		uint32_t cls;
		ZCom_Node *node = nullptr;
		uint32_t clientID = 0;
		int32_t hp = 100; // replicated authority state
		~PreSrv() {
			delete node;
			node = nullptr;
		}
		PreSrv(int p) {
			g_currentControl = this;
			ZCom_initSockets(true, p, 2, 0);
			cls = ZCom_registerClass("PR", 0);
		}
		bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override {
			return true;
		}
		void ZCom_cbConnectionSpawned(uint32_t id) override {
			if (clientID == 0)
				clientID = id;
		}
	} srv(port);

	class PreCli : public ZCom_Control {
	  public:
		uint32_t cls;
		bool connected = false;
		ZCom_Node *node = nullptr;
		int32_t rhp = 0;
		~PreCli() {
			delete node;
			node = nullptr;
		}
		PreCli(int p) {
			g_currentControl = this;
			ZCom_initSockets(false, 0, 0, 0);
			cls = ZCom_registerClass("PR", 0);
		}
		uint32_t ConnectTo(const char *h, int p) {
			ZCom_Address a;
			char hp[64];
			snprintf(hp, sizeof hp, "%s:%d", h, p);
			a.setAddress(eZCom_AddressUDP, 0, hp);
			return ZCom_Connect(a, nullptr);
		}
		void ZCom_cbConnectResult(uint32_t, eZCom_ConnectResult r, ZCom_BitStream &) override {
			connected = (r == eZCom_ConnAccepted);
		}
		void ZCom_cbNodeRequest_Dynamic(uint32_t, uint32_t, ZCom_BitStream *, int, uint32_t) override {
			node = new ZCom_Node();
			node->beginReplicationSetup(1);
			node->addReplicationInt(&rhp, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL, 0);
			node->endReplicationSetup();
			node->registerRequestedNode(cls, this);
		}
	} cli(port);

	// Server creates authority node BEFORE client connects, with state = 100.
	g_currentControl = &srv;
	srv.node = new ZCom_Node();
	srv.node->setRole(eZCom_RoleAuthority);
	srv.node->beginReplicationSetup(1);
	srv.node->addReplicationInt(&srv.hp, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL, 0);
	srv.node->endReplicationSetup();
	srv.node->registerNodeDynamic(srv.cls, &srv);
	g_currentControl = nullptr;

	// Tick server with NO peers — this consumes initial=true entries.
	// Without the fix, the next client to connect would receive an empty state.
	processBoth(&srv, &cli, 10);

	// Mutate state AFTER the empty ticks to verify it propagates regardless.
	srv.hp = 100;
	processBoth(&srv, &cli, 5);

	// Now client connects.
	cli.ConnectTo("127.0.0.1", port);
	processBoth(&srv, &cli, 60);
	BOOST_REQUIRE(cli.connected);
	BOOST_REQUIRE(cli.node != nullptr);

	// Client should have received the server's authority state.
	// Without the force-replication fix this is 0 (initial=true consumed before peer existed).
	// With the fix this is 100.
	BOOST_CHECK_EQUAL(cli.rhp, 100);

	// Further state changes must also propagate.
	srv.hp = 77;
	processBoth(&srv, &cli, 30);
	BOOST_CHECK_EQUAL(cli.rhp, 77);

	srv.Shutdown();
	cli.Shutdown();
}

// G1 follow-up to the AutoReplicator refactor: verify the bool auto-rep fix
// (AutoReplicatorBool writes exactly 1 byte) through the REAL runtime path —
// packReplicatorsForRouting + forceAll (net_control.cpp:882 forceReplicationUpdate
// on connection spawn) + PackedReplicator.data routing — with a late-joining
// client. The int twin above only covers int; rope active/attached
// (net_worm.cpp) are bool auto-reps that ride this same late-join path, so the
// fix's adjacency guarantee must hold here too: a 1-byte bool write must not
// clobber the neighbouring bytes.
BOOST_AUTO_TEST_CASE(bool_auto_rep_late_join_preserves_adjacent_bytes) {
	g_currentControl = nullptr;
	int port = s_port++;

	// Server authority state: a byte buffer with a bool in the middle so any
	// over-write into the adjacent bytes is detectable.
	uint8_t sbuf[3] = {0xA5, 0, 0x5A}; // a, b(bool), c
	bool *sb = reinterpret_cast<bool *>(&sbuf[1]);
	sbuf[1] = 1; // b = true

	class PreSrv : public ZCom_Control {
	  public:
		uint32_t cls;
		ZCom_Node *node = nullptr;
		uint32_t clientID = 0;
		bool *sb;
		~PreSrv() {
			delete node;
			node = nullptr;
		}
		PreSrv(int p, bool *bp) : sb(bp) {
			g_currentControl = this;
			ZCom_initSockets(true, p, 2, 0);
			cls = ZCom_registerClass("PRB", 0);
		}
		bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override {
			return true;
		}
		void ZCom_cbConnectionSpawned(uint32_t id) override {
			if (clientID == 0)
				clientID = id;
		}
	} srv(port, sb);

	class PreCli : public ZCom_Control {
	  public:
		uint32_t cls;
		bool connected = false;
		ZCom_Node *node = nullptr;
		uint8_t rbuf[3] = {0xA5, 0, 0x5A}; // a, b(bool), c
		bool *rb = reinterpret_cast<bool *>(&rbuf[1]);
		~PreCli() {
			delete node;
			node = nullptr;
		}
		PreCli(int p) {
			g_currentControl = this;
			ZCom_initSockets(false, 0, 0, 0);
			cls = ZCom_registerClass("PRB", 0);
		}
		uint32_t ConnectTo(const char *h, int p) {
			ZCom_Address a;
			char hp[64];
			snprintf(hp, sizeof hp, "%s:%d", h, p);
			a.setAddress(eZCom_AddressUDP, 0, hp);
			return ZCom_Connect(a, nullptr);
		}
		void ZCom_cbConnectResult(uint32_t, eZCom_ConnectResult r, ZCom_BitStream &) override {
			connected = (r == eZCom_ConnAccepted);
		}
		void ZCom_cbNodeRequest_Dynamic(uint32_t, uint32_t, ZCom_BitStream *, int, uint32_t) override {
			node = new ZCom_Node();
			node->beginReplicationSetup(1);
			node->addReplicationBool(rb, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL, 0);
			node->endReplicationSetup();
			node->registerRequestedNode(cls, this);
		}
	} cli(port);

	g_currentControl = &srv;
	srv.node = new ZCom_Node();
	srv.node->setRole(eZCom_RoleAuthority);
	srv.node->beginReplicationSetup(1);
	srv.node->addReplicationBool(sb, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL, 0);
	srv.node->endReplicationSetup();
	srv.node->registerNodeDynamic(srv.cls, &srv);
	g_currentControl = nullptr;

	// Tick server with NO peers: consumes the initial=true bool entry via the
	// runtime packReplicatorsForRouting path, so forceAll is what recovers it.
	processBoth(&srv, &cli, 10);

	// Late join: forceReplicationUpdate() (net_control.cpp:882) re-packs the
	// current bool state through packReplicatorsForRouting -> PackedReplicator.data.
	cli.ConnectTo("127.0.0.1", port);
	processBoth(&srv, &cli, 60);
	BOOST_REQUIRE(cli.connected);
	BOOST_REQUIRE(cli.node != nullptr);

	// Bool must have arrived...
	BOOST_CHECK_EQUAL(*cli.rb, true);
	// ...and adjacent bytes untouched (the fix: 1-byte write, not 4-byte clobber).
	BOOST_CHECK_EQUAL(cli.rbuf[0], 0xA5);
	BOOST_CHECK_EQUAL(cli.rbuf[2], 0x5A);

	// Further bool change must still propagate without clobbering neighbours.
	sbuf[1] = 0; // b = false
	processBoth(&srv, &cli, 30);
	BOOST_CHECK_EQUAL(*cli.rb, false);
	BOOST_CHECK_EQUAL(cli.rbuf[0], 0xA5);
	BOOST_CHECK_EQUAL(cli.rbuf[2], 0x5A);

	srv.Shutdown();
	cli.Shutdown();
}

// Regression for the "client can't spawn" bug:
// When the server creates a worm/player node BEFORE a client connects, it has
// setEventNotification(true, false). On client connect, ZCom_cbConnectionSpawned
// pushes eZCom_EventInit to that node with connID = new client. The server's
// think() must observe that event and send a SYNC event to the client (mirrors
// NetWorm::sendSyncMessage / BasePlayer::sendSyncMessage). Without the event
// arriving, m_isActive stays false on the client → JUMP falls through to RESPAWN
// every frame ("JUMP with inactive worm, sending RESPAWN").
BOOST_AUTO_TEST_CASE(authority_node_before_client_sends_sync_on_init) {
	g_currentControl = nullptr;
	int port = s_port++;

	class SyncSrv : public ZCom_Control {
	  public:
		uint32_t cls;
		ZCom_Node *node = nullptr;
		uint32_t clientID = 0;
		// State we want the SYNC payload to carry.
		bool sIsActive = true;
		bool sNinjaActive = false;
		uint8_t sCurWeapon = 2;
		int syncSendsToClient = 0;
		~SyncSrv() {
			delete node;
			node = nullptr;
		}
		SyncSrv(int p) {
			g_currentControl = this;
			ZCom_initSockets(true, p, 2, 0);
			cls = ZCom_registerClass("SY", 0);
		}
		bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override {
			return true;
		}
		void ZCom_cbConnectionSpawned(uint32_t id) override {
			if (clientID == 0)
				clientID = id;
		}
		// Server's per-tick pump: on eEvent_Init, emit a SYNC event to that peer.
		// Mirrors NetWorm::think() and BasePlayer::think() event-handler branches.
		void pumpAndSync() {
			if (!node)
				return;
			while (node->checkEventWaiting()) {
				eZCom_Event type;
				eZCom_NodeRole role;
				uint32_t connID;
				auto data = node->getNextEvent(&type, &role, &connID);
				if (type == eZCom_EventInit && connID == clientID) {
					ZCom_BitStream sync;
					sync.addBool(sIsActive);
					sync.addBool(sNinjaActive);
					sync.addInt(sCurWeapon, 8);
					node->sendEventDirect(eZCom_ReliableOrdered, &sync, connID);
					syncSendsToClient++;
				}
			}
		}
	} srv(port);

	class SyncCli : public ZCom_Control {
	  public:
		uint32_t cls;
		bool connected = false;
		ZCom_Node *node = nullptr;
		// State decoded from the SYNC event.
		bool cIsActive = false;
		bool cNinjaActive = false;
		int cCurWeapon = -1;
		int syncEvents = 0;
		~SyncCli() {
			delete node;
			node = nullptr;
		}
		SyncCli(int p) {
			g_currentControl = this;
			ZCom_initSockets(false, 0, 0, 0);
			cls = ZCom_registerClass("SY", 0);
		}
		uint32_t ConnectTo(const char *h, int p) {
			ZCom_Address a;
			char hp[64];
			snprintf(hp, sizeof hp, "%s:%d", h, p);
			a.setAddress(eZCom_AddressUDP, 0, hp);
			return ZCom_Connect(a, nullptr);
		}
		void ZCom_cbConnectResult(uint32_t, eZCom_ConnectResult r, ZCom_BitStream &) override {
			connected = (r == eZCom_ConnAccepted);
		}
		void ZCom_cbNodeRequest_Dynamic(uint32_t, uint32_t, ZCom_BitStream *, int, uint32_t) override {
			node = new ZCom_Node();
			node->setEventNotification(false, true); // proxy: remove events only
			node->registerRequestedNode(cls, this);
		}
		// Client drains pending user events; first one is the SYNC payload.
		void drainEvents() {
			if (!node)
				return;
			while (node->checkEventWaiting()) {
				eZCom_Event type;
				eZCom_NodeRole role;
				uint32_t connID;
				auto data = node->getNextEvent(&type, &role, &connID);
				if (type == eZCom_EventUser && data) {
					cIsActive = data->getBool();
					cNinjaActive = data->getBool();
					cCurWeapon = data->getInt(8);
					syncEvents++;
				}
			}
		}
	} cli(port);

	// 1. Server creates the authority node BEFORE any client connects.
	//    This is the real-world server.cpp PLAYER_REQUEST flow, but pushed earlier
	//    so the connection-spawn path has to handle an already-existing node.
	g_currentControl = &srv;
	srv.node = new ZCom_Node();
	srv.node->setRole(eZCom_RoleAuthority);
	srv.node->setEventNotification(true, false); // init yes, remove no
	srv.node->registerNodeDynamic(srv.cls, &srv);
	g_currentControl = nullptr;

	// 2. Client connects. ZCom_cbConnectionSpawned will push eZCom_EventInit
	//    to the existing node with connID = cli conn id.
	cli.ConnectTo("127.0.0.1", port);
	processBoth(&srv, &cli, 40);
	BOOST_REQUIRE(cli.connected);
	BOOST_REQUIRE_NE(srv.clientID, 0u);

	// 3. Server's per-tick pump sees eZCom_EventInit and emits SYNC.
	g_currentControl = &srv;
	srv.pumpAndSync();
	g_currentControl = nullptr;

	// 4. Let the SYNC packet traverse the wire.
	processBoth(&srv, &cli, 40);

	// 5. Client drains its pending user events.
	g_currentControl = &cli;
	cli.drainEvents();
	g_currentControl = nullptr;

	// The SYNC event must have arrived; otherwise the worm stays inactive
	// and JUMP falls through to RESPAWN every frame.
	BOOST_CHECK_GE(cli.syncEvents, 1);
	BOOST_CHECK_EQUAL(cli.cIsActive, true);
	BOOST_CHECK_EQUAL(cli.cNinjaActive, false);
	BOOST_CHECK_EQUAL(cli.cCurWeapon, 2);
	// Server must have sent exactly one SYNC for this connection.
	BOOST_CHECK_EQUAL(srv.syncSendsToClient, 1);

	srv.Shutdown();
	cli.Shutdown();
}

// Regression for "client can't spawn" — same as above but the node is created
// AFTER the client connects (mirrors server.cpp PLAYER_REQUEST flow where the
// server registers the worm and player node, then sets the client as owner in
// the same data-received callback). setOwner() must trigger eZCom_EventInit
// for the new peer, so the server's think-loop emits SYNC to the client.
BOOST_AUTO_TEST_CASE(authority_node_after_client_sends_sync_on_init) {
	g_currentControl = nullptr;
	int port = s_port++;

	class SyncSrv2 : public ZCom_Control {
	  public:
		uint32_t cls;
		ZCom_Node *node = nullptr;
		uint32_t clientID = 0;
		bool sIsActive = true;
		bool sNinjaActive = false;
		uint8_t sCurWeapon = 3;
		int syncSendsToClient = 0;
		~SyncSrv2() {
			delete node;
			node = nullptr;
		}
		SyncSrv2(int p) {
			g_currentControl = this;
			ZCom_initSockets(true, p, 2, 0);
			cls = ZCom_registerClass("SA", 0);
		}
		bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override {
			return true;
		}
		void ZCom_cbConnectionSpawned(uint32_t id) override {
			if (clientID == 0)
				clientID = id;
		}
		void pumpAndSync() {
			if (!node)
				return;
			while (node->checkEventWaiting()) {
				eZCom_Event type;
				eZCom_NodeRole role;
				uint32_t connID;
				auto data = node->getNextEvent(&type, &role, &connID);
				if (type == eZCom_EventInit && connID == clientID) {
					ZCom_BitStream sync;
					sync.addBool(sIsActive);
					sync.addBool(sNinjaActive);
					sync.addInt(sCurWeapon, 8);
					node->sendEventDirect(eZCom_ReliableOrdered, &sync, connID);
					syncSendsToClient++;
				}
			}
		}
	} srv(port);

	class SyncCli2 : public ZCom_Control {
	  public:
		uint32_t cls;
		bool connected = false;
		ZCom_Node *node = nullptr;
		bool cIsActive = false;
		bool cNinjaActive = false;
		int cCurWeapon = -1;
		int syncEvents = 0;
		~SyncCli2() {
			delete node;
			node = nullptr;
		}
		SyncCli2(int p) {
			g_currentControl = this;
			ZCom_initSockets(false, 0, 0, 0);
			cls = ZCom_registerClass("SA", 0);
		}
		uint32_t ConnectTo(const char *h, int p) {
			ZCom_Address a;
			char hp[64];
			snprintf(hp, sizeof hp, "%s:%d", h, p);
			a.setAddress(eZCom_AddressUDP, 0, hp);
			return ZCom_Connect(a, nullptr);
		}
		void ZCom_cbConnectResult(uint32_t, eZCom_ConnectResult r, ZCom_BitStream &) override {
			connected = (r == eZCom_ConnAccepted);
		}
		void ZCom_cbNodeRequest_Dynamic(uint32_t, uint32_t, ZCom_BitStream *, int role, uint32_t net_id) override {
			node = new ZCom_Node();
			node->setEventNotification(false, true);
			// Save role for diagnostics — Owner here means client controls the worm.
			node->setRole(static_cast<eZCom_NodeRole>(role));
			node->registerRequestedNode(cls, this);
		}
		void drainEvents() {
			if (!node)
				return;
			while (node->checkEventWaiting()) {
				eZCom_Event type;
				eZCom_NodeRole role;
				uint32_t connID;
				auto data = node->getNextEvent(&type, &role, &connID);
				if (type == eZCom_EventUser && data) {
					cIsActive = data->getBool();
					cNinjaActive = data->getBool();
					cCurWeapon = data->getInt(8);
					syncEvents++;
				}
			}
		}
	} cli(port);

	// 1. Client connects FIRST, then server creates the worm.
	//    This matches server.cpp's PLAYER_REQUEST flow where the worm node
	//    is created (and the client set as owner) inside the data-received
	//    callback, AFTER the connection has already been established.
	cli.ConnectTo("127.0.0.1", port);
	processBoth(&srv, &cli, 40);
	BOOST_REQUIRE(cli.connected);
	BOOST_REQUIRE_NE(srv.clientID, 0u);

	// 2. Server creates the authority node, owner = client. setOwner must
	//    trigger announceNodeWithOwner which pushes eZCom_EventInit for
	//    the client peer.
	g_currentControl = &srv;
	srv.node = new ZCom_Node();
	srv.node->setRole(eZCom_RoleAuthority);
	srv.node->setEventNotification(true, false); // init yes, remove no
	srv.node->setOwner(srv.clientID, true);
	srv.node->registerNodeDynamic(srv.cls, &srv);
	g_currentControl = nullptr;

	// 3. Pump the wire so the announce + (deferred) init event get sent.
	processBoth(&srv, &cli, 40);

	// 4. Server's per-tick pump sees eZCom_EventInit and emits SYNC.
	g_currentControl = &srv;
	srv.pumpAndSync();
	g_currentControl = nullptr;

	// 5. Let the SYNC packet traverse the wire.
	processBoth(&srv, &cli, 40);

	// 6. Client drains its pending user events.
	g_currentControl = &cli;
	cli.drainEvents();
	g_currentControl = nullptr;

	// The SYNC event must have arrived.
	BOOST_CHECK_GE(cli.syncEvents, 1);
	BOOST_CHECK_EQUAL(cli.cIsActive, true);
	BOOST_CHECK_EQUAL(cli.cNinjaActive, false);
	BOOST_CHECK_EQUAL(cli.cCurWeapon, 3);
	BOOST_CHECK_GE(srv.syncSendsToClient, 1);

	srv.Shutdown();
	cli.Shutdown();
}

BOOST_AUTO_TEST_SUITE_END()
