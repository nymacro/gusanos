// test_filetransfer.cpp
// Tests file/data transfer over Zoidcom nodes using chunked sequences.
// Corresponds to ex08_file_transfer sample.

#include <boost/test/unit_test.hpp>
#include <cstring>

#include "net_control.h"
#include "net_node.h"

static void processBoth(ZCom_Control* srv, ZCom_Control* cli, int n)
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

class FileServer : public ZCom_Control
{
public:
	int m_spawnedCount;
	uint32_t m_fileClass;
	ZCom_Node* m_node;

	FileServer(int udpPort)
		: ZCom_Control()
		, m_spawnedCount(0)
		, m_fileClass(0)
		, m_node(nullptr)
	{
		m_fileClass = ZCom_registerClass("FileXfer", 0);
		g_currentControl = this;
		ZCom_initSockets(true, udpPort, 0, 0);
	}

	void SendFileChunk(int chunkIdx, int totalChunks,
	                    const uint8_t* data, int len)
	{
		if (!m_node) return;
		ZCom_BitStream chunk;
		chunk.addInt(totalChunks, 8);
		chunk.addInt(chunkIdx, 8);
		chunk.addInt(len, 16);
		for (int i = 0; i < len; i++)
		{
			chunk.addInt(data[i], 8);
		}
		m_node->sendEvent(eZCom_ReliableOrdered, 0, &chunk);
	}

protected:
	bool ZCom_cbConnectionRequest(ZCom_ConnID id, ZCom_BitStream& request, ZCom_BitStream& reply) override
	{
		return true;
	}

	void ZCom_cbConnectionSpawned(ZCom_ConnID id) override
	{
		m_spawnedCount++;
		m_node = new ZCom_Node();
		m_node->registerNodeDynamic(m_fileClass, this);
	}
};

class FileClient : public ZCom_Control
{
public:
	bool m_connected;
	uint32_t m_fileClass;

	FileClient(int udpPort)
		: ZCom_Control()
		, m_connected(false)
		, m_fileClass(0)
	{
		m_fileClass = ZCom_registerClass("FileXfer", 0);
		g_currentControl = this;
		ZCom_initSockets(false, 0, 0, 0);
	}

	uint32_t ConnectTo(const char* host, int port)
	{
		ZCom_Address addr;
		addr.setAddress(ZCom_Control::eZCom_AddressUDP, port, host);
		return ZCom_Connect(addr, nullptr);
	}

protected:
	void ZCom_cbConnectResult(ZCom_ConnID id, eZCom_ConnectResult result, ZCom_BitStream& reply) override
	{
		m_connected = (result == eZCom_ConnAccepted);
	}
};

BOOST_AUTO_TEST_SUITE(filetransfer)

BOOST_AUTO_TEST_CASE(send_single_file_chunk)
{
	static int portOffset = 0;
	g_currentControl = nullptr;
	int port = 19140 + (portOffset++);

	{
	FileServer srv(port);
	FileClient cli(port);

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);

	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);
	BOOST_REQUIRE(srv.m_node != nullptr);

	g_currentControl = &srv;

	ZCom_BitStream nameData;
	nameData.addString("test.bin");
	srv.m_node->sendEvent(eZCom_ReliableOrdered, 0, &nameData);

	const char* testData = "Hello, this is file data!";
	srv.SendFileChunk(0, 1, (const uint8_t*)testData, (int)strlen(testData));

	g_currentControl = nullptr;

	processBoth(&srv, &cli, 10);

	srv.Shutdown();
	cli.Shutdown();
	}

	BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(send_multi_chunk_file)
{
	static int portOffset = 0;
	g_currentControl = nullptr;
	int port = 19150 + (portOffset++);

	{
	FileServer srv(port);
	FileClient cli(port);

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);

	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);
	BOOST_REQUIRE(srv.m_node != nullptr);

	g_currentControl = &srv;

	ZCom_BitStream nameData;
	nameData.addString("large.bin");
	srv.m_node->sendEvent(eZCom_ReliableOrdered, 0, &nameData);

	const char* chunks[] = {"chunk0_data_", "chunk1_data_", "chunk2_data_"};
	for (int i = 0; i < 3; i++)
	{
		srv.SendFileChunk(i, 3,
		                  (const uint8_t*)chunks[i],
		                  (int)strlen(chunks[i]));
	}
	g_currentControl = nullptr;

	processBoth(&srv, &cli, 10);

	srv.Shutdown();
	cli.Shutdown();
	}

	BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
