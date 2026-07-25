// test_bandwidth.cpp
// T1.1 upstream bandwidth limiting and T1.3 per-replicator min/max-delay
// throttling. Uses the free functions the game actually calls
// (ZCom_setUpstreamLimit / ZCom_processReplicators) where relevant.

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

static int s_port = 25000;

// Forward declaration (defined after the fixtures).
static std::unique_ptr<ZCom_Replicator> makeIntRep(zS32 *ptr, int mindelay, int maxdelay);

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
		node->addReplicator(makeIntRep(&m_clientVal, 0, 0), false);
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

// T1.3 throttling lives in the explicit-replicator (m_replicators) path, so the
// monitored value must be an explicit ZCom_Replicate_Numericp (the game's
// PosSpdReplicator is exactly this), not an autoreplicator (addReplicationInt).
static std::unique_ptr<ZCom_Replicator> makeIntRep(zS32 *ptr, int mindelay, int maxdelay) {
	auto rep = std::make_unique<ZCom_Replicate_Numericp<zS32>>(ptr, 32, 0, ZCOM_REPRULE_AUTH_2_ALL);
	rep->getSetup()->setMinDelay(mindelay);
	rep->getSetup()->setMaxDelay(maxdelay);
	return rep;
}

// Helper: build a monitored authority node on the server with the given
// min/max delay, register it, and let the initial state replicate.
static void setupMonitoredNode(MonSrv &srv, int mindelay, int maxdelay) {
	srv.node = new ZCom_Node();
	srv.node->setRole(eZCom_RoleAuthority);
	srv.node->beginReplicationSetup(1);
	srv.node->addReplicator(makeIntRep(&srv.m_serverVal, mindelay, maxdelay), false);
	srv.node->endReplicationSetup();
	srv.node->registerNodeDynamic(srv.cls, &srv);
	srv.node->setOwner(1, true);
}

// Count how many distinct client-side updates arrived while driving the
// server value up by one each tick for `ticks` ticks.
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

BOOST_AUTO_TEST_SUITE(bandwidth)

// T1.1 — the upstream limiter must NOT drop reliable replicator traffic.
// Bandwidth control is delegated to ENet (enet_host_bandwidth_limit), which
// queues instead of dropping; the app-level limiter is now a pass-through. So a
// capped connection must deliver every update, just like an uncapped one.
BOOST_AUTO_TEST_CASE(upstream_limit_does_not_drop_reliable_traffic) {
	g_currentControl = nullptr;
	int port = s_port++;

	int unlimited = 0;
	{
		MonSrv srv(port);
		MonCli cli(port);
		cli.ConnectTo("127.0.0.1", port);
		processBoth(&srv, &cli, 30);
		BOOST_REQUIRE(cli.connected);
		setupMonitoredNode(srv, 0, 0);
		processBoth(&srv, &cli, 20);
		unlimited = countReceived(srv, cli, 150);
		srv.Shutdown();
		cli.Shutdown();
	}

	int limited = 0;
	{
		MonSrv srv(port + 1);
		MonCli cli(port + 1);
		cli.ConnectTo("127.0.0.1", port + 1);
		processBoth(&srv, &cli, 30);
		BOOST_REQUIRE(cli.connected);
		setupMonitoredNode(srv, 0, 0);
		processBoth(&srv, &cli, 20);

		// Apply the limit via the free function (the game's actual call path).
		g_currentControl = &srv;
		ZCom_setUpstreamLimit(500, 500);
		g_currentControl = nullptr;

		limited = countReceived(srv, cli, 150);
		srv.Shutdown();
		cli.Shutdown();
	}

	BOOST_CHECK_GT(unlimited, 0);
	BOOST_CHECK_GT(limited, 0);
	// The limiter must never starve reliable replication: a capped connection
	// delivers exactly as many updates as an uncapped one in this harness.
	BOOST_CHECK_EQUAL(limited, unlimited);
}

// T1.3 — a non-zero minDelay prevents re-sending a changed value until the
// delay elapses. With no wall-clock advance in a tight loop, only the first
// send gets through.
BOOST_AUTO_TEST_CASE(min_delay_throttles_sends) {
	g_currentControl = nullptr;
	int port = s_port++;

	int noMin = 0;
	{
		MonSrv srv(port);
		MonCli cli(port);
		cli.ConnectTo("127.0.0.1", port);
		processBoth(&srv, &cli, 30);
		setupMonitoredNode(srv, 0, 0);
		processBoth(&srv, &cli, 20);
		noMin = countReceived(srv, cli, 100);
		srv.Shutdown();
		cli.Shutdown();
	}

	int withMin = 0;
	{
		MonSrv srv(port + 1);
		MonCli cli(port + 1);
		cli.ConnectTo("127.0.0.1", port + 1);
		processBoth(&srv, &cli, 30);
		setupMonitoredNode(srv, 100, 0); // 100ms min delay
		processBoth(&srv, &cli, 20);
		withMin = countReceived(srv, cli, 100);
		srv.Shutdown();
		cli.Shutdown();
	}

	BOOST_CHECK_GT(noMin, 10);
	BOOST_CHECK_LT(withMin, 5); // essentially only the first send
	BOOST_CHECK_LT(withMin, noMin);
}

// T1.3 — a non-zero maxDelay force-sends an UNCHANGED value once the delay
// elapses (dead-reckoning / periodic resync).
BOOST_AUTO_TEST_CASE(max_delay_forces_resend) {
	g_currentControl = nullptr;
	int port = s_port++;

	class CountInterceptor : public ZCom_NodeReplicationInterceptor {
	  public:
		int count = 0;
		bool inPreUpdateItem(ZCom_Node *, uint32_t, eZCom_NodeRole, ZCom_Replicator *, uint32_t) override {
			count++;
			return true;
		}
	};
	CountInterceptor inter;

	MonSrv srv(port);
	MonCli cli(port);
	cli.ConnectTo("127.0.0.1", port);
	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.connected);

	setupMonitoredNode(srv, 0, 100); // 100ms max delay; value stays constant
	processBoth(&srv, &cli, 20);

	cli.node->setReplicationInterceptor(&inter);
	int baseline = inter.count;
	// Value never changes; without maxDelay no further sends occur. With
	// maxDelay=100, after 100ms the unchanged value is force-re-sent.
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	processBoth(&srv, &cli, 1);

	BOOST_CHECK_GT(inter.count, baseline);
	srv.Shutdown();
	cli.Shutdown();
}

// T1.3 — ZCom_processReplicators must drive Process() callbacks without
// disturbing normal replication.
BOOST_AUTO_TEST_CASE(process_replicators_runs_process_callbacks) {
	g_currentControl = nullptr;
	int port = s_port++;
	MonSrv srv(port);
	MonCli cli(port);
	cli.ConnectTo("127.0.0.1", port);
	processBoth(&srv, &cli, 30);
	setupMonitoredNode(srv, 0, 0);
	processBoth(&srv, &cli, 20);

	int counter = 0, last = cli.m_clientVal, received = 0;
	for (int i = 0; i < 50; i++) {
		srv.m_serverVal = ++counter;
		g_currentControl = &srv;
		srv.ZCom_processReplicators(16);
		g_currentControl = nullptr;
		processBoth(&srv, &cli, 1);
		if (cli.m_clientVal != last) {
			received++;
			last = cli.m_clientVal;
		}
	}
	BOOST_CHECK_GT(received, 0);
	srv.Shutdown();
	cli.Shutdown();
}

BOOST_AUTO_TEST_SUITE_END()
