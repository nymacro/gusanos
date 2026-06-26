// test_datasync.cpp
// Tests data synchronization via replicators.
// Corresponds to ex06_object_datasync sample.

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

class SyncServer : public ZCom_Control
{
public:
	int m_spawnedCount;
	uint32_t m_syncClass;
	ZCom_Node* m_node;

	SyncServer(int udpPort)
		: ZCom_Control()
		, m_spawnedCount(0)
		, m_syncClass(0)
		, m_node(nullptr)
	{
		m_syncClass = ZCom_registerClass("GameState", 0);
		g_currentControl = this;
		ZCom_initSockets(true, udpPort, 0, 0);
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
		m_node->registerNodeDynamic(m_syncClass, this);
	}
};

class SyncClient : public ZCom_Control
{
public:
	bool m_connected;
	uint32_t m_syncClass;

	SyncClient(int udpPort)
		: ZCom_Control()
		, m_connected(false)
		, m_syncClass(0)
	{
		m_syncClass = ZCom_registerClass("GameState", 0);
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

BOOST_AUTO_TEST_SUITE(datasync)

BOOST_AUTO_TEST_CASE(node_with_replicator_setup)
{
	static int portOffset = 0;
	g_currentControl = nullptr;
	int port = 19100 + (portOffset++);

	{
	SyncServer srv(port);
	SyncClient cli(port);

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);

	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);
	BOOST_REQUIRE(srv.m_node != nullptr);

	srv.Shutdown();
	cli.Shutdown();
	}

	BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(node_id_unique)
{
	static int portOffset = 0;
	g_currentControl = nullptr;
	int port = 19110 + (portOffset++);

	{
	SyncServer srv(port);
	SyncClient cli(port);

	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);

	processBoth(&srv, &cli, 30);
	BOOST_REQUIRE(cli.m_connected);
	BOOST_REQUIRE(srv.m_node != nullptr);

	uint32_t nid1 = srv.m_node->getNetworkID();

	g_currentControl = &srv;
	ZCom_Node* node2 = new ZCom_Node();
	node2->registerNodeDynamic(srv.m_syncClass, &srv);
	uint32_t nid2 = node2->getNetworkID();
	g_currentControl = nullptr;

	BOOST_CHECK(nid1 != nid2);

	srv.Shutdown();
	cli.Shutdown();
	}
}



BOOST_AUTO_TEST_CASE(replicator_pack_unpack_cycle)
{
	static int portOffset = 0;
	g_currentControl = nullptr;
	int port = 19120 + (portOffset++);

	{
	SyncServer srv(port);

	ZCom_Node* source = new ZCom_Node();
	float sourceVal = 0.0f;

	g_currentControl = &srv;
	source->registerNodeDynamic(srv.m_syncClass, &srv);
	source->addReplicationFloat(&sourceVal, 16, 0, 0);
	sourceVal = 0.42f;
	g_currentControl = nullptr;

	ZCom_BitStream packed;
	source->packAllReplicators(&packed);
	BOOST_REQUIRE_MESSAGE(packed.getDataLength() > 0,
	                       "packAllReplicators should produce data");

	ZCom_Node* target = new ZCom_Node();
	float targetVal = 0.0f;

	g_currentControl = &srv;
	target->registerNodeDynamic(srv.m_syncClass, &srv);
	target->addReplicationFloat(&targetVal, 16, 0, 0);
	g_currentControl = nullptr;

	ZCom_BitStream packedCopy(packed.getData(), packed.getDataLength());
	target->unpackAllReplicators(&packedCopy, true, 0);

	BOOST_CHECK_CLOSE(targetVal, sourceVal, 0.1f);

	delete target;
	delete source;
	srv.Shutdown();
	}
}

BOOST_AUTO_TEST_CASE(replicator_multiple_values)
{
	static int portOffset = 0;
	g_currentControl = nullptr;
	int port = 19130 + (portOffset++);

	{
	SyncServer srv(port);

	ZCom_Node* source = new ZCom_Node();
	float x = 0.0f, y = 0.0f, z = 0.0f;
	int32_t ammo = 0;

	g_currentControl = &srv;
	source->registerNodeDynamic(srv.m_syncClass, &srv);
	source->addReplicationFloat(&x, 16, 0, 0);
	source->addReplicationFloat(&y, 16, 0, 0);
	source->addReplicationFloat(&z, 16, 0, 0);
	source->addReplicationInt(&ammo, 16, true, 0, 0);
	x = 0.15f; y = 0.25f; z = 0.35f; ammo = 100;
	g_currentControl = nullptr;

	ZCom_BitStream packed;
	source->packAllReplicators(&packed);
	BOOST_REQUIRE_MESSAGE(packed.getDataLength() > 0,
	                       "packAllReplicators should produce data");

	ZCom_Node* target = new ZCom_Node();
	float rx = 0, ry = 0, rz = 0;
	int32_t rammo = 0;

	g_currentControl = &srv;
	target->registerNodeDynamic(srv.m_syncClass, &srv);
	target->addReplicationFloat(&rx, 16, 0, 0);
	target->addReplicationFloat(&ry, 16, 0, 0);
	target->addReplicationFloat(&rz, 16, 0, 0);
	target->addReplicationInt(&rammo, 16, true, 0, 0);
	g_currentControl = nullptr;

	ZCom_BitStream packedCopy(packed.getData(), packed.getDataLength());
	target->unpackAllReplicators(&packedCopy, true, 0);

	BOOST_CHECK_CLOSE(rx, x, 0.1f);
	BOOST_CHECK_CLOSE(ry, y, 0.1f);
	BOOST_CHECK_CLOSE(rz, z, 0.1f);
	BOOST_CHECK_EQUAL(rammo, ammo);

	delete target;
	delete source;
	srv.Shutdown();
	}
}

BOOST_AUTO_TEST_SUITE_END()
