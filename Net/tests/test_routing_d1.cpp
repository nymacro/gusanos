// test_routing_d1.cpp
// Phase D1: per-connection reprule routing in processOutput.
// Verifies that authority nodes route replicator updates to peers based on
// each peer's role for the node and the replicator's ZCOM_REPRULE_* flags,
// instead of broadcasting to all peers.
//   - AUTH_2_PROXY  reaches proxy only
//   - AUTH_2_OWNER  reaches owner only
//   - AUTH_2_ALL    reaches both
//   - OWNER_2_AUTH   reaches authority only (owner -> server)

#include <boost/test/unit_test.hpp>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "net_control.h"
#include "net_node.h"
#include "net_replicator.h"

static int s_d1_port = 25000;

// Server with an authority dynamic node. Tracks spawned connection IDs in
// connect order so tests can identify which client is which.
struct RouteSrv : public ZCom_Control
{
    uint32_t cls = 0;
    ZCom_Node* node = nullptr;
    int spawns = 0;
    std::vector<uint32_t> spawnedIDs;
    int32_t srvVal = 0; // authority-side value (receiver for OWNER_2_AUTH)
    RouteSrv(int port) {
        g_currentControl = this;
        ZCom_initSockets(true, port, 4, 0);
        cls = ZCom_registerClass("R", 0);
    }
    bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream&, ZCom_BitStream&) override { return true; }
    void ZCom_cbConnectionSpawned(uint32_t id) override { spawnedIDs.push_back(id); spawns++; }
};

// Client that creates a matching replica on announcement. The replica's
// auto-replication entry uses ZCOM_REPRULE_NONE so it never originates sends
// (only the authority drives state). `val` is the local sink for received ints.
struct RouteCli : public ZCom_Control
{
    uint32_t cls = 0;
    bool connected = false;
    ZCom_Node* node = nullptr;
    uint32_t gotID = 0;
    int32_t val = 0;
    uint32_t rule = ZCOM_REPRULE_NONE; // rule for this client's replica entry
    RouteCli(int port, uint32_t r = ZCOM_REPRULE_NONE) : rule(r) {
        g_currentControl = this;
        ZCom_initSockets(false, 0, 0, 0);
        cls = ZCom_registerClass("R", 0);
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
        node->addReplicationInt((zS32*)&val, 32, false, ZCOM_REPFLAG_MOSTRECENT, rule, 0);
        node->endReplicationSetup();
        node->registerRequestedNode(cls, this);
        (void)role;
    }
};

static void process3(ZCom_Control* s, ZCom_Control* c1, ZCom_Control* c2, int n)
{
    for (int i = 0; i < n; ++i) {
        g_currentControl = s;  s->ZCom_processInput(eZCom_NoBlock);  s->ZCom_processOutput();
        g_currentControl = c1; c1->ZCom_processInput(eZCom_NoBlock); c1->ZCom_processOutput();
        g_currentControl = c2; c2->ZCom_processInput(eZCom_NoBlock); c2->ZCom_processOutput();
    }
    g_currentControl = nullptr;
}

static void process2(ZCom_Control* s, ZCom_Control* c, int n)
{
    for (int i = 0; i < n; ++i) {
        g_currentControl = s; s->ZCom_processInput(eZCom_NoBlock); s->ZCom_processOutput();
        g_currentControl = c; c->ZCom_processInput(eZCom_NoBlock); c->ZCom_processOutput();
    }
    g_currentControl = nullptr;
}

// Build a server authority node with one int replicator using the given rule,
// owned by client1 (the first spawned connection). Announces to both clients.
static void buildOwnerProxyNode(RouteSrv& srv, uint32_t rule)
{
    srv.node = new ZCom_Node();
    srv.node->setRole(eZCom_RoleAuthority);
    srv.node->beginReplicationSetup(1);
    srv.node->addReplicationInt((zS32*)&srv.srvVal, 32, false,
        ZCOM_REPFLAG_MOSTRECENT, rule, 0);
    srv.node->endReplicationSetup();
    srv.node->registerNodeDynamic(srv.cls, &srv);
    // client1 (first spawned) becomes owner; others are proxies.
    srv.node->setOwner(srv.spawnedIDs[0], true);
}

BOOST_AUTO_TEST_SUITE(routing_d1)

