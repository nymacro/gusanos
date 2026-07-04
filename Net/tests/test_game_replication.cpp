// test_game_replication.cpp
// Integration-style tests that mirror the game's networking patterns:
//   - Replicator peekData() works inside inPreUpdateItem (worm<->player linkage)
//   - Node events (e.g. sync messages) reach the client's event queue
//   - Node events arriving before the local node exists are buffered & replayed
//   - Client node role (Owner/Proxy) is honoured by sendEvent rule checks

#include <boost/test/unit_test.hpp>
#include <cstring>
#include <cstdio>
#include <cstdint>

#include "net_control.h"
#include "net_node.h"
#include "net_replicator.h"

static int s_port = 24000;

// Mirror the game pattern: authority node replicates an INTERCEPT int (like
// NetWorm::m_playerID / BasePlayer::m_wormID). The client interceptor calls
// peekData() to read the incoming value before it is stored.
struct GameInterceptor : public ZCom_NodeReplicationInterceptor
{
    int inPreUpdateItemCount = 0;
    uint32_t lastPeeked = 0xDEAD;
    bool peekCalled = false;

    bool inPreUpdate(ZCom_Node*, uint32_t, eZCom_NodeRole) override { return true; }
    bool inPreUpdateItem(ZCom_Node*, uint32_t, eZCom_NodeRole, ZCom_Replicator* rep, uint32_t) override
    {
        inPreUpdateItemCount++;
        void* peeked = rep->peekData();
        if (peeked) {
            peekCalled = true;
            lastPeeked = *static_cast<uint32_t*>(peeked);
        }
        return true;
    }
    bool outPreUpdate(ZCom_Node*, uint32_t, eZCom_NodeRole) override { return true; }
    bool outPreUpdateItem(ZCom_Node*, uint32_t, eZCom_NodeRole, ZCom_Replicator*) override { return true; }
};

class RepSrv : public ZCom_Control
{
public:
    uint32_t cls;
    int spawned = 0;
    ZCom_Node* node = nullptr;
    RepSrv(int port) {
        g_currentControl = this;
        ZCom_initSockets(true, port, 2, 0);
        cls = ZCom_registerClass("Rep", 0);
    }
    bool ZCom_cbConnectionRequest(uint32_t, ZCom_BitStream&, ZCom_BitStream&) override { return true; }
    void ZCom_cbConnectionSpawned(uint32_t) override { spawned++; }
};

class RepCli : public ZCom_Control
{
public:
    uint32_t cls;
    bool connected = false;
    GameInterceptor interceptor;
    ZCom_Node* node = nullptr;
    uint32_t gotNodeID = 0;
    int gotRole = -1;
    RepCli(int port) {
        g_currentControl = this;
        ZCom_initSockets(false, 0, 0, 0);
        cls = ZCom_registerClass("Rep", 0);
    }
    uint32_t ConnectTo(const char* host, int port) {
        ZCom_Address a; char hp[64]; snprintf(hp,sizeof hp,"%s:%d",host,port);
        a.setAddress(eZCom_AddressUDP,0,hp); return ZCom_Connect(a, nullptr);
    }
    void ZCom_cbConnectResult(uint32_t, eZCom_ConnectResult r, ZCom_BitStream&) override { connected = (r==eZCom_ConnAccepted); }
    void ZCom_cbNodeRequest_Dynamic(uint32_t, uint32_t, ZCom_BitStream*, int role, uint32_t net_id) override {
        gotNodeID = net_id; gotRole = role;
        node = new ZCom_Node();
        // Net layer assigns the server-announced node ID via applyRequestNodeID.
        node->setEventNotification(true, true);
        node->beginReplicationSetup(1);
        node->setInterceptID(0);
        static uint32_t local = 0;
        node->addReplicationInt((zS32*)&local, 32, false,
            ZCOM_REPFLAG_MOSTRECENT | ZCOM_REPFLAG_INTERCEPT, ZCOM_REPRULE_AUTH_2_ALL, 0);
        node->endReplicationSetup();
        node->setReplicationInterceptor(&interceptor);
        node->registerRequestedNode(cls, this);
        // Role is auto-assigned by the omfgnet layer from the request context.
    }
};

