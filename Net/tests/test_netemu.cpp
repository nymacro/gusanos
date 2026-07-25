// test_netemu.cpp
// T1.2 — ZCom_simulateLag / ZCom_simulateLoss emulation. Exercises the free
// functions the game calls (ZCom_simulateLag / ZCom_simulateLoss), which route
// through g_currentControl into ZCom_Control's per-connection emulation state.

#include <boost/test/unit_test.hpp>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <chrono>

#include "net_control.h"
#include "net_node.h"
#include "net_replicator.h"

static int s_port = 26000;

class MonSrv : public ZCom_Control {
  public:
	uint32_t cls;
	int spawned = 0;
	int m_clientConnID = 0;
	int32_t m_serverVal = 0;
	ZCom_Node *node = nullptr;
	~MonSrv() {
		delete node;
		node = nullptr;
	}
	MonSrv(int port) {
		g_currentControl = this;
		ZCom_initSockets(true, port, 2, 0);
		cls = ZCom_registerClass("Mon", 0);
	}
	bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream &, ZCom_BitStream &) override {
		return true;
	}
	void ZCom_cbConnectionSpawned(uint32_t id) override {
		spawned++;
		m_clientConnID = (int)id;
	}
};

class MonCli : public ZCom_Control {
  public:
	uint32_t cls;
	bool connected = false;
	int32_t m_clientVal = 0;
	ZCom_Node *node = nullptr;
	~MonCli() {
		delete node;
		node = nullptr;
	}
	MonCli(int port) {
		g_currentControl = this;
		ZCom_initSockets(false, 0, 0, 0);
		cls = ZCom_registerClass("Mon", 0);
	}
	uint32_t ConnectTo(const char *host, int port) {
		ZCom_Address a;
		char hp[64];
		snprintf(hp, sizeof hp, "%s:%d", host, port);
		a.setAddress(ZCom_Control::eZCom_AddressUDP, 0, hp);
		return ZCom_Connect(a, nullptr);
	}
	void ZCom_cbConnectResult(uint32_t, eZCom_ConnectResult r, ZCom_BitStream &) override {
		connected = (r == eZCom_ConnAccepted);
	}
	void ZCom_cbNodeRequest_Dynamic(uint32_t, uint32_t, ZCom_BitStream *, int, uint32_t) override {
		node = new ZCom_Node();
		node->setEventNotification(true, true);
		node->beginReplicationSetup(1);
		node->addReplicationInt((zS32 *)&m_clientVal, 32, false, 0, ZCOM_REPRULE_AUTH_2_ALL, 0, 0);
		node->endReplicationSetup();
		node->registerRequestedNode(cls, this);
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

static void setupMonitoredNode(MonSrv &srv) {
	srv.node = new ZCom_Node();
	srv.node->setRole(eZCom_RoleAuthority);
	srv.node->beginReplicationSetup(1);
	srv.node->addReplicationInt((zS32 *)&srv.m_serverVal, 32, false, 0, ZCOM_REPRULE_AUTH_2_ALL, 0, 0);
	srv.node->endReplicationSetup();
	srv.node->registerNodeDynamic(srv.cls, &srv);
	srv.node->setOwner(1, true);
}

static int countReceived(MonSrv &srv, MonCli &cli, int ticks) {
	int counter = 0, last = cli.m_clientVal, received = 0;
	for (int i = 0; i < ticks; i++) {
		srv.m_serverVal = ++counter;
		processBoth(&srv, &cli, 1);
		if (cli.m_clientVal != last) {
			received++;
			last = cli.m_clientVal;
		}
	}
	return received;
}

BOOST_AUTO_TEST_SUITE(netemu)

// T1.2 — simulateLoss drops ~lossPct of non-critical packets.
BOOST_AUTO_TEST_CASE(loss_drops_fraction_of_packets) {
	g_currentControl = nullptr;
	int port = s_port++;
	MonSrv srv(port);
	MonCli cli(port);
	cli.ConnectTo("127.0.0.1", port);
	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.connected);
	setupMonitoredNode(srv);
	processBoth(&srv, &cli, 20);

	std::srand(42); // deterministic loss sequence
	g_currentControl = &srv;
	ZCom_simulateLoss((int)srv.m_clientConnID, 0.5f);
	g_currentControl = nullptr;

	const int N = 300;
	int received = countReceived(srv, cli, N);
	// ~50% should be dropped; allow wide tolerance for RNG variance.
	BOOST_CHECK_GT(received, (int)(N * 0.30));
	BOOST_CHECK_LT(received, (int)(N * 0.70));
	srv.Shutdown();
	cli.Shutdown();
}

// T1.2 — simulateLag defers delivery by ~lag ms.
BOOST_AUTO_TEST_CASE(lag_delays_delivery) {
	g_currentControl = nullptr;
	int port = s_port++;
	MonSrv srv(port);
	MonCli cli(port);
	cli.ConnectTo("127.0.0.1", port);
	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.connected);
	setupMonitoredNode(srv);
	processBoth(&srv, &cli, 20);

	g_currentControl = &srv;
	ZCom_simulateLag((int)srv.m_clientConnID, 60); // 60ms lag
	g_currentControl = nullptr;

	// Change to a unique value and run one tick; the packet is deferred.
	srv.m_serverVal = 777;
	processBoth(&srv, &cli, 1);
	// With no time elapsed it must NOT have arrived yet.
	BOOST_CHECK_NE(cli.m_clientVal, 777);

	// After the lag window it should arrive.
	std::this_thread::sleep_for(std::chrono::milliseconds(120));
	processBoth(&srv, &cli, 1);
	BOOST_CHECK_EQUAL(cli.m_clientVal, 777);
	srv.Shutdown();
	cli.Shutdown();
}

BOOST_AUTO_TEST_SUITE_END()
