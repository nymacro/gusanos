// test_unique_replication.cpp
// Tests for wire-announced unique-node replication in the ENet compat layer.
//
// Background: Game and Updater register their nodes with registerNodeUnique().
// Previously unique nodes were never replicated: the connect-spawn loop skipped
// them, and dispatch by 16-bit nodeID never matched across sides (each side
// assigned an independent m_nextNodeID). These tests cover the new
// MSG_NODE_ANNOUNCE_UNIQUE path:
//   1. A unique authority node's replicator reaches the client proxy (the
//      announce is buffered until the proxy registers, then the proxy adopts
//      the server nodeID and the buffered MSG_REPLICATORS replays).
//   2. A unique authority node's sendEvent(AUTH_2_ALL) reaches the proxy.
//   3. eZCom_EventInit fires on the authority unique node once per newly
//      connecting peer (previously dead for unique nodes).
//   4. Reverse order: the proxy registered BEFORE the announce arrives is
//      re-keyed to the server nodeID and still receives state.
//   5. The Updater pattern: a unique node at zoid level 2 replicates and
//      round-trips an event after the client enables zoid level 2.

#include <boost/test/unit_test.hpp>
#include <cstring>
#include <cstdio>
#include <cstdint>

#include "net_control.h"
#include "net_node.h"
#include "net_replicator.h"

static int s_port = 25000;