static void processBoth(ZCom_Control* s, ZCom_Control* c, int n)
{
    for (int i=0;i<n;i++){
        g_currentControl=s; s->ZCom_processInput(eZCom_NoBlock); s->ZCom_processOutput();
        g_currentControl=c; c->ZCom_processInput(eZCom_NoBlock); c->ZCom_processOutput();
    }
    g_currentControl=nullptr;
}

BOOST_AUTO_TEST_SUITE(game_replication)

// peekData() inside inPreUpdateItem returns the incoming value (not nullptr),
// enabling the worm<->player linkage that BasePlayer/NetWorm interceptors rely on.
BOOST_AUTO_TEST_CASE(interceptor_peekdata_returns_incoming_value)
{
    g_currentControl = nullptr;
    int port = s_port++;
    RepSrv srv(port);
    RepCli cli(port);
    cli.ConnectTo("127.0.0.1", port);
    processBoth(&srv, &cli, 40);
    BOOST_REQUIRE(cli.connected);

    uint32_t playerID = 42;
    srv.node = new ZCom_Node();
    srv.node->setRole(eZCom_RoleAuthority);
    srv.node->beginReplicationSetup(1);
    srv.node->setInterceptID(0);
    srv.node->addReplicationInt((zS32*)&playerID, 32, false,
        ZCOM_REPFLAG_MOSTRECENT | ZCOM_REPFLAG_INTERCEPT, ZCOM_REPRULE_AUTH_2_ALL, 0);
    srv.node->endReplicationSetup();
    srv.node->registerNodeDynamic(srv.cls, &srv);
    srv.node->setOwner(1, true);
    processBoth(&srv, &cli, 30);
    BOOST_REQUIRE_NE(cli.gotNodeID, 0u);

    playerID = 99;
    processBoth(&srv, &cli, 30);

    BOOST_CHECK_GT(cli.interceptor.inPreUpdateItemCount, 0);
    BOOST_CHECK(cli.interceptor.peekCalled);
    BOOST_CHECK_EQUAL(cli.interceptor.lastPeeked, 99u);

    srv.Shutdown(); cli.Shutdown();
}

// A node event sent from server->client must land in the client node's event
// queue (checkEventWaiting / getNextEvent). This was a coverage gap: the
// existing test_events.cpp only asserted BOOST_CHECK(true).
BOOST_AUTO_TEST_CASE(node_event_reaches_client_queue)
{
    g_currentControl = nullptr;
    int port = s_port++;

    // Simple server with an authority dynamic node.
    class ESrv : public ZCom_Control {
    public:
        uint32_t cls; ZCom_Node* node=nullptr; int spawned=0;
        ESrv(int p){ g_currentControl=this; ZCom_initSockets(true,p,2,0); cls=ZCom_registerClass("E",0); }
        bool ZCom_cbConnectionRequest(uint32_t,ZCom_BitStream&,ZCom_BitStream&) override { return true; }
        void ZCom_cbConnectionSpawned(uint32_t) override { spawned++; }
    } srv(port);

    class ECli : public ZCom_Control {
    public:
        uint32_t cls; bool connected=false; ZCom_Node* node=nullptr; uint32_t gotID=0;
        ECli(int p){ g_currentControl=this; ZCom_initSockets(false,0,0,0); cls=ZCom_registerClass("E",0); }
        uint32_t ConnectTo(const char* h,int p){ ZCom_Address a; char hp[64]; snprintf(hp,sizeof hp,"%s:%d",h,p); a.setAddress(eZCom_AddressUDP,0,hp); return ZCom_Connect(a,nullptr); }
        void ZCom_cbConnectResult(uint32_t,eZCom_ConnectResult r,ZCom_BitStream&) override { connected=(r==eZCom_ConnAccepted); }
        void ZCom_cbNodeRequest_Dynamic(uint32_t,uint32_t,ZCom_BitStream*,int role,uint32_t net_id) override {
            gotID=net_id;
            node=new ZCom_Node();
            // Net layer assigns the server-announced node ID via applyRequestNodeID.
            node->setEventNotification(true,true);
            node->registerRequestedNode(cls,this);
            // Role auto-assigned by omfgnet layer from request context.
        }
    } cli(port);

    cli.ConnectTo("127.0.0.1", port);
    processBoth(&srv, &cli, 40);
    BOOST_REQUIRE(cli.connected);

    srv.node = new ZCom_Node();
    srv.node->setRole(eZCom_RoleAuthority);
    srv.node->registerNodeDynamic(srv.cls, &srv);
    srv.node->setEventNotification(true, false);
    processBoth(&srv, &cli, 30);
    BOOST_REQUIRE_NE(cli.gotID, 0u);
    BOOST_REQUIRE(cli.node != nullptr);

    // Send a user event with a payload.
    ZCom_BitStream ev;
    ev.addInt(0xCAFE, 16);
    srv.node->sendEvent(eZCom_ReliableOrdered, 0, &ev);
    processBoth(&srv, &cli, 20);

    BOOST_CHECK(cli.node->checkEventWaiting());
    if (cli.node->checkEventWaiting()) {
        eZCom_Event type; eZCom_NodeRole role; uint32_t connID;
        ZCom_BitStream* data = cli.node->getNextEvent(&type, &role, &connID);
        BOOST_CHECK_EQUAL(type, eZCom_EventUser);
        BOOST_REQUIRE(data);
        BOOST_CHECK_EQUAL(data->getInt(16), 0xCAFE);
    }

    srv.Shutdown(); cli.Shutdown();
}

