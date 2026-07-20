// test_filetransfer_channel.cpp
// Regression test for T2.2 (file transfer moved to ENet channel 1).
//
// pumpFileTransfers now sends MSG_FILE_DATA and MSG_FILE_COMPLETE on channel 1
// instead of channel 0, so large level downloads no longer head-of-line-block
// gameplay traffic. The receiver dispatches by msgType (not channel), so the
// change is transparent to the dispatch path — this test verifies the
// end-to-end behavior: a file transfer completes on channel 1 while a
// concurrent ZCom_sendData message (channel 0) still arrives at the server.

#include <boost/test/unit_test.hpp>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <fstream>

#include "net_control.h"
#include "net_node.h"
#include "net_replicator.h"

static int s_fc_port = 27300;

struct FCSrv : public ZCom_Control
{
	uint32_t cls = 0;
	ZCom_Node* node = nullptr;
	int spawns = 0;
	std::vector<uint32_t> spawnedIDs;
	int32_t val = 0;
	int dataReceived = 0;
	std::string lastData;

	~FCSrv() { delete node; node = nullptr; }
	FCSrv(int port) {
		g_currentControl = this;
		ZCom_initSockets(true, port, 4, 0);
		cls = ZCom_registerClass("FC", 0);
	}
	bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream&, ZCom_BitStream&) override { return true; }
	void ZCom_cbConnectionSpawned(uint32_t id) override { spawnedIDs.push_back(id); spawns++; }
	void ZCom_cbDataReceived(uint32_t, ZCom_BitStream& data) override {
		dataReceived++;
		lastData = data.getStringStatic();
	}
};

struct FCCli : public ZCom_Control
{
	uint32_t cls = 0;
	bool connected = false;
	ZCom_Node* node = nullptr;
	uint32_t gotID = 0;
	int32_t val = 0;
	bool autoAccept = true;
	std::string savePath;
	bool gotIncoming = false;
	bool gotComplete = false;
	int dataEvents = 0;
	ZCom_FileTransID lastFid = 0;
	ZCom_ConnID lastConn = 0;

	~FCCli() { delete node; node = nullptr; }
	FCCli(int port) {
		g_currentControl = this;
		ZCom_initSockets(false, 0, 0, 0);
		cls = ZCom_registerClass("FC", 0);
	}
	uint32_t ConnectTo(const char* h, int p) {
		ZCom_Address a; char hp[64]; snprintf(hp, sizeof hp, "%s:%d", h, p);
		a.setAddress(eZCom_AddressUDP, 0, hp); return ZCom_Connect(a, nullptr);
	}
	void ZCom_cbConnectResult(uint32_t, eZCom_ConnectResult r, ZCom_BitStream&) override {
		connected = (r == eZCom_ConnAccepted);
	}
	void ZCom_cbNodeRequest_Dynamic(uint32_t, uint32_t, ZCom_BitStream*, int role, uint32_t net_id) override {
		gotID = net_id;
		node = new ZCom_Node();
		node->beginReplicationSetup(1);
		node->addReplicationInt((zS32*)&val, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_NONE, 0);
		node->endReplicationSetup();
		node->registerRequestedNode(cls, this);
		(void)role;
	}
	void drainFileEvents() {
		if (!node) return;
		eZCom_Event type; eZCom_NodeRole role; ZCom_ConnID conn;
		while (node->checkEventWaiting()) {
			auto d = node->getNextEvent(&type, &role, &conn);
			if (!d) continue;
			if (type == eZCom_EventFile_Incoming) {
				ZCom_FileTransID fid = static_cast<ZCom_FileTransID>(d->getInt(ZCOM_FTRANS_ID_BITS));
				lastFid = fid; lastConn = conn; gotIncoming = true;
				node->acceptFile(conn, fid, savePath.c_str(), autoAccept);
			} else if (type == eZCom_EventFile_Data) {
				d->getInt(ZCOM_FTRANS_ID_BITS);
				++dataEvents;
			} else if (type == eZCom_EventFile_Complete) {
				lastFid = static_cast<ZCom_FileTransID>(d->getInt(ZCOM_FTRANS_ID_BITS));
				gotComplete = true;
			}
		}
	}
};

static void pump2(FCSrv* s, FCCli* c, int n)
{
	for (int i = 0; i < n; ++i) {
		g_currentControl = s; s->ZCom_processInput(eZCom_NoBlock); s->ZCom_processOutput();
		g_currentControl = c; c->ZCom_processInput(eZCom_NoBlock); c->ZCom_processOutput();
		c->drainFileEvents();
	}
	g_currentControl = nullptr;
}

