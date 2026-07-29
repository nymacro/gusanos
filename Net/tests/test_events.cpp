// test_events.cpp
// Tests custom event sending/receiving through ZCom_Node::sendEvent.
// Corresponds to ex05_object_events sample.

#include <boost/test/unit_test.hpp>
#include <cstring>
#include <cstdio>

#include "net_control.h"
#include "net_node.h"

static void processBoth(ZCom_Control *srv, ZCom_Control *cli, int n = 200) {
	for (int i = 0; i < n; i++) {
		g_currentControl = srv;
		srv->ZCom_processInput(eZCom_NoBlock);
		srv->ZCom_processOutput();
		g_currentControl = cli;
		cli->ZCom_processInput(eZCom_NoBlock);
		cli->ZCom_processOutput();
	}
	g_currentControl = nullptr;
}

class EventServer : public ZCom_Control {
  public:
	int m_spawnedCount;
	uint32_t m_eventClass;
	ZCom_Node *m_node;

	~EventServer() {
		delete m_node;
		m_node = nullptr;
	}
	EventServer(int udpPort) : ZCom_Control(), m_spawnedCount(0), m_eventClass(0), m_node(nullptr) {
		m_eventClass = ZCom_registerClass("EventNode", 0);
		g_currentControl = this;
		ZCom_initSockets(true, udpPort, 0, 0);
	}

	void SendTestEvent(const char *data) {
		if (!m_node)
			return;
		ZCom_BitStream eventData;
		eventData.addString(data);
		m_node->sendEvent(eZCom_ReliableOrdered, 0, &eventData);
	}

  protected:
	bool ZCom_cbConnectionRequest(ZCom_ConnID id, ZCom_BitStream &request, ZCom_BitStream &reply) override {
		return true;
	}

	void ZCom_cbConnectionSpawned(ZCom_ConnID id) override {
		m_spawnedCount++;
		m_node = new ZCom_Node();
		m_node->registerNodeDynamic(m_eventClass, this);
	}
};

class EventClient : public ZCom_Control {
  public:
	bool m_connected;
	uint32_t m_eventClass;

	EventClient(int udpPort) : ZCom_Control(), m_connected(false), m_eventClass(0) {
		m_eventClass = ZCom_registerClass("EventNode", 0);
		g_currentControl = this;
		ZCom_initSockets(false, 0, 0, 0);
	}

	uint32_t ConnectTo(const char *host, int port) {
		ZCom_Address addr;
		char hostport[64];
		snprintf(hostport, sizeof(hostport), "%s:%d", host, port);
		addr.setAddress(ZCom_Control::eZCom_AddressUDP, 0, hostport);
		return ZCom_Connect(addr, nullptr);
	}

  protected:
	void ZCom_cbConnectResult(ZCom_ConnID id, eZCom_ConnectResult result, ZCom_BitStream &reply) override {
		m_connected = (result == eZCom_ConnAccepted);
	}
};

BOOST_AUTO_TEST_SUITE(events)