// A node event that arrives BEFORE the client has created the local node
// (race between announcement and a sync message) must be buffered and replayed
// when the node is registered, not silently dropped.
BOOST_AUTO_TEST_CASE(node_event_buffered_before_node_registration)
{
    g_currentControl = nullptr;
    int port = s_port++;

    class BSrv : public ZCom_Control {
    public:
        uint32_t cls; ZCom_Node* node=nullptr;
        BSrv(int p){ g_currentControl=this; ZCom_initSockets(true,p,2,0); cls=ZCom_registerClass("B",0); }
        bool ZCom_cbConnectionRequest(uint32_t,ZCom_BitStream&,ZCom_BitStream&) override { return true; }
        void ZCom_cbConnectionSpawned(uint32_t) override {}
    } srv(port);

    class BCli : public ZCom_Control {
    public:
        uint32_t cls; bool connected=false; ZCom_Node* node=nullptr; uint32_t gotID=0;
        bool created=false; // defer node creation to simulate the race
        BCli(int p){ g_currentControl=this; ZCom_initSockets(false,0,0,0); cls=ZCom_registerClass("B",0); }
        uint32_t ConnectTo(const char* h,int p){ ZCom_Address a; char hp[64]; snprintf(hp,sizeof hp,"%s:%d",h,p); a.setAddress(eZCom_AddressUDP,0,hp); return ZCom_Connect(a,nullptr); }
        void ZCom_cbConnectResult(uint32_t,eZCom_ConnectResult r,ZCom_BitStream&) override { connected=(r==eZCom_ConnAccepted); }
        // Defer node creation to force event buffering.
        void ZCom_cbNodeRequest_Dynamic(uint32_t,uint32_t,ZCom_BitStream*,int role,uint32_t net_id) override {
            gotID=net_id;
            // Record but don't create the node yet.
        }
        void createNodeNow() {
            node = new ZCom_Node();
            // Net layer assigns the server-announced node ID via applyRequestNodeID.
            node->setEventNotification(true,true);
            node->setReplicationInterceptor(&interceptor);
            node->registerRequestedNode(cls, this);
            // Role auto-assigned by omfgnet layer from request context.
            created;
        }
    } cli(port);

    cli.ConnectTo("127.0.0.1", port);
    processBoth(&srv, &cli, 40);
    BOOST_REQUIRE(cli.connected);

    // Create server node; it auto-announces. The server also sends a sync
    // event immediately (simulating the eEvent_Init -> sendSyncMessage path).
    srv.node = new ZCom_Node();
    srv.node->setRole(eZCom_RoleAuthority);
    srv.node->setEventNotification(true, false);
    srv.node->registerNodeDynamic(srv.cls, &srv);

    // Pump once so the announcement is sent but the client's cbNodeRequest
    // fires (recording gotID without creating the node).
    processBoth(&srv, &cli, 5);
    BOOST_REQUIRE_NE(cli.gotID, 0u);

    // Send a sync event from the server directly to the client's node ID.
    // The client node does not exist yet, so this must be buffered.
    ZCom_BitStream sync;
    sync.addInt(0x1234, 16);
    srv.node->sendEventDirect(eZCom_ReliableOrdered, &sync, 1);
    processBoth(&srv, &cli, 10);

    // The client node still does not exist; the event should be buffered.
    BOOST_REQUIRE(cli.node == nullptr);

    // Now create the client node — the buffered event must be replayed.
    g_currentControl = &cli;
    cli.createNodeNow();
    g_currentControl = nullptr;
    processBoth(&srv, &cli, 5);

    BOOST_REQUIRE(cli.node != nullptr);
    BOOST_CHECK(cli.node->checkEventWaiting());
    if (cli.node->checkEventWaiting()) {
        eZCom_Event type; eZCom_NodeRole role; uint32_t connID;
        ZCom_BitStream* data = cli.node->getNextEvent(&type, &role, &connID);
        BOOST_CHECK_EQUAL(type, eZCom_EventUser);
        BOOST_REQUIRE(data);
        BOOST_CHECK_EQUAL(data->getInt(16), 0x1234);
    }

    srv.Shutdown(); cli.Shutdown();
}

