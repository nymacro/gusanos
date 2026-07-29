// test_routing_d2.cpp
// Phase D2: per-connection reprule routing in sendEvent.
// Verifies that node events are routed to peers based on each peer's role for
// the node and the event's ZCOM_REPRULE_* flags, instead of broadcasting.
//   - AUTH_2_PROXY event  reaches proxy only
//   - AUTH_2_OWNER event  reaches owner only
//   - AUTH_2_ALL event    reaches both
//   - OWNER_2_AUTH event  reaches authority only (owner -> server)
//   - rule==0 event       broadcasts to all (legacy regression)

#include <boost/test/unit_test.hpp>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "net_control.h"
#include "net_node.h"
#include "net_replicator.h"

static int s_d2_port = 25200;

struct EvSrv : public ZCom_Control {
	uint32_t cls = 0;
	ZCom_Node *node = nullptr;
	int spawns = 0;
	std::vector<uint32_t> spawnedIDs;
	int eventCount = 0;
	uint32_t lastPayload = 0;
	~EvSrv() {
		delete node;
		node = nullptr;
	}
	EvSrv(int port) {
		g_currentControl = this;
		ZCom_initSockets(true, port, 4, 0);
		cls = ZCom_registerClass("E", 0);
	}
	bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override {
		return true;
	}
	void ZCom_cbConnectionSpawned(uint32_t id) override {
		spawnedIDs.push_back(id);
		spawns++;
	}
	// Drain events queued on the authority node (receiver for OWNER_2_AUTH).
	void drainEvents() {
		if (!node)
			return;
		while (node->checkEventWaiting()) {
			eZCom_Event t;
			eZCom_NodeRole r;
			uint32_t c;
			auto d = node->getNextEvent(&t, &r, &c);
			if (t == eZCom_EventUser && d) {
				eventCount++;
				lastPayload = d->getInt(8);
			}
		}
	}
};

struct EvCli : public ZCom_Control {
	uint32_t cls = 0;
	bool connected = false;
	ZCom_Node *node = nullptr;
	uint32_t gotID = 0;
	int eventCount = 0;
	uint32_t lastPayload = 0;
	~EvCli() {
		delete node;
		node = nullptr;
	}
	EvCli(int port) {
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
		(void)role;
	}
	void drainEvents() {
		if (!node)
			return;
		while (node->checkEventWaiting()) {
			eZCom_Event t;
			eZCom_NodeRole r;
			uint32_t c;
			auto d = node->getNextEvent(&t, &r, &c);
			if (t == eZCom_EventUser && d) {
				eventCount++;
				lastPayload = d->getInt(8);
			}
		}
	}
};

static void process3(ZCom_Control *s, ZCom_Control *c1, ZCom_Control *c2, int n) {
	for (int i = 0; i < n; ++i) {
		g_currentControl = s;
		s->ZCom_processInput(eZCom_NoBlock);
		s->ZCom_processOutput();
		g_currentControl = c1;
		c1->ZCom_processInput(eZCom_NoBlock);
		c1->ZCom_processOutput();
		g_currentControl = c2;
		c2->ZCom_processInput(eZCom_NoBlock);
		c2->ZCom_processOutput();
	}
	g_currentControl = nullptr;
}

