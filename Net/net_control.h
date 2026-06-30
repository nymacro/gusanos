#ifndef NET_CONTROL_H
#define NET_CONTROL_H

#include "net_types.h"
#include "net_node.h"
#include "net_address.h"
#include "net_bitstream.h"
#include <vector>
#include <map>
#include <set>
#include <string>
#include <cstdint>
#include <enet/enet.h>

struct ZCom_ConnStats {
	int avg_ping;
	int min_ping;
	int max_ping;
	int last_sec_out;
	int last_sec_in;
	int current_out;
	int current_in;
	int total_out;
	int total_in;
	int last_sec_loss_percent;
	int current_loss_count;
};

// Registered class info
struct ZCom_ClassInfo {
	std::string name;
	uint32_t flags;
};

// Node registration helper
struct NodeRegistration {
	ZCom_Node* node;
	uint32_t classID;
	int role;
	uint32_t connID;
	bool isDynamic;
};

// Internal protocol message types (values >= 100 to avoid conflict with game messages)
static const int MSG_CONNECTION_REPLY = 100;
static const int MSG_ZOID_REQUEST = 101;
static const int MSG_ZOID_RESULT = 102;
static const int MSG_NODE_ANNOUNCE = 103;
// Node event prefix (8 bits = 0, followed by 16-bit nodeID + payload)
static const int MSG_NODE_EVENT = 0;

static const int MSG_REPLICATORS = 105;
static const int MSG_DISCONNECT_DATA = 106;

class ZCom_Control {

public:
	ZCom_Control();
	virtual ~ZCom_Control();

	// Class registration
	uint32_t ZCom_registerClass(const char* name, uint32_t flags);
	uint32_t ZCom_getClassID(const char* name) const;

	// Main loop
	void ZCom_processOutput();
	void ZCom_processInput(int flags);

	// Connection
	uint32_t ZCom_Connect(ZCom_Address& addr, ZCom_BitStream* data);
	void ZCom_disconnectAll(ZCom_BitStream* data);
	void ZCom_Disconnect(uint32_t id, ZCom_BitStream* data);
	void Shutdown();
	void disconnectPeer(uint32_t connID);

	// Peer info
	ZCom_Address const* ZCom_getPeer(uint32_t id);
	ZCom_ConnStats ZCom_getConnectionStats(uint32_t id);

	// Sending
	void ZCom_sendData(uint32_t connID, ZCom_BitStream* stream, int mode);
	void sendToAll(int mode, ZCom_BitStream* stream);

	// Node management
	bool registerNode(ZCom_Node* node);
	bool registerExistingNode(ZCom_Node* node); // register without reassigning node ID
	void removeNode(ZCom_Node* node);
	void sendNodeAnnouncement(uint32_t connID, ZCom_Node* node, int role);
	void announceNodeWithOwner(ZCom_Node* node); // announce with owner-aware roles
	void announceNodeToAll(ZCom_Node* node, int role);
	void syncNodesToPeer(ENetPeer* peer);

	// Enum types (re-exported from network_compat.h)
	using eZCom_SendMode = int;

	// Address type
	static const int eZCom_AddressUDP = 0;

	// Internal: set the ENetHost
	void setHost(ENetHost* host) { m_host = host; }
	ENetHost* getHost() { return m_host; }

	// Find peer by connection ID
	ENetPeer* findPeer(uint32_t connID);

	// Callbacks (overridden by Server/Client)
	virtual bool ZCom_cbConnectionRequest(uint32_t id, ZCom_BitStream& request, ZCom_BitStream& reply) { return true; }
	virtual void ZCom_cbConnectionSpawned(uint32_t id) {}
	virtual void ZCom_cbConnectionClosed(uint32_t id, eZCom_CloseReason reason, ZCom_BitStream& reasondata) {}
	virtual void ZCom_cbDataReceived(uint32_t id, ZCom_BitStream& data) {}
	virtual bool ZCom_cbZoidRequest(uint32_t id, uint8_t requested_level, ZCom_BitStream& reason) { return false; }
	virtual void ZCom_cbZoidResult(uint32_t id, eZCom_ZoidResult result, uint8_t new_level, ZCom_BitStream& reason) {}
	virtual void ZCom_cbConnectResult(uint32_t id, eZCom_ConnectResult result, ZCom_BitStream& reply) {}
	virtual void ZCom_cbNodeRequest_Dynamic(uint32_t id, uint32_t requested_class, ZCom_BitStream* announcedata, int role, uint32_t net_id) {}
	virtual void ZCom_cbNodeRequest_Tag(uint32_t id, uint32_t requested_class, ZCom_BitStream* announcedata, int role, uint32_t tag) {}
	virtual void ZCom_cbDiscovered(const ZCom_Address& addr, ZCom_BitStream& reply) {}
	virtual bool ZCom_cbDiscoverRequest(const ZCom_Address& addr, ZCom_BitStream& request, ZCom_BitStream& reply) { return false; }

	// I/O
	void setLogFunction(void (*logFn)(const char*)) { m_logFn = logFn; }
	void (*m_logFn)(const char*);

	// Group manager
	ZCom_ConnGroupManager& ZCom_getGroupManager() { return m_groupManager; }

	// Access to registered nodes
	const std::vector<ZCom_Node*>& getNodesConst() const { return m_nodes; }

	// Access to peer map for iteration
	const std::map<uint32_t, ENetPeer*>& getPeers() const { return m_peerMap; }

	// Node lookup
	ZCom_Node* ZCom_getNode(uint32_t nid);

	// Replicator processing
	void ZCom_processReplicators(uint32_t simulation_time_passed);

protected:
	ENetHost* m_host;
	uint32_t m_nextConnID;
	uint32_t m_nextNodeID;
	uint32_t m_nextClassID;
	std::vector<ZCom_ClassInfo> m_classes;
	std::vector<ZCom_Node*> m_nodes;
	std::map<uint32_t, ENetPeer*> m_peerMap;
	std::map<uint32_t, ZCom_Address> m_addressMap;
	ZCom_BitStream m_disconnectData;
	std::map<uint32_t, ZCom_BitStream> m_pendingDisconnectData; ///< Pre-disconnect reason data per connID
	std::set<uint32_t> m_waitingForReply; ///< Client connIDs waiting for connection reply

	ZCom_ConnGroupManager m_groupManager;

	void processENetEvent(ENetEvent& event);
	void dispatchNodeEvent(uint32_t nodeID, int type, int role, uint32_t connID, ZCom_BitStream* data);
	uint32_t allocateConnID();
	void sendConnectionReply(ENetPeer* peer, ZCom_BitStream& reply, bool accepted);
};

// Global ZoidCom functions
bool ZCom_initSockets(bool isServer, int port, int maxClients, int something);
void ZCom_simulateLag(int val, int lag);
void ZCom_simulateLoss(int val, float loss);
void ZCom_setControlID(int id);
void ZCom_setDebugName(const char* name);
void ZCom_setUpstreamLimit(int limit, int something);
void ZCom_sendData(uint32_t connID, ZCom_BitStream* stream, eZCom_SendMode mode);
void ZCom_requestDownstreamLimit(uint32_t connID, int pps, int bpp);
void ZCom_requestZoidMode(uint32_t connID, uint8_t level);

// Current control for global functions
extern ZCom_Control* g_currentControl;

#endif // NET_CONTROL_H