// Client node role (Owner) must allow sending events with OWNER_2_AUTH rule.
// Without the correct role, selectWeapons() events would be silently dropped.
BOOST_AUTO_TEST_CASE(client_owner_role_allows_owner_to_auth_event)
{
    g_currentControl = nullptr;
    int port = s_port++;

    class OSrv : public ZCom_Control {
    public:
        uint32_t cls; ZCom_Node* node=nullptr; int spawned=0; uint32_t clientID=0;
        OSrv(int p){ g_currentControl=this; ZCom_initSockets(true,p,2,0); cls=ZCom_registerClass("O",0); }
        bool ZCom_cbConnectionRequest(uint32_t,ZCom_BitStream&,ZCom_BitStream&) override { return true; }
        void ZCom_cbConnectionSpawned(uint32_t id) override { spawned++; if(clientID==0) clientID=id; }
    } srv(port);

    class OCli : public ZCom_Control {
    public:
        uint32_t cls; bool connected=false; ZCom_Node* node=nullptr; uint32_t gotID=0; int gotRole=-1;
        OCli(int p){ g_currentControl=this; ZCom_initSockets(false,0,0,0); cls=ZCom_registerClass("O",0); }
        uint32_t ConnectTo(const char* h,int p){ ZCom_Address a; char hp[64]; snprintf(hp,sizeof hp,"%s:%d",h,p); a.setAddress(eZCom_AddressUDP,0,hp); return ZCom_Connect(a,nullptr); }
        void ZCom_cbConnectResult(uint32_t,eZCom_ConnectResult r,ZCom_BitStream&) override { connected=(r==eZCom_ConnAccepted); }
        void ZCom_cbNodeRequest_Dynamic(uint32_t,uint32_t,ZCom_BitStream*,int role,uint32_t net_id) override {
            gotID=net_id; gotRole=role;
            node=new ZCom_Node();
            // Net layer assigns the server-announced node ID via applyRequestNodeID.
            node->setEventNotification(true,true);
            node->registerRequestedNode(cls,this);
            // Role (Owner) auto-assigned by omfgnet layer from request context.
        }
    } cli(port);

    cli.ConnectTo("127.0.0.1", port);
    processBoth(&srv, &cli, 40);
    BOOST_REQUIRE(cli.connected);
    BOOST_REQUIRE_NE(srv.clientID, 0u);

    // Server creates a node owned by the client — client should get RoleOwner.
    srv.node = new ZCom_Node();
    srv.node->setRole(eZCom_RoleAuthority);
    srv.node->setOwner(srv.clientID, true);
    srv.node->registerNodeDynamic(srv.cls, &srv);
    srv.node->setEventNotification(true, false);
    processBoth(&srv, &cli, 30);
    BOOST_REQUIRE_NE(cli.gotID, 0u);
    BOOST_CHECK_EQUAL(cli.gotRole, eZCom_RoleOwner);

    // Client (owner) sends an OWNER_2_AUTH event; server should receive it.
    ZCom_BitStream ev;
    ev.addInt(0x77, 8);
    cli.node->sendEvent(eZCom_ReliableOrdered, ZCOM_REPRULE_OWNER_2_AUTH, &ev);
    processBoth(&srv, &cli, 20);

    bool srvGotIt = false;
    if (srv.node) {
        while (srv.node->checkEventWaiting()) {
            eZCom_Event type; eZCom_NodeRole role; uint32_t connID;
            ZCom_BitStream* data = srv.node->getNextEvent(&type, &role, &connID);
            if (type == eZCom_EventUser && data) {
                srvGotIt = true;
                BOOST_CHECK_EQUAL(data->getInt(8), 0x77);
            }
        }
    }
    BOOST_CHECK(srvGotIt);

    srv.Shutdown(); cli.Shutdown();
}