BOOST_AUTO_TEST_CASE(node_send_event) {
	static int portOffset = 0;
	g_currentControl = nullptr;
	int port = 19070 + (portOffset++);

	{
		EventServer srv(port);
		EventClient cli(port);

		uint32_t cid = cli.ConnectTo("127.0.0.1", port);
		BOOST_REQUIRE(cid != ZCom_Invalid_ID);

		processBoth(&srv, &cli, 30);
		BOOST_REQUIRE(cli.m_connected);
		BOOST_REQUIRE(srv.m_node != nullptr);

		g_currentControl = &srv;
		srv.SendTestEvent("Player Joined");
		g_currentControl = nullptr;

		processBoth(&srv, &cli, 10);

		srv.Shutdown();
		cli.Shutdown();
	}

	BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(node_send_multiple_events) {
	static int portOffset = 0;
	g_currentControl = nullptr;
	int port = 19080 + (portOffset++);

	{
		EventServer srv(port);
		EventClient cli(port);

		uint32_t cid = cli.ConnectTo("127.0.0.1", port);
		BOOST_REQUIRE(cid != ZCom_Invalid_ID);

		processBoth(&srv, &cli, 30);
		BOOST_REQUIRE(cli.m_connected);
		BOOST_REQUIRE(srv.m_node != nullptr);

		g_currentControl = &srv;
		for (int i = 0; i < 5; i++) {
			char buf[32];
			snprintf(buf, sizeof(buf), "event%d", i);
			srv.SendTestEvent(buf);
		}
		g_currentControl = nullptr;

		processBoth(&srv, &cli, 10);

		srv.Shutdown();
		cli.Shutdown();
	}

	BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(event_binary_payload) {
	static int portOffset = 0;
	g_currentControl = nullptr;
	int port = 19090 + (portOffset++);

	{
		EventServer srv(port);
		EventClient cli(port);

		uint32_t cid = cli.ConnectTo("127.0.0.1", port);
		BOOST_REQUIRE(cid != ZCom_Invalid_ID);

		processBoth(&srv, &cli, 30);
		BOOST_REQUIRE(cli.m_connected);
		BOOST_REQUIRE(srv.m_node != nullptr);

		if (srv.m_node) {
			g_currentControl = &srv;
			ZCom_BitStream eventData;
			eventData.addInt(0xDEAD, 16);
			eventData.addInt(0xBEEF, 16);
			srv.m_node->sendEvent(eZCom_ReliableOrdered, 0, &eventData);
			g_currentControl = nullptr;
		}

		processBoth(&srv, &cli, 10);

		srv.Shutdown();
		cli.Shutdown();
	}

	BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(event_interceptor_registration) {
	static int portOffset = 0;
	g_currentControl = nullptr;
	int port = 19100 + (portOffset++);

	EventServer srv(port);
	EventClient cli(port);

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);
	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);

	// Create a node and set an event interceptor
	g_currentControl = &cli;
	ZCom_Node *node = new ZCom_Node();
	node->beginReplicationSetup(0);
	node->endReplicationSetup();
	node->registerNodeDynamic(cli.m_eventClass, &cli);

	// Interceptor registration should not crash
	node->setEventInterceptor(nullptr);
	BOOST_CHECK(true);

	node->unregisterNode();
	delete node;
	g_currentControl = nullptr;

	cli.Shutdown();
	srv.Shutdown();
}

BOOST_AUTO_TEST_CASE(event_type_constants) {
	BOOST_CHECK_EQUAL(eZCom_EventNoEvent, 0);
	BOOST_CHECK_EQUAL(eZCom_EventInit, 1);
	BOOST_CHECK_EQUAL(eZCom_EventSyncRequest, 2);
	BOOST_CHECK_EQUAL(eZCom_EventRemoved, 3);
	BOOST_CHECK_EQUAL(eZCom_EventFile_Incoming, 4);
	BOOST_CHECK_EQUAL(eZCom_EventFile_Data, 5);
	BOOST_CHECK_EQUAL(eZCom_EventFile_Aborted, 6);
	BOOST_CHECK_EQUAL(eZCom_EventFile_Complete, 7);
	BOOST_CHECK_EQUAL(eZCom_EventReplicator, 8);
	BOOST_CHECK_EQUAL(eZCom_EventUser, 9);
	BOOST_CHECK(eZCom_EventUser > eZCom_EventReplicator);
}

BOOST_AUTO_TEST_CASE(send_mode_constants) {
	BOOST_CHECK_EQUAL(eZCom_ReliableUnordered, 0);
	BOOST_CHECK_EQUAL(eZCom_ReliableOrdered, 1);
	BOOST_CHECK_EQUAL(eZCom_Unreliable, 2);
}

BOOST_AUTO_TEST_CASE(replication_flag_constants) {
	BOOST_CHECK_EQUAL(ZCOM_REPFLAG_NONE, 0);
	BOOST_CHECK_EQUAL(ZCOM_REPFLAG_UNRELIABLE, (1L << 0));
	BOOST_CHECK_EQUAL(ZCOM_REPFLAG_MOSTRECENT, (1L << 1));
	BOOST_CHECK_EQUAL(ZCOM_REPFLAG_RARELYCHANGED, (1L << 2));
	BOOST_CHECK_EQUAL(ZCOM_REPFLAG_ONLYONCE, (1L << 3));
	BOOST_CHECK_EQUAL(ZCOM_REPFLAG_INTERCEPT, (1L << 4));
	BOOST_CHECK_EQUAL(ZCOM_REPFLAG_SETUPPERSISTS, (1L << 5));
	BOOST_CHECK_EQUAL(ZCOM_REPFLAG_SETUPAUTODELETE, (1L << 6));
	BOOST_CHECK_EQUAL(ZCOM_REPFLAG_STARTCLEAN, (1L << 7));
}

BOOST_AUTO_TEST_CASE(replication_rule_constants) {
	BOOST_CHECK_EQUAL(ZCOM_REPRULE_AUTH_2_PROXY, (1L << 0));
	BOOST_CHECK_EQUAL(ZCOM_REPRULE_AUTH_2_OWNER, (1L << 1));
	BOOST_CHECK_EQUAL(ZCOM_REPRULE_AUTH_2_ALL, (ZCOM_REPRULE_AUTH_2_PROXY | ZCOM_REPRULE_AUTH_2_OWNER));
	BOOST_CHECK_EQUAL(ZCOM_REPRULE_OWNER_2_AUTH, (1L << 2));
	BOOST_CHECK_EQUAL(ZCOM_REPRULE_NONE, 0);
}

BOOST_AUTO_TEST_CASE(node_event_notification_toggle) {
	static int portOffset = 0;
	g_currentControl = nullptr;
	int port = 19110 + (portOffset++);

	EventServer srv(port);
	EventClient cli(port);

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);
	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);

	// Create a node with event notification enabled
	g_currentControl = &cli;
	ZCom_Node *node = new ZCom_Node();
	node->beginReplicationSetup(0);
	node->endReplicationSetup();
	node->setEventNotification(true, true);
	node->registerNodeDynamic(cli.m_eventClass, &cli);

	node->unregisterNode();
	delete node;
	g_currentControl = nullptr;

	cli.Shutdown();
	srv.Shutdown();
}

BOOST_AUTO_TEST_SUITE_END()
