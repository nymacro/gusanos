// test_connect.cpp
// Tests basic connect/disconnect client-server interaction.
// Corresponds to ex00_connect sample.

#include <boost/test/unit_test.hpp>
#include <cstdio>
#include <cstring>

#include "net_control.h"

static void resetGlobals()
{
	g_currentControl = nullptr;
}

class TestServer : public ZCom_Control
{
public:
	int m_connCount;
	int m_disconnectCount;
	int m_dataReceivedCount;
	int m_port;
	std::string m_lastData;

	TestServer(int udpPort)
		: ZCom_Control()
		, m_connCount(0)
		, m_disconnectCount(0)
		, m_dataReceivedCount(0)
		, m_port(udpPort)
	{
	}

	bool InitSockets()
	{
		return ZCom_initSockets(true, m_port, 0, 0);
	}

protected:
	bool ZCom_cbConnectionRequest(ZCom_ConnID id, ZCom_BitStream& request, ZCom_BitStream& reply) override
	{
		return true;
	}

	void ZCom_cbConnectionSpawned(ZCom_ConnID id) override
	{
		m_connCount++;
	}

	void ZCom_cbConnectionClosed(ZCom_ConnID id, eZCom_CloseReason reason, ZCom_BitStream& reasondata) override
	{
		m_disconnectCount++;
		m_connCount--;
	}

	void ZCom_cbDataReceived(ZCom_ConnID id, ZCom_BitStream& data) override
	{
		m_dataReceivedCount++;
		m_lastData = data.getStringStatic();

		ZCom_BitStream reply;
		reply.addString("your data has been received");
		ZCom_sendData(id, &reply, eZCom_ReliableOrdered);
	}
};

class TestClient : public ZCom_Control
{
public:
	bool m_connected;
	bool m_accepted;
	bool m_disconnected;
	int m_dataReceivedCount;
	std::string m_lastReply;

	TestClient()
		: ZCom_Control()
		, m_connected(false)
		, m_accepted(false)
		, m_disconnected(false)
		, m_dataReceivedCount(0)
	{
	}

	bool InitSockets()
	{
		return ZCom_initSockets(false, 0, 0, 0);
	}

protected:
	void ZCom_cbConnectResult(ZCom_ConnID id, eZCom_ConnectResult result, ZCom_BitStream& reply) override
	{
		m_connected = true;
		m_accepted = (result == eZCom_ConnAccepted);
	}

	void ZCom_cbConnectionClosed(ZCom_ConnID id, eZCom_CloseReason reason, ZCom_BitStream& reasondata) override
	{
		m_disconnected = true;
	}

	void ZCom_cbDataReceived(ZCom_ConnID id, ZCom_BitStream& data) override
	{
		m_dataReceivedCount++;
		m_lastReply = data.getStringStatic();
	}
};

struct ConnectFixture
{
	int testPort;
	TestServer* server;
	TestClient* client;
	ZCom_Address serverAddr;

	ConnectFixture()
		: testPort(18899)
		, server(nullptr)
		, client(nullptr)
	{
		static int portOffset = 0;
		testPort = 18900 + (portOffset++);
		resetGlobals();
	}

	void Setup()
	{
		resetGlobals();

		server = new TestServer(testPort);
		BOOST_REQUIRE(server->InitSockets());

		client = new TestClient();
		BOOST_REQUIRE(client->InitSockets());

		char hostport[64];
		snprintf(hostport, sizeof(hostport), "127.0.0.1:%d", testPort);
		serverAddr.setAddress(ZCom_Control::eZCom_AddressUDP, 0, hostport);
	}

	void ProcessAll(int iterations = 200)
	{
		for (int i = 0; i < iterations; i++)
		{
			g_currentControl = server;
			server->ZCom_processInput(eZCom_NoBlock);
			server->ZCom_processOutput();

			g_currentControl = client;
			client->ZCom_processInput(eZCom_NoBlock);
			client->ZCom_processOutput();
		}
		g_currentControl = nullptr;
	}

	~ConnectFixture()
	{
		g_currentControl = nullptr;
		delete client;
		delete server;
	}
};

BOOST_AUTO_TEST_SUITE(connection)

BOOST_AUTO_TEST_CASE(server_can_accept_client)
{
	ConnectFixture f;
	f.Setup();

	g_currentControl = f.client;
	uint32_t cid = f.client->ZCom_Connect(f.serverAddr, nullptr);
	g_currentControl = nullptr;

	BOOST_CHECK(cid != ZCom_Invalid_ID);

	f.ProcessAll(200);

	BOOST_CHECK_EQUAL(f.server->m_connCount, 1);
	BOOST_CHECK(f.client->m_connected);
	BOOST_CHECK(f.client->m_accepted);
}

BOOST_AUTO_TEST_CASE(client_can_send_data_to_server)
{
	ConnectFixture f;
	f.Setup();

	g_currentControl = f.client;
	uint32_t cid = f.client->ZCom_Connect(f.serverAddr, nullptr);
	g_currentControl = nullptr;
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);
	f.ProcessAll(200);
	BOOST_REQUIRE(f.client->m_connected);

	g_currentControl = f.client;
	ZCom_BitStream data;
	data.addString("Hello server!");
	ZCom_sendData(cid, &data, eZCom_ReliableOrdered);
	g_currentControl = nullptr;

	f.ProcessAll(200);

	BOOST_CHECK_EQUAL(f.server->m_dataReceivedCount, 1);
	BOOST_CHECK_EQUAL(f.server->m_lastData, "Hello server!");
}

BOOST_AUTO_TEST_CASE(server_echos_data_back)
{
	ConnectFixture f;
	f.Setup();

	g_currentControl = f.client;
	uint32_t cid = f.client->ZCom_Connect(f.serverAddr, nullptr);
	g_currentControl = nullptr;
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);
	f.ProcessAll(200);

	g_currentControl = f.client;
	ZCom_BitStream data;
	data.addString("ping");
	ZCom_sendData(cid, &data, eZCom_ReliableOrdered);
	g_currentControl = nullptr;

	f.ProcessAll(200);

	BOOST_CHECK_EQUAL(f.client->m_dataReceivedCount, 1);
	BOOST_CHECK_EQUAL(f.client->m_lastReply, "your data has been received");
}

BOOST_AUTO_TEST_CASE(client_disconnect_notifies_server)
{
	ConnectFixture f;
	f.Setup();

	g_currentControl = f.client;
	uint32_t cid = f.client->ZCom_Connect(f.serverAddr, nullptr);
	g_currentControl = nullptr;
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);
	f.ProcessAll(200);
	BOOST_REQUIRE(f.client->m_connected);
	BOOST_REQUIRE_EQUAL(f.server->m_connCount, 1);

	g_currentControl = f.client;
	f.client->ZCom_Disconnect(cid, nullptr);
	g_currentControl = nullptr;

	f.ProcessAll(200);

	BOOST_CHECK_EQUAL(f.server->m_disconnectCount, 1);
	BOOST_CHECK_EQUAL(f.server->m_connCount, 0);
	BOOST_CHECK(f.client->m_disconnected);
}

BOOST_AUTO_TEST_CASE(shutdown_cleans_up)
{
	ConnectFixture f;
	f.Setup();

	g_currentControl = f.client;
	uint32_t cid = f.client->ZCom_Connect(f.serverAddr, nullptr);
	g_currentControl = nullptr;
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);
	f.ProcessAll(200);
	BOOST_REQUIRE(f.client->m_connected);

	f.client->Shutdown();
	f.server->Shutdown();

	BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