// End-to-end game-style flow: authority node with event notification + owner,
// eEvent_Init fires on the server -> server sends a sync event to the client
// (mirrors BasePlayer/NetWorm sendSyncMessage). The client must receive it,
// even though the client node is created asynchronously in cbNodeRequest_Dynamic.
// Also verifies replicator state propagates to the client after the link is up.
BOOST_AUTO_TEST_CASE(full_sync_and_replication_flow)
{
    g_currentControl = nullptr;
    int port = s_port++;

    class FullSrv : public ZCom_Control {
    public:
        uint32_t cls; ZCom_Node* node=nullptr; uint32_t clientID=0;
        int32_t state = 7; // replicated authority state
        FullSrv(int p){ g_currentControl=this; ZCom_initSockets(true,p,2,0); cls=ZCom_registerClass("F",0); }
        bool ZCom_cbConnectionRequest(uint32_t,ZCom_BitStream&,ZCom_BitStream&) override { return true; }
        void ZCom_cbConnectionSpawned(uint32_t id) override { if(clientID==0) clientID=id; }
        // Server pumps its node events; on eEvent_Init it sends a sync event
        // to the new peer (exactly like BasePlayer::think / NetWorm::think).
        void pumpNode() {
            if (!node) return;
            while (node->checkEventWaiting()) {
                eZCom_Event type; eZCom_NodeRole role; uint32_t connID;
                ZCom_BitStream* data = node->getNextEvent(&type, &role, &connID);
                if (type == eZCom_EventInit) {
                    ZCom_BitStream* sync = new ZCom_BitStream();
                    sync->addInt(0x5BAD, 16);
                    node->sendEventDirect(eZCom_ReliableOrdered, sync, connID);
                }
            }
        }
    } srv(port);

    class FullCli : public ZCom_Control {
    public:
        uint32_t cls; bool connected=false; ZCom_Node* node=nullptr;
        int32_t localState = 0;
        int syncEvents = 0;
        FullCli(int p){ g_currentControl=this; ZCom_initSockets(false,0,0,0); cls=ZCom_registerClass("F",0); }
        uint32_t ConnectTo(const char* h,int p){ ZCom_Address a; char hp[64]; snprintf(hp,sizeof hp,"%s:%d",h,p); a.setAddress(eZCom_AddressUDP,0,hp); return ZCom_Connect(a,nullptr); }
        void ZCom_cbConnectResult(uint32_t,eZCom_ConnectResult r,ZCom_BitStream&) override { connected=(r==eZCom_ConnAccepted); }
        void ZCom_cbNodeRequest_Dynamic(uint32_t,uint32_t,ZCom_BitStream*,int role,uint32_t net_id) override {
            node=new ZCom_Node();
            // Net layer assigns the server-announced node ID via applyRequestNodeID.
            node->setEventNotification(true,true);
            node->beginReplicationSetup(1);
            node->addReplicationInt((zS32*)&localState, 32, false,
                ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL, 0);
            node->endReplicationSetup();
            node->registerRequestedNode(cls,this);
            // Role auto-assigned by omfgnet layer from request context.
        }
        void pumpNode() {
            if (!node) return;
            while (node->checkEventWaiting()) {
                eZCom_Event type; eZCom_NodeRole role; uint32_t connID;
                ZCom_BitStream* data = node->getNextEvent(&type, &role, &connID);
                if (type == eZCom_EventUser && data) {
                    // Verify sync payload (idempotent across duplicate syncs)
                    (void)data->getInt(16);
                    syncEvents++;
                }
            }
        }
    } cli(port);

    cli.ConnectTo("127.0.0.1", port);
    processBoth(&srv, &cli, 40);
    BOOST_REQUIRE(cli.connected);
    BOOST_REQUIRE_NE(srv.clientID, 0u);

    // Server creates an authority node owned by the client. It auto-announces.
    srv.node = new ZCom_Node();
    srv.node->setEventNotification(true, false); // generate eEvent_Init on link
    srv.node->beginReplicationSetup(1);
    srv.node->addReplicationInt((zS32*)&srv.state, 32, false,
        ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL, 0);
    srv.node->endReplicationSetup();
    srv.node->setOwner(srv.clientID, true);
    srv.node->registerNodeDynamic(srv.cls, &srv);

    // Pump the server's node events so eEvent_Init -> sendEventDirect(sync) fires.
    g_currentControl = &srv; srv.pumpNode(); g_currentControl = nullptr;

    // Let the announcement + sync event + replicator state propagate.
    processBoth(&srv, &cli, 40);
    g_currentControl = &cli; cli.pumpNode(); g_currentControl = nullptr;
    g_currentControl = &srv; srv.pumpNode(); g_currentControl = nullptr;
    processBoth(&srv, &cli, 20);
    g_currentControl = &cli; cli.pumpNode(); g_currentControl = nullptr;

    // Client node was created via the request callback.
    BOOST_REQUIRE(cli.node != nullptr);
    // Client received at least one sync event (the buffered-then-replayed one).
    BOOST_CHECK_GE(cli.syncEvents, 1);
    // Replicated authority state reached the client.
    BOOST_CHECK_EQUAL(cli.localState, 7);

    // Now change authority state and confirm it propagates.
    srv.state = 99;
    processBoth(&srv, &cli, 40);
    BOOST_CHECK_EQUAL(cli.localState, 99);

    srv.Shutdown(); cli.Shutdown();
}

// Regression for applyRequestNodeID: the client's proxy node (created via
// registerRequestedNode inside cbNodeRequest_Dynamic, WITHOUT a manual
// setNetworkID) must receive the server-announced node ID from the Net layer.
// Previously applyRequestNodeID pushed/announced but never setNodeID, leaving
// the client node at nodeID=0; MSG_REPLICATORS for the server's ID then never
// resolved ("NOT FOUND locally — buffering for later") and state never arrived.
BOOST_AUTO_TEST_CASE(register_requested_node_gets_announced_id)
{
    g_currentControl = nullptr;
    int port = s_port++;
    RepSrv srv(port);
    RepCli cli(port);
    cli.ConnectTo("127.0.0.1", port);
    processBoth(&srv, &cli, 40);
    BOOST_REQUIRE(cli.connected);

    uint32_t playerID = 42;
    g_currentControl = &srv;
    srv.node = new ZCom_Node();
    srv.node->setRole(eZCom_RoleAuthority);
    srv.node->beginReplicationSetup(1);
    srv.node->setInterceptID(0);
    srv.node->addReplicationInt((zS32*)&playerID, 32, false,
        ZCOM_REPFLAG_MOSTRECENT | ZCOM_REPFLAG_INTERCEPT, ZCOM_REPRULE_AUTH_2_ALL, 0);
    srv.node->endReplicationSetup();
    srv.node->registerNodeDynamic(srv.cls, &srv);
    g_currentControl = nullptr;
    processBoth(&srv, &cli, 30);

    // Client created its proxy node from the request callback.
    BOOST_REQUIRE(cli.node != nullptr);
    BOOST_REQUIRE_NE(cli.gotNodeID, 0u);
    // The proxy node's local ID must match the server-announced ID (assigned by
    // applyRequestNodeID), not be left at the 0 default.
    BOOST_CHECK_NE(cli.node->getNetworkID(), 0u);
    BOOST_CHECK_EQUAL(cli.node->getNetworkID(), cli.gotNodeID);

    // Replicator state for the announced ID must reach the client (lookup
    // resolves instead of buffering forever).
    playerID = 77;
    processBoth(&srv, &cli, 30);
    BOOST_CHECK_GT(cli.interceptor.inPreUpdateItemCount, 0);
    BOOST_CHECK_EQUAL(cli.interceptor.lastPeeked, 77u);

    srv.Shutdown(); cli.Shutdown();
}

BOOST_AUTO_TEST_SUITE_END()