// Shared two-control pump (server first, then client).
static void pumpBoth(ZCom_Control *s, ZCom_Control *c, int n) {
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

BOOST_AUTO_TEST_SUITE(unique_replication)

// 1. A unique authority node's replicated int reaches the client proxy, even
// though the proxy is registered after the server already announced (the
// common game ordering). Validates: announce-buffer-by-class, server-nodeID
// adoption on register, and buffered MSG_REPLICATORS replay.
BOOST_AUTO_TEST_CASE(unique_replicator_reaches_proxy) {
	g_currentControl = nullptr;
	int port = s_port++;

	class USrv : public ZCom_Control {
	  public:
		uint32_t cls;
		ZCom_Node *node = nullptr;
		int32_t gravity = 42;
		~USrv() { delete node; }
		USrv(int p) {
			g_currentControl = this;
			ZCom_initSockets(true, p, 2, 0);
			cls = ZCom_registerClass("UQ1", 0);
		}
		bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override { return true; }
		void ZCom_cbConnectionSpawned(uint32_t) override {}
	} srv(port);

	class UCli : public ZCom_Control {
	  public:
		uint32_t cls;
		bool connected = false;
		ZCom_Node *node = nullptr;
		int32_t rgravity = 0;
		~UCli() { delete node; }
		UCli(int p) {
			g_currentControl = this;
			ZCom_initSockets(false, 0, 0, 0);
			cls = ZCom_registerClass("UQ1", 0);
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
		// Proxy is NOT created here (unique nodes are app-silent); the test
		// registers it later to mimic ZCom_cbZoidResult.
	} cli(port);

	// Server registers the unique authority node BEFORE the client connects
	// (mirrors Network::host creating the Game node).
	g_currentControl = &srv;
	srv.node = new ZCom_Node();
	srv.node->beginReplicationSetup(1);
	srv.node->addReplicationInt((zS32 *)&srv.gravity, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL,
								0);
	srv.node->endReplicationSetup();
	srv.node->registerNodeUnique(srv.cls, eZCom_RoleAuthority, &srv);
	g_currentControl = nullptr;

	// Tick the server with no peers so the initial=true entry is consumed
	// (exercises forceReplicationUpdate recovery on late join, like the game).
	pumpBoth(&srv, &cli, 10);

	// Now the client connects.
	cli.ConnectTo("127.0.0.1", port);
	pumpBoth(&srv, &cli, 60);
	BOOST_REQUIRE(cli.connected);

	// The announce (+replicator) arrived while the proxy did not exist yet;
	// both are buffered. Register the proxy now (deferred, post-connect).
	g_currentControl = &cli;
	cli.node = new ZCom_Node();
	cli.node->beginReplicationSetup(1);
	cli.node->addReplicationInt((zS32 *)&cli.rgravity, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL,
								0);
	cli.node->endReplicationSetup();
	cli.node->registerNodeUnique(cli.cls, eZCom_RoleProxy, &cli);
	g_currentControl = nullptr;
	pumpBoth(&srv, &cli, 40);

	BOOST_REQUIRE(cli.node != nullptr);
	BOOST_CHECK_NE(cli.node->getNetworkID(), 0u);
	// The proxy adopted the server's nodeID (not the local m_nextNodeID).
	BOOST_CHECK_EQUAL(cli.node->getNetworkID(), srv.node->getNetworkID());
	// Buffered replicator replayed: client received the authority value.
	BOOST_CHECK_EQUAL(cli.rgravity, 42);

	// Further mutation propagates.
	srv.gravity = 99;
	pumpBoth(&srv, &cli, 40);
	BOOST_CHECK_EQUAL(cli.rgravity, 99);

	srv.Shutdown();
	cli.Shutdown();
}

// 2. A unique authority node's sendEvent(AUTH_2_ALL) reaches the proxy via
// getNextEvent. Validates that m_peerRole routing now works for unique nodes
// (previously the role was never recorded so routeNodeEvent dropped the event).
BOOST_AUTO_TEST_CASE(unique_event_reaches_proxy) {
	g_currentControl = nullptr;
	int port = s_port++;

	class ESrv : public ZCom_Control {
	  public:
		uint32_t cls;
		ZCom_Node *node = nullptr;
		~ESrv() { delete node; }
		ESrv(int p) {
			g_currentControl = this;
			ZCom_initSockets(true, p, 2, 0);
			cls = ZCom_registerClass("UQ2", 0);
		}
		bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override { return true; }
		void ZCom_cbConnectionSpawned(uint32_t) override {}
	} srv(port);

	class ECli : public ZCom_Control {
	  public:
		uint32_t cls;
		bool connected = false;
		ZCom_Node *node = nullptr;
		~ECli() { delete node; }
		ECli(int p) {
			g_currentControl = this;
			ZCom_initSockets(false, 0, 0, 0);
			cls = ZCom_registerClass("UQ2", 0);
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
	} cli(port);

	g_currentControl = &srv;
	srv.node = new ZCom_Node();
	srv.node->setEventNotification(true, false);
	srv.node->registerNodeUnique(srv.cls, eZCom_RoleAuthority, &srv);
	g_currentControl = nullptr;

	pumpBoth(&srv, &cli, 10); // consume initial state with no peers

	cli.ConnectTo("127.0.0.1", port);
	pumpBoth(&srv, &cli, 60);
	BOOST_REQUIRE(cli.connected);

	// Register the proxy after connect (deferred).
	g_currentControl = &cli;
	cli.node = new ZCom_Node();
	cli.node->registerNodeUnique(cli.cls, eZCom_RoleProxy, &cli);
	g_currentControl = nullptr;
	pumpBoth(&srv, &cli, 40);
	BOOST_REQUIRE(cli.node != nullptr);

	// Server sends a user event to all (AUTH_2_ALL).
	ZCom_BitStream ev;
	ev.addInt(0xBEEF, 16);
	srv.node->sendEvent(eZCom_ReliableOrdered, ZCOM_REPRULE_AUTH_2_ALL, &ev);
	pumpBoth(&srv, &cli, 40);

	bool gotIt = false;
	while (cli.node->checkEventWaiting()) {
		eZCom_Event type;
		eZCom_NodeRole role;
		uint32_t connID;
		auto data = cli.node->getNextEvent(&type, &role, &connID);
		if (type == eZCom_EventUser && data) {
			gotIt = true;
			BOOST_CHECK_EQUAL(data->getInt(16), 0xBEEF);
		}
	}
	BOOST_CHECK(gotIt);

	srv.Shutdown();
	cli.Shutdown();
}

// 3. eZCom_EventInit fires on the authority unique node once per newly
// connecting peer (the connect-spawn loop now pushes it for unique nodes;
// previously the `if (node->isUnique()) return;` skipped it entirely).
BOOST_AUTO_TEST_CASE(unique_event_init_fires_per_peer) {
	g_currentControl = nullptr;
	int port = s_port++;

	class ISrv : public ZCom_Control {
	  public:
		uint32_t cls;
		ZCom_Node *node = nullptr;
		int initEvents = 0;
		std::vector<uint32_t> initConnIDs;
		~ISrv() { delete node; }
		ISrv(int p) {
			g_currentControl = this;
			ZCom_initSockets(true, p, 4, 0); // room for multiple clients
			cls = ZCom_registerClass("UQ3", 0);
		}
		bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override { return true; }
		void ZCom_cbConnectionSpawned(uint32_t) override {}
		void drain() {
			if (!node)
				return;
			while (node->checkEventWaiting()) {
				eZCom_Event type;
				eZCom_NodeRole role;
				uint32_t connID;
				(void)node->getNextEvent(&type, &role, &connID);
				if (type == eZCom_EventInit) {
					initEvents++;
					initConnIDs.push_back(connID);
				}
			}
		}
	} srv(port);

	g_currentControl = &srv;
	srv.node = new ZCom_Node();
	srv.node->setEventNotification(true, false);
	srv.node->registerNodeUnique(srv.cls, eZCom_RoleAuthority, &srv);
	g_currentControl = nullptr;

	auto makeClient = [port](int /*i*/) {
		class ICli : public ZCom_Control {
		  public:
			uint32_t cls;
			bool connected = false;
			ZCom_Node *node = nullptr;
			~ICli() { delete node; }
			ICli(int p) {
				g_currentControl = this;
				ZCom_initSockets(false, 0, 0, 0);
				cls = ZCom_registerClass("UQ3", 0);
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
		};
		return ICli(port);
	};

	// First client connects.
	auto cli1 = makeClient(1);
	cli1.ConnectTo("127.0.0.1", port);
	pumpBoth(&srv, &cli1, 60);
	BOOST_REQUIRE(cli1.connected);
	// Register the proxy so the link is established (not strictly required for
	// the server-side init event, but mirrors the real flow).
	g_currentControl = &cli1;
	cli1.node = new ZCom_Node();
	cli1.node->registerNodeUnique(cli1.cls, eZCom_RoleProxy, &cli1);
	g_currentControl = nullptr;
	pumpBoth(&srv, &cli1, 20);
	g_currentControl = &srv;
	srv.drain();
	g_currentControl = nullptr;
	BOOST_CHECK_EQUAL(srv.initEvents, 1);

	// Second client connects: exactly one more init event for the new peer.
	auto cli2 = makeClient(2);
	cli2.ConnectTo("127.0.0.1", port);
	pumpBoth(&srv, &cli2, 60);
	BOOST_REQUIRE(cli2.connected);
	g_currentControl = &cli2;
	cli2.node = new ZCom_Node();
	cli2.node->registerNodeUnique(cli2.cls, eZCom_RoleProxy, &cli2);
	g_currentControl = nullptr;
	pumpBoth(&srv, &cli2, 20);
	g_currentControl = &srv;
	srv.drain();
	g_currentControl = nullptr;
	BOOST_CHECK_EQUAL(srv.initEvents, 2);
	// The two init events targeted distinct connection IDs.
	BOOST_REQUIRE_EQUAL(srv.initConnIDs.size(), 2u);
	BOOST_CHECK_NE(srv.initConnIDs[0], srv.initConnIDs[1]);

	srv.Shutdown();
	cli1.Shutdown();
	cli2.Shutdown();
}

// 4. Reverse order: the client registers its unique proxy BEFORE the announce
// arrives. When the announce comes in, linkUniqueNode re-keys the existing
// proxy to the server nodeID and replays buffered state.
BOOST_AUTO_TEST_CASE(unique_proxy_before_announce_is_rekeyed) {
	g_currentControl = nullptr;
	int port = s_port++;

	class RSrv : public ZCom_Control {
	  public:
		uint32_t cls;
		ZCom_Node *node = nullptr;
		int32_t val = 7;
		~RSrv() { delete node; }
		RSrv(int p) {
			g_currentControl = this;
			ZCom_initSockets(true, p, 2, 0);
			cls = ZCom_registerClass("UQ4", 0);
		}
		bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override { return true; }
		void ZCom_cbConnectionSpawned(uint32_t) override {}
	} srv(port);

	class RCli : public ZCom_Control {
	  public:
		uint32_t cls;
		bool connected = false;
		ZCom_Node *node = nullptr;
		int32_t rval = 0;
		~RCli() { delete node; }
		RCli(int p) {
			g_currentControl = this;
			ZCom_initSockets(false, 0, 0, 0);
			cls = ZCom_registerClass("UQ4", 0);
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
	} cli(port);

	g_currentControl = &srv;
	srv.node = new ZCom_Node();
	srv.node->beginReplicationSetup(1);
	srv.node->addReplicationInt((zS32 *)&srv.val, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL, 0);
	srv.node->endReplicationSetup();
	srv.node->registerNodeUnique(srv.cls, eZCom_RoleAuthority, &srv);
	g_currentControl = nullptr;

	// Register the client proxy FIRST (before connecting/pumping), so it gets
	// a local m_nextNodeID. Bump the client's counter with a throwaway unique
	// node first so the proxy's local ID differs from the server's nodeID
	// (both controls start m_nextNodeID at 1); this makes the re-key a real
	// re-key rather than a coincidental no-op.
	uint32_t throwClass = cli.ZCom_registerClass("UQ4_dummy", 0);
	g_currentControl = &cli;
	{
		ZCom_Node *dummy = new ZCom_Node();
		dummy->registerNodeUnique(throwClass, eZCom_RoleProxy, &cli);
		dummy->unregisterNode();
		delete dummy;
	}
	cli.node = new ZCom_Node();
	cli.node->beginReplicationSetup(1);
	cli.node->addReplicationInt((zS32 *)&cli.rval, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL, 0);
	cli.node->endReplicationSetup();
	cli.node->registerNodeUnique(cli.cls, eZCom_RoleProxy, &cli);
	uint32_t localIDBefore = cli.node->getNetworkID();
	g_currentControl = nullptr;
	BOOST_REQUIRE_NE(localIDBefore, 0u);

	cli.ConnectTo("127.0.0.1", port);
	pumpBoth(&srv, &cli, 80);
	BOOST_REQUIRE(cli.connected);

	// The proxy must have been re-keyed to the server's nodeID.
	BOOST_REQUIRE(cli.node != nullptr);
	BOOST_CHECK_EQUAL(cli.node->getNetworkID(), srv.node->getNetworkID());
	BOOST_CHECK_NE(cli.node->getNetworkID(), localIDBefore);
	BOOST_CHECK_EQUAL(cli.rval, 7);

	srv.val = 123;
	pumpBoth(&srv, &cli, 40);
	BOOST_CHECK_EQUAL(cli.rval, 123);

	srv.Shutdown();
	cli.Shutdown();
}

// 5. Updater pattern: a unique node at zoid level 2 replicates and round-trips
// an event after the client enables zoid level 2 (proxy registered inside
// ZCom_cbZoidResult, mirroring Client::ZCom_cbZoidResult case 2).
BOOST_AUTO_TEST_CASE(updater_zoid2_replicates_and_events) {
	g_currentControl = nullptr;
	int port = s_port++;

	class ZSrv : public ZCom_Control {
	  public:
		uint32_t cls;
		ZCom_Node *node = nullptr;
		int32_t level = 5;
		~ZSrv() { delete node; }
		ZSrv(int p) {
			g_currentControl = this;
			ZCom_initSockets(true, p, 2, 0);
			cls = ZCom_registerClass("UQ5", 0);
		}
		bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override { return true; }
		void ZCom_cbConnectionSpawned(uint32_t) override {}
		bool ZCom_cbZoidRequest(uint32_t, uint8_t, ZCom_BitStream &) override { return true; }
	} srv(port);

	class ZCli : public ZCom_Control {
	  public:
		uint32_t cls;
		bool connected = false;
		ZCom_Node *node = nullptr;
		int32_t rlevel = 0;
		bool zoidEnabled = false;
		~ZCli() { delete node; }
		ZCli(int p) {
			g_currentControl = this;
			ZCom_initSockets(false, 0, 0, 0);
			cls = ZCom_registerClass("UQ5", 0);
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
		// Mirrors Client::ZCom_cbZoidResult case 2: register the unique proxy
		// when zoid level 2 is granted.
		void ZCom_cbZoidResult(uint32_t, eZCom_ZoidResult result, uint8_t new_level, ZCom_BitStream &) override {
			if (result != eZCom_ZoidEnabled)
				return;
			if (new_level == 2) {
				zoidEnabled = true;
				node = new ZCom_Node();
				node->beginReplicationSetup(1);
				node->addReplicationInt((zS32 *)&rlevel, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL,
										0);
				node->endReplicationSetup();
				node->registerNodeUnique(cls, eZCom_RoleProxy, this);
			}
		}
	} cli(port);

	g_currentControl = &srv;
	srv.node = new ZCom_Node();
	srv.node->setEventNotification(true, false);
	srv.node->beginReplicationSetup(1);
	srv.node->addReplicationInt((zS32 *)&srv.level, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL, 0);
	srv.node->endReplicationSetup();
	srv.node->registerNodeUnique(srv.cls, eZCom_RoleAuthority, &srv);
	srv.node->applyForZoidLevel(2);
	g_currentControl = nullptr;

	pumpBoth(&srv, &cli, 10);

	cli.ConnectTo("127.0.0.1", port);
	pumpBoth(&srv, &cli, 60);
	BOOST_REQUIRE(cli.connected);

	// Client requests zoid level 2; the proxy is registered in cbZoidResult.
	// Recover the connection id from the peer map (single peer).
	uint32_t connID = 0;
	for (auto &pr : cli.getPeers())
		connID = pr.first;
	BOOST_REQUIRE_NE(connID, 0u);
	g_currentControl = &cli;
	ZCom_requestZoidMode(connID, 2);
	g_currentControl = nullptr;
	pumpBoth(&srv, &cli, 60);
	BOOST_REQUIRE(cli.zoidEnabled);
	BOOST_REQUIRE(cli.node != nullptr);
	BOOST_CHECK_EQUAL(cli.node->getNetworkID(), srv.node->getNetworkID());
	BOOST_CHECK_EQUAL(cli.rlevel, 5);

	// Round-trip an event at zoid level 2.
	ZCom_BitStream ev;
	ev.addInt(0xF00D, 16);
	srv.node->sendEvent(eZCom_ReliableOrdered, ZCOM_REPRULE_AUTH_2_ALL, &ev);
	pumpBoth(&srv, &cli, 40);

	bool gotIt = false;
	while (cli.node->checkEventWaiting()) {
		eZCom_Event type;
		eZCom_NodeRole role;
		uint32_t cID;
		auto data = cli.node->getNextEvent(&type, &role, &cID);
		if (type == eZCom_EventUser && data) {
			gotIt = true;
			BOOST_CHECK_EQUAL(data->getInt(16), 0xF00D);
		}
	}
	BOOST_CHECK(gotIt);

	srv.Shutdown();
	cli.Shutdown();
}

// 6. Terrain-destruction-via-unique-node: mirrors the Game node, which carries
// both eHole (live destruction, AUTH_2_ALL) and eTerrainSnapshot (initial state,
// sendEventDirect on eZCom_EventInit) on the SAME unique node. The proxy is
// registered after connect (deferred, like Client::ZCom_cbZoidResult case 1).
// Validates that initial (snapshot) AND ongoing (eHole) destruction replicate
// through the unique node — the path NetLevel used to fill as a dynamic node.
BOOST_AUTO_TEST_CASE(terrain_destruction_via_unique_node) {
	g_currentControl = nullptr;
	int port = s_port++;

	// Event kinds on the (single) unique node, mirroring Game::NetEvents.
	enum : int { kHole = 0, kSnapshot, kEventCount };

	class TSrv : public ZCom_Control {
	  public:
		uint32_t cls;
		ZCom_Node *node = nullptr;
		int snapshotsSent = 0;
		~TSrv() { delete node; }
		TSrv(int p) {
			g_currentControl = this;
			ZCom_initSockets(true, p, 2, 0);
			cls = ZCom_registerClass("UQ6", 0);
		}
		bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override { return true; }
		void ZCom_cbConnectionSpawned(uint32_t) override {}
		void sendHole(uint8_t payload) {
			ZCom_BitStream ev;
			ev.addInt(kHole, 4);
			ev.addInt(payload, 8);
			node->sendEvent(eZCom_ReliableOrdered, ZCOM_REPRULE_AUTH_2_ALL, &ev);
		}
		void drain() {
			if (!node)
				return;
			while (node->checkEventWaiting()) {
				eZCom_Event t;
				eZCom_NodeRole r;
				uint32_t c;
				auto d = node->getNextEvent(&t, &r, &c);
				if (t == eZCom_EventInit) {
					ZCom_BitStream snap;
					snap.addInt(kSnapshot, 4);
					snap.addInt(0x5A, 8);
					node->sendEventDirect(eZCom_ReliableOrdered, &snap, c);
					++snapshotsSent;
				}
				(void)d;
			}
		}
	} srv(port);

	class TCli : public ZCom_Control {
	  public:
		uint32_t cls;
		bool connected = false;
		ZCom_Node *node = nullptr;
		int snapshotPayloads = 0;
		int holePayloads = 0;
		uint8_t lastHole = 0;
		~TCli() { delete node; }
		TCli(int p) {
			g_currentControl = this;
			ZCom_initSockets(false, 0, 0, 0);
			cls = ZCom_registerClass("UQ6", 0);
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
		void drain() {
			if (!node)
				return;
			while (node->checkEventWaiting()) {
				eZCom_Event t;
				eZCom_NodeRole r;
				uint32_t c;
				auto d = node->getNextEvent(&t, &r, &c);
				if (t == eZCom_EventUser && d) {
					int ev = d->getInt(4);
					if (ev == kSnapshot)
						++snapshotPayloads;
					else if (ev == kHole) {
						++holePayloads;
						lastHole = (uint8_t)d->getInt(8);
					}
				}
			}
		}
	} cli(port);

	g_currentControl = &srv;
	srv.node = new ZCom_Node();
	srv.node->setEventNotification(true, false);
	srv.node->registerNodeUnique(srv.cls, eZCom_RoleAuthority, &srv);
	g_currentControl = nullptr;

	// Consume initial state with no peers (mirrors host starting before join).
	for (int i = 0; i < 10; ++i) {
		g_currentControl = &srv;
		srv.ZCom_processInput(eZCom_NoBlock);
		srv.ZCom_processOutput();
		srv.drain();
	}
	g_currentControl = nullptr;

	cli.ConnectTo("127.0.0.1", port);
	// Pump with draining so the authority's EventInit-driven snapshot traverses.
	for (int i = 0; i < 60; ++i) {
		g_currentControl = &srv;
		srv.ZCom_processInput(eZCom_NoBlock);
		srv.ZCom_processOutput();
		srv.drain();
		g_currentControl = &cli;
		cli.ZCom_processInput(eZCom_NoBlock);
		cli.ZCom_processOutput();
		cli.drain();
	}
	g_currentControl = nullptr;
	BOOST_REQUIRE(cli.connected);
	BOOST_CHECK_GT(srv.snapshotsSent, 0);

	// Register the proxy AFTER connect (deferred, like cbZoidResult case 1).
	g_currentControl = &cli;
	cli.node = new ZCom_Node();
	cli.node->registerNodeUnique(cli.cls, eZCom_RoleProxy, &cli);
	g_currentControl = nullptr;
	for (int i = 0; i < 40; ++i) {
		g_currentControl = &srv;
		srv.ZCom_processInput(eZCom_NoBlock);
		srv.ZCom_processOutput();
		srv.drain();
		g_currentControl = &cli;
		cli.ZCom_processInput(eZCom_NoBlock);
		cli.ZCom_processOutput();
		cli.drain();
	}
	g_currentControl = nullptr;
	BOOST_REQUIRE(cli.node != nullptr);

	// Initial: the buffered snapshot replayed onto the now-linked proxy.
	BOOST_CHECK_EQUAL(cli.snapshotPayloads, 1);

	// Ongoing: a live eHole broadcast reaches the proxy.
	srv.sendHole(0xA1);
	for (int i = 0; i < 40; ++i) {
		g_currentControl = &srv;
		srv.ZCom_processInput(eZCom_NoBlock);
		srv.ZCom_processOutput();
		srv.drain();
		g_currentControl = &cli;
		cli.ZCom_processInput(eZCom_NoBlock);
		cli.ZCom_processOutput();
		cli.drain();
	}
	g_currentControl = nullptr;
	BOOST_CHECK_GT(cli.holePayloads, 0);
	BOOST_CHECK_EQUAL(cli.lastHole, 0xA1u);

	// A second ongoing hole also propagates (no freeze after the first).
	srv.sendHole(0xB2);
	for (int i = 0; i < 40; ++i) {
		g_currentControl = &srv;
		srv.ZCom_processInput(eZCom_NoBlock);
		srv.ZCom_processOutput();
		srv.drain();
		g_currentControl = &cli;
		cli.ZCom_processInput(eZCom_NoBlock);
		cli.ZCom_processOutput();
		cli.drain();
	}
	g_currentControl = nullptr;
	BOOST_CHECK_EQUAL(cli.lastHole, 0xB2u);
	BOOST_CHECK_EQUAL(cli.snapshotPayloads, 1); // no spurious extra snapshots

	srv.Shutdown();
	cli.Shutdown();
}

BOOST_AUTO_TEST_SUITE_END()