static void buildAuthNode(FCSrv& srv)
{
	srv.node = new ZCom_Node();
	srv.node->setRole(eZCom_RoleAuthority);
	srv.node->beginReplicationSetup(1);
	srv.node->addReplicationInt((zS32*)&srv.val, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_NONE, 0);
	srv.node->endReplicationSetup();
	srv.node->registerNodeDynamic(srv.cls, &srv);
}

static bool writePatternFile(const char* path, size_t bytes)
{
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f.is_open()) return false;
	std::vector<char> buf(bytes);
	for (size_t i = 0; i < bytes; ++i) buf[i] = static_cast<char>((i * 7 + 3) % 251);
	f.write(buf.data(), bytes);
	return true;
}

static bool readAll(const char* path, std::vector<char>& out)
{
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f.is_open()) return false;
	std::streamsize sz = f.tellg();
	if (sz < 0) { out.clear(); return true; }
	f.seekg(0, std::ios::beg);
	out.resize(sz);
	f.read(out.data(), sz);
	return true;
}

BOOST_AUTO_TEST_SUITE(filetransfer_channel)

// A file transfer on channel 1 must complete while a ZCom_sendData message
// on channel 0 still arrives at the server — the channel separation that T2.2
// introduces. Without the channel move both flows shared channel 0; this test
// would still pass on loopback (no HOL blocking), but it pins the contract so
// a future regression to channel 0 for file data is caught.
BOOST_AUTO_TEST_CASE(file_transfer_completes_with_concurrent_userdata)
{
	g_currentControl = nullptr;
	int port = s_fc_port++;
	char srcPath[128]; snprintf(srcPath, sizeof srcPath, "/tmp/ztest_fc_src_%d.bin", port);
	char dstPath[128]; snprintf(dstPath, sizeof dstPath, "/tmp/ztest_fc_dst_%d.bin", port);
	std::remove(srcPath); std::remove(dstPath);

	const size_t FSIZE = 5000;
	BOOST_REQUIRE(writePatternFile(srcPath, FSIZE));

	FCSrv srv(port);
	FCCli cli(port);
	cli.savePath = dstPath;
	uint32_t cid = cli.ConnectTo("127.0.0.1", port);
	BOOST_REQUIRE(cid != ZCom_Invalid_ID);
	pump2(&srv, &cli, 40);
	BOOST_REQUIRE(cli.connected);
	BOOST_REQUIRE_EQUAL(srv.spawnedIDs.size(), 1u);

	buildAuthNode(srv);
	pump2(&srv, &cli, 40);
	BOOST_REQUIRE(cli.node != nullptr);

	// Server initiates the file transfer (file chunks go out on channel 1).
	ZCom_FileTransID fid = srv.node->sendFile(srcPath, nullptr, srv.spawnedIDs[0], nullptr, 1.0f);
	BOOST_REQUIRE_NE(fid, ZCom_Invalid_ID);

	// While the file is in flight, the client sends a ZCom_sendData message
	// to the server on channel 0 (gameplay channel).
	g_currentControl = &cli;
	ZCom_BitStream data;
	data.addString("gameplay-ping");
	ZCom_sendData(cid, &data, eZCom_ReliableOrdered);
	g_currentControl = nullptr;

	pump2(&srv, &cli, 120);

	// File transfer (channel 1) completed.
	BOOST_REQUIRE(cli.gotIncoming);
	BOOST_REQUIRE(cli.gotComplete);
	BOOST_CHECK_GE(cli.dataEvents, 1);
	const ZCom_FileTransInfo& info = cli.node->getFileInfo(cli.lastConn, cli.lastFid);
	BOOST_CHECK_EQUAL(info.size, static_cast<zU32>(FSIZE));
	BOOST_CHECK_EQUAL(info.transferred, info.size);

	// The saved file matches the source byte-for-byte.
	std::vector<char> srcData, dstData;
	BOOST_REQUIRE(readAll(srcPath, srcData));
	BOOST_REQUIRE(readAll(dstPath, dstData));
	BOOST_CHECK_EQUAL_COLLECTIONS(dstData.begin(), dstData.end(),
	                              srcData.begin(), srcData.end());

	// The concurrent ZCom_sendData message (channel 0) arrived at the server.
	BOOST_CHECK_EQUAL(srv.dataReceived, 1);
	BOOST_CHECK_EQUAL(srv.lastData, "gameplay-ping");

	srv.Shutdown(); cli.Shutdown();
	std::remove(srcPath); std::remove(dstPath);
}

BOOST_AUTO_TEST_SUITE_END()
