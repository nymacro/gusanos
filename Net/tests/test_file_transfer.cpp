// test_file_transfer.cpp
// Phase E: functional file transfer over ENet.
//   full_round_trip: server sendFile -> client acceptFile -> Incoming/Data*/Complete
//   abort_path:      client acceptFile(false) -> Aborted, no file written
// Verifies getFileInfo reports size/transferred, and the saved file matches.

#include <boost/test/unit_test.hpp>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <fstream>

#include "net_control.h"
#include "net_node.h"
#include "net_replicator.h"

static int s_fe_port = 26000;

// Server with an authority node. `val` backs a dummy NONE-rule replicator so
// the node announces cleanly (mirrors the routing harness) without originating
// any replicator traffic.
struct FileSrv : public ZCom_Control
{
    uint32_t cls = 0;
    ZCom_Node* node = nullptr;
    int spawns = 0;
    std::vector<uint32_t> spawnedIDs;
    int32_t val = 0;
    ~FileSrv() { delete node; node = nullptr; }
    FileSrv(int port) {
        g_currentControl = this;
        ZCom_initSockets(true, port, 4, 0);
        cls = ZCom_registerClass("F", 0);
    }
    bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream&, ZCom_BitStream&) override { return true; }
    void ZCom_cbConnectionSpawned(uint32_t id) override { spawnedIDs.push_back(id); spawns++; }
};

// Client that creates a matching replica on announcement and drains file
// events from its node queue, auto-accepting (or denying) incoming transfers.
struct FileCli : public ZCom_Control
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
    bool gotAborted = false;
    int dataEvents = 0;
    ZCom_FileTransID lastFid = 0;
    ZCom_ConnID lastConn = 0;
    zU32 infoSizeAfterIncoming = 0;
    zU32 infoXferAfterIncoming = 0;
    ~FileCli() { delete node; node = nullptr; }
    FileCli(int port) {
        g_currentControl = this;
        ZCom_initSockets(false, 0, 0, 0);
        cls = ZCom_registerClass("F", 0);
    }
    uint32_t ConnectTo(const char* h, int p) {
        ZCom_Address a; char hp[64]; snprintf(hp, sizeof hp, "%s:%d", h, p);
        a.setAddress(eZCom_AddressUDP, 0, hp); return ZCom_Connect(a, nullptr);
    }
    void ZCom_cbConnectResult(uint32_t, eZCom_ConnectResult r, ZCom_BitStream&) override { connected = (r == eZCom_ConnAccepted); }
    void ZCom_cbNodeRequest_Dynamic(uint32_t, uint32_t, ZCom_BitStream*, int role, uint32_t net_id) override {
        gotID = net_id;
        node = new ZCom_Node();
        // Net layer assigns the server-announced node ID via applyRequestNodeID.
        node->beginReplicationSetup(1);
        node->addReplicationInt((zS32*)&val, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_NONE, 0);
        node->endReplicationSetup();
        node->registerRequestedNode(cls, this);
        (void)role;
    }
    // Drain file events from the node queue, reacting to Incoming.
    void drainFileEvents() {
        if (!node) return;
        eZCom_Event type; eZCom_NodeRole role; ZCom_ConnID conn;
        while (node->checkEventWaiting()) {
            auto d = node->getNextEvent(&type, &role, &conn);
            if (!d) continue;
            if (type == eZCom_EventFile_Incoming) {
                ZCom_FileTransID fid = static_cast<ZCom_FileTransID>(d->getInt(ZCOM_FTRANS_ID_BITS));
                lastFid = fid; lastConn = conn; gotIncoming = true;
                const ZCom_FileTransInfo& info = node->getFileInfo(conn, fid);
                infoSizeAfterIncoming = info.size;
                infoXferAfterIncoming = info.transferred;
                node->acceptFile(conn, fid, savePath.empty() ? nullptr : savePath.c_str(), autoAccept);
            } else if (type == eZCom_EventFile_Data) {
                d->getInt(ZCOM_FTRANS_ID_BITS);
                ++dataEvents;
            } else if (type == eZCom_EventFile_Complete) {
                lastFid = static_cast<ZCom_FileTransID>(d->getInt(ZCOM_FTRANS_ID_BITS));
                gotComplete = true;
            } else if (type == eZCom_EventFile_Aborted) {
                lastFid = static_cast<ZCom_FileTransID>(d->getInt(ZCOM_FTRANS_ID_BITS));
                gotAborted = true;
            }
        }
    }
};

static void pump2(FileSrv* s, FileCli* c, int n)
{
    for (int i = 0; i < n; ++i) {
        g_currentControl = s; s->ZCom_processInput(eZCom_NoBlock); s->ZCom_processOutput();
        g_currentControl = c; c->ZCom_processInput(eZCom_NoBlock); c->ZCom_processOutput();
        c->drainFileEvents();
    }
    g_currentControl = nullptr;
}

// Build a server authority node with a dummy NONE-rule replicator.
static void buildAuthNode(FileSrv& srv)
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

BOOST_AUTO_TEST_SUITE(file_transfer)