// AUTH_2_PROXY: only the proxy client receives the update.
BOOST_AUTO_TEST_CASE(auth_2_proxy_reaches_proxy_only)
{
    g_currentControl = nullptr;
    int port = s_d1_port++;
    RouteSrv srv(port);
    RouteCli c1(port), c2(port);
    c1.ConnectTo("127.0.0.1", port);
    process3(&srv, &c1, &c2, 40);
    BOOST_REQUIRE(c1.connected);
    c2.ConnectTo("127.0.0.1", port);
    process3(&srv, &c1, &c2, 40);
    BOOST_REQUIRE(c2.connected);
    BOOST_REQUIRE_EQUAL(srv.spawnedIDs.size(), 2u);

    buildOwnerProxyNode(srv, ZCOM_REPRULE_AUTH_2_PROXY);
    process3(&srv, &c1, &c2, 40);
    BOOST_REQUIRE_NE(c1.gotID, 0u);
    BOOST_REQUIRE_NE(c2.gotID, 0u);
    BOOST_REQUIRE(c1.node && c2.node);

    // c1 is owner, c2 is proxy for this node.
    BOOST_CHECK_EQUAL(srv.getPeerRole(srv.spawnedIDs[0], srv.node->getNetworkID()), eZCom_RoleOwner);
    BOOST_CHECK_EQUAL(srv.getPeerRole(srv.spawnedIDs[1], srv.node->getNetworkID()), eZCom_RoleProxy);

    srv.srvVal = 111;
    process3(&srv, &c1, &c2, 40);

    BOOST_CHECK_EQUAL(c2.val, 111);   // proxy received
    BOOST_CHECK_NE(c1.val, 111);      // owner did not receive AUTH_2_PROXY

    srv.Shutdown(); c1.Shutdown(); c2.Shutdown();
}

// AUTH_2_OWNER: only the owner client receives the update.
BOOST_AUTO_TEST_CASE(auth_2_owner_reaches_owner_only)
{
    g_currentControl = nullptr;
    int port = s_d1_port++;
    RouteSrv srv(port);
    RouteCli c1(port), c2(port);
    c1.ConnectTo("127.0.0.1", port);
    process3(&srv, &c1, &c2, 40);
    BOOST_REQUIRE(c1.connected);
    c2.ConnectTo("127.0.0.1", port);
    process3(&srv, &c1, &c2, 40);
    BOOST_REQUIRE(c2.connected);
    BOOST_REQUIRE_EQUAL(srv.spawnedIDs.size(), 2u);

    buildOwnerProxyNode(srv, ZCOM_REPRULE_AUTH_2_OWNER);
    process3(&srv, &c1, &c2, 40);
    BOOST_REQUIRE(c1.node && c2.node);

    srv.srvVal = 222;
    process3(&srv, &c1, &c2, 40);

    BOOST_CHECK_EQUAL(c1.val, 222);   // owner received
    BOOST_CHECK_NE(c2.val, 222);      // proxy did not receive AUTH_2_OWNER

    srv.Shutdown(); c1.Shutdown(); c2.Shutdown();
}

// AUTH_2_ALL: both owner and proxy receive the update.
BOOST_AUTO_TEST_CASE(auth_2_all_reaches_both)
{
    g_currentControl = nullptr;
    int port = s_d1_port++;
    RouteSrv srv(port);
    RouteCli c1(port), c2(port);
    c1.ConnectTo("127.0.0.1", port);
    process3(&srv, &c1, &c2, 40);
    BOOST_REQUIRE(c1.connected);
    c2.ConnectTo("127.0.0.1", port);
    process3(&srv, &c1, &c2, 40);
    BOOST_REQUIRE(c2.connected);
    BOOST_REQUIRE_EQUAL(srv.spawnedIDs.size(), 2u);

    buildOwnerProxyNode(srv, ZCOM_REPRULE_AUTH_2_ALL);
    process3(&srv, &c1, &c2, 40);
    BOOST_REQUIRE(c1.node && c2.node);

    srv.srvVal = 333;
    process3(&srv, &c1, &c2, 40);

    BOOST_CHECK_EQUAL(c1.val, 333);   // owner received
    BOOST_CHECK_EQUAL(c2.val, 333);   // proxy received

    srv.Shutdown(); c1.Shutdown(); c2.Shutdown();
}

// OWNER_2_AUTH: an owner client replicates to the authority (server).
BOOST_AUTO_TEST_CASE(owner_2_auth_reaches_authority)
{
    g_currentControl = nullptr;
    int port = s_d1_port++;
    RouteSrv srv(port);
    // The owner client's replica uses OWNER_2_AUTH so it sends to authority.
    RouteCli c1(port, ZCOM_REPRULE_OWNER_2_AUTH);
    c1.ConnectTo("127.0.0.1", port);
    process2(&srv, &c1, 40);
    BOOST_REQUIRE(c1.connected);
    BOOST_REQUIRE_EQUAL(srv.spawnedIDs.size(), 1u);

    // Server authority node with OWNER_2_AUTH (authority won't send this rule;
    // it only receives the owner's updates).
    srv.node = new ZCom_Node();
    srv.node->setRole(eZCom_RoleAuthority);
    srv.node->beginReplicationSetup(1);
    srv.node->addReplicationInt((zS32*)&srv.srvVal, 32, false,
        ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_OWNER_2_AUTH, 0);
    srv.node->endReplicationSetup();
    srv.node->registerNodeDynamic(srv.cls, &srv);
    srv.node->setOwner(srv.spawnedIDs[0], true);
    process2(&srv, &c1, 40);
    BOOST_REQUIRE(c1.node != nullptr);
    BOOST_REQUIRE_EQUAL(c1.node->getRole(), eZCom_RoleOwner);

    // Owner drives the value; authority must receive it.
    c1.val = 777;
    process2(&srv, &c1, 40);

    BOOST_CHECK_EQUAL(srv.srvVal, 777);

    srv.Shutdown(); c1.Shutdown();
}

BOOST_AUTO_TEST_SUITE_END()