static void process2(ZCom_Control *s, ZCom_Control *c, int n) {
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

// Set up a server authority node owned by client1 (first spawned). No
// replicators — events only. Announces to both clients.
static void buildOwnerProxyNode(EvSrv &srv) {
	srv.node = new ZCom_Node();
	srv.node->setRole(eZCom_RoleAuthority);
	srv.node->setEventNotification(true, true);
	srv.node->registerNodeDynamic(srv.cls, &srv);
	srv.node->setOwner(srv.spawnedIDs[0], true);
}

BOOST_AUTO_TEST_SUITE(routing_d2)

BOOST_AUTO_TEST_CASE(event_auth_2_proxy_reaches_proxy_only) {
	g_currentControl = nullptr;
	int port = s_d2_port++;
	EvSrv srv(port);
	EvCli c1(port), c2(port);
	c1.ConnectTo("127.0.0.1", port);
	process3(&srv, &c1, &c2, 40);
	BOOST_REQUIRE(c1.connected);
	c2.ConnectTo("127.0.0.1", port);
	process3(&srv, &c1, &c2, 40);
	BOOST_REQUIRE(c2.connected);
	BOOST_REQUIRE_EQUAL(srv.spawnedIDs.size(), 2u);

	buildOwnerProxyNode(srv);
	process3(&srv, &c1, &c2, 40);
	BOOST_REQUIRE(c1.node && c2.node);

	ZCom_BitStream ev;
	ev.addInt(0xA1, 8);
	srv.node->sendEvent(eZCom_ReliableOrdered, ZCOM_REPRULE_AUTH_2_PROXY, &ev);
	process3(&srv, &c1, &c2, 40);
	c1.drainEvents();
	c2.drainEvents();

	BOOST_CHECK_GT(c2.eventCount, 0); // proxy received
	BOOST_CHECK_EQUAL(c2.lastPayload, 0xA1u);
	BOOST_CHECK_EQUAL(c1.eventCount, 0); // owner did not receive

	srv.Shutdown();
	c1.Shutdown();
	c2.Shutdown();
}

BOOST_AUTO_TEST_CASE(event_auth_2_owner_reaches_owner_only) {
	g_currentControl = nullptr;
	int port = s_d2_port++;
	EvSrv srv(port);
	EvCli c1(port), c2(port);
	c1.ConnectTo("127.0.0.1", port);
	process3(&srv, &c1, &c2, 40);
	BOOST_REQUIRE(c1.connected);
	c2.ConnectTo("127.0.0.1", port);
	process3(&srv, &c1, &c2, 40);
	BOOST_REQUIRE(c2.connected);
	BOOST_REQUIRE_EQUAL(srv.spawnedIDs.size(), 2u);

	buildOwnerProxyNode(srv);
	process3(&srv, &c1, &c2, 40);
	BOOST_REQUIRE(c1.node && c2.node);

	ZCom_BitStream ev;
	ev.addInt(0xB2, 8);
	srv.node->sendEvent(eZCom_ReliableOrdered, ZCOM_REPRULE_AUTH_2_OWNER, &ev);
	process3(&srv, &c1, &c2, 40);
	c1.drainEvents();
	c2.drainEvents();

	BOOST_CHECK_GT(c1.eventCount, 0); // owner received
	BOOST_CHECK_EQUAL(c1.lastPayload, 0xB2u);
	BOOST_CHECK_EQUAL(c2.eventCount, 0); // proxy did not receive

	srv.Shutdown();
	c1.Shutdown();
	c2.Shutdown();
}

BOOST_AUTO_TEST_CASE(event_auth_2_all_reaches_both) {
	g_currentControl = nullptr;
	int port = s_d2_port++;
	EvSrv srv(port);
	EvCli c1(port), c2(port);
	c1.ConnectTo("127.0.0.1", port);
	process3(&srv, &c1, &c2, 40);
	BOOST_REQUIRE(c1.connected);
	c2.ConnectTo("127.0.0.1", port);
	process3(&srv, &c1, &c2, 40);
	BOOST_REQUIRE(c2.connected);
	BOOST_REQUIRE_EQUAL(srv.spawnedIDs.size(), 2u);

	buildOwnerProxyNode(srv);
	process3(&srv, &c1, &c2, 40);
	BOOST_REQUIRE(c1.node && c2.node);

	ZCom_BitStream ev;
	ev.addInt(0xC3, 8);
	srv.node->sendEvent(eZCom_ReliableOrdered, ZCOM_REPRULE_AUTH_2_ALL, &ev);
	process3(&srv, &c1, &c2, 40);
	c1.drainEvents();
	c2.drainEvents();

	BOOST_CHECK_GT(c1.eventCount, 0); // owner received
	BOOST_CHECK_EQUAL(c1.lastPayload, 0xC3u);
	BOOST_CHECK_GT(c2.eventCount, 0); // proxy received
	BOOST_CHECK_EQUAL(c2.lastPayload, 0xC3u);

	srv.Shutdown();
	c1.Shutdown();
	c2.Shutdown();
}

BOOST_AUTO_TEST_CASE(event_owner_2_auth_reaches_authority) {
	g_currentControl = nullptr;
	int port = s_d2_port++;
	EvSrv srv(port);
	EvCli c1(port);
	c1.ConnectTo("127.0.0.1", port);
	process2(&srv, &c1, 40);
	BOOST_REQUIRE(c1.connected);
	BOOST_REQUIRE_EQUAL(srv.spawnedIDs.size(), 1u);

	srv.node = new ZCom_Node();
	srv.node->setRole(eZCom_RoleAuthority);
	srv.node->setEventNotification(true, true);
	srv.node->registerNodeDynamic(srv.cls, &srv);
	srv.node->setOwner(srv.spawnedIDs[0], true);
	process2(&srv, &c1, 40);
	BOOST_REQUIRE(c1.node != nullptr);
	BOOST_REQUIRE_EQUAL(c1.node->getRole(), eZCom_RoleOwner);

	ZCom_BitStream ev;
	ev.addInt(0xD4, 8);
	c1.node->sendEvent(eZCom_ReliableOrdered, ZCOM_REPRULE_OWNER_2_AUTH, &ev);
	process2(&srv, &c1, 40);
	srv.drainEvents();

	BOOST_CHECK_GT(srv.eventCount, 0); // authority received
	BOOST_CHECK_EQUAL(srv.lastPayload, 0xD4u);

	srv.Shutdown();
	c1.Shutdown();
}

// rule==0 must still broadcast to every peer (legacy semantics).
BOOST_AUTO_TEST_CASE(event_rule_zero_broadcasts_to_all) {
	g_currentControl = nullptr;
	int port = s_d2_port++;
	EvSrv srv(port);
	EvCli c1(port), c2(port);
	c1.ConnectTo("127.0.0.1", port);
	process3(&srv, &c1, &c2, 40);
	BOOST_REQUIRE(c1.connected);
	c2.ConnectTo("127.0.0.1", port);
	process3(&srv, &c1, &c2, 40);
	BOOST_REQUIRE(c2.connected);
	BOOST_REQUIRE_EQUAL(srv.spawnedIDs.size(), 2u);

	buildOwnerProxyNode(srv);
	process3(&srv, &c1, &c2, 40);
	BOOST_REQUIRE(c1.node && c2.node);

	ZCom_BitStream ev;
	ev.addInt(0xE5, 8);
	srv.node->sendEvent(eZCom_ReliableOrdered, 0, &ev);
	process3(&srv, &c1, &c2, 40);
	c1.drainEvents();
	c2.drainEvents();

	BOOST_CHECK_GT(c1.eventCount, 0); // owner received (broadcast)
	BOOST_CHECK_EQUAL(c1.lastPayload, 0xE5u);
	BOOST_CHECK_GT(c2.eventCount, 0); // proxy received (broadcast)
	BOOST_CHECK_EQUAL(c2.lastPayload, 0xE5u);

	srv.Shutdown();
	c1.Shutdown();
	c2.Shutdown();
}

BOOST_AUTO_TEST_SUITE_END()