// Full round-trip: server sends a 100KB file, client accepts, receives
// Incoming + one-or-more Data + Complete, and the saved file matches.
BOOST_AUTO_TEST_CASE(full_round_trip)
{
    g_currentControl = nullptr;
    int port = s_fe_port++;
    char srcPath[128]; snprintf(srcPath, sizeof srcPath, "/tmp/ztest_fe_src_%d.bin", port);
    char dstPath[128]; snprintf(dstPath, sizeof dstPath, "/tmp/ztest_fe_dst_%d.bin", port);
    std::remove(srcPath); std::remove(dstPath);

    const size_t FSIZE = 100000;
    BOOST_REQUIRE(writePatternFile(srcPath, FSIZE));

    FileSrv srv(port);
    FileCli cli(port);
    cli.savePath = dstPath;
    cli.ConnectTo("127.0.0.1", port);
    pump2(&srv, &cli, 40);
    BOOST_REQUIRE(cli.connected);
    BOOST_REQUIRE_EQUAL(srv.spawnedIDs.size(), 1u);

    buildAuthNode(srv);
    pump2(&srv, &cli, 40);
    BOOST_REQUIRE(cli.node != nullptr);
    BOOST_REQUIRE_EQUAL(cli.node->getNetworkID(), srv.node->getNetworkID());

    // Server initiates the transfer to the single client.
    ZCom_FileTransID fid = srv.node->sendFile(srcPath, nullptr, srv.spawnedIDs[0], nullptr, 1.0f);
    BOOST_REQUIRE_NE(fid, ZCom_Invalid_ID);

    pump2(&srv, &cli, 120);

    BOOST_REQUIRE(cli.gotIncoming);
    BOOST_REQUIRE(cli.gotComplete);
    BOOST_CHECK(!cli.gotAborted);
    // At least one Data event must have been delivered (file > 1 chunk).
    BOOST_CHECK_GE(cli.dataEvents, 1);
    // getFileInfo right after Incoming reported the full size, zero transferred.
    BOOST_CHECK_EQUAL(cli.infoSizeAfterIncoming, static_cast<zU32>(FSIZE));
    BOOST_CHECK_EQUAL(cli.infoXferAfterIncoming, 0u);
    // getFileInfo after Complete reports full transfer.
    const ZCom_FileTransInfo& info = cli.node->getFileInfo(cli.lastConn, cli.lastFid);
    BOOST_CHECK_EQUAL(info.size, static_cast<zU32>(FSIZE));
    BOOST_CHECK_EQUAL(info.transferred, info.size);

    // The saved file must match the source byte-for-byte.
    std::vector<char> srcData, dstData;
    BOOST_REQUIRE(readAll(srcPath, srcData));
    BOOST_REQUIRE(readAll(dstPath, dstData));
    BOOST_CHECK_EQUAL(srcData.size(), FSIZE);
    BOOST_CHECK_EQUAL(dstData.size(), FSIZE);
    BOOST_CHECK_EQUAL_COLLECTIONS(dstData.begin(), dstData.end(), srcData.begin(), srcData.end());

    srv.Shutdown(); cli.Shutdown();
    std::remove(srcPath); std::remove(dstPath);
}

// Abort path: client denies the incoming transfer and must receive Aborted.
// No destination file should be written.
BOOST_AUTO_TEST_CASE(abort_path)
{
    g_currentControl = nullptr;
    int port = s_fe_port++;
    char srcPath[128]; snprintf(srcPath, sizeof srcPath, "/tmp/ztest_fe_src_%d.bin", port);
    char dstPath[128]; snprintf(dstPath, sizeof dstPath, "/tmp/ztest_fe_dst_%d.bin", port);
    std::remove(srcPath); std::remove(dstPath);

    const size_t FSIZE = 5000;
    BOOST_REQUIRE(writePatternFile(srcPath, FSIZE));

    FileSrv srv(port);
    FileCli cli(port);
    cli.autoAccept = false;          // deny the incoming transfer
    cli.savePath = dstPath;
    cli.ConnectTo("127.0.0.1", port);
    pump2(&srv, &cli, 40);
    BOOST_REQUIRE(cli.connected);

    buildAuthNode(srv);
    pump2(&srv, &cli, 40);
    BOOST_REQUIRE(cli.node != nullptr);

    ZCom_FileTransID fid = srv.node->sendFile(srcPath, nullptr, srv.spawnedIDs[0], nullptr, 1.0f);
    BOOST_REQUIRE_NE(fid, ZCom_Invalid_ID);

    pump2(&srv, &cli, 80);

    BOOST_REQUIRE(cli.gotIncoming);
    BOOST_REQUIRE(cli.gotAborted);
    BOOST_CHECK(!cli.gotComplete);

    // The destination file must not exist (acceptFile(false) never opened it).
    std::ifstream check(dstPath);
    BOOST_CHECK(!check.good());
    check.close();

    srv.Shutdown(); cli.Shutdown();
    std::remove(srcPath); std::remove(dstPath);
}

BOOST_AUTO_TEST_SUITE_END()
