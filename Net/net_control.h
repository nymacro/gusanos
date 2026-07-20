#ifndef NET_CONTROL_H
#define NET_CONTROL_H

#include "net_types.h"
#include "net_node.h"
#include "net_address.h"
#include "net_bitstream.h"
#include <vector>
#include <map>
#include <set>
#include <unordered_set>
#include <string>
#include <cstdint>
#include <fstream>
#include <enet/enet.h>

// C1: map a Zoidcom send mode to ENet packet flags (ReliableUnordered ->
// RELIABLE | UNSEQUENCED). Declared here so unit tests can call it directly.
enet_uint32 enetPacketFlags(eZCom_SendMode mode);

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
static const int MSG_NODE_EVENT = 104;

static const int MSG_REPLICATORS = 105;
static const int MSG_DISCONNECT_DATA = 106;
// File transfer protocol (Phase E)
static const int MSG_FILE_OFFER = 107;    // sender -> receiver: start a transfer
static const int MSG_FILE_ACCEPT = 108;   // receiver -> sender: accept/deny
static const int MSG_FILE_DATA = 109;     // sender -> receiver: file chunk
static const int MSG_FILE_ABORT = 110;    // either side: abort
static const int MSG_FILE_COMPLETE = 111; // sender -> receiver: all chunks sent
static const int MSG_DOWNSTREAM_REQUEST = 112; // peer asks us to cap our upstream to it (T1.1)

// Wire-protocol version. Stamped by the server into MSG_CONNECTION_REPLY and
// checked by the client; a mismatch refuses the connection with
// eZCom_ConnWrongVersion. Bump this whenever the wire format changes
// (e.g. string encoding, replicator slot tagging, new channel semantics).
// Current version 2 adds the MSG_DOWNSTREAM_REQUEST control message (T1.1).
// Future Tier-2/3 wire-format changes (T2.6/T2.7/T3.2) must bump this further
// and gate new behavior on it.
static const int PROTOCOL_VERSION = 2;

// Per-connection emulation state (T1.2 lag/loss). Bandwidth limiting is handled
// entirely by ENet (enet_host_bandwidth_limit); the former app-level limiter was
// removed because it destroyed reliable packets (networked rubber-banding).
struct PeerNetState {
	zU32 upstreamLimit = 0;   // recorded "downstream limit" request from this peer
	                        // (set when the peer sends MSG_DOWNSTREAM_REQUEST). ENet
	                        // is the bandwidth authority; this is retained only as a
	                        // record of the peer's request.
	zU32 lagMsec = 0;         // T1.2: defer outgoing packets by this many ms (0 = off)
	zFloat lossPct = 0.0f;    // T1.2: drop this fraction [0,1] of outgoing packets
};

// A packet deferred by T1.2 lag emulation, awaiting its send time.
struct PendingLagSend {
	uint32_t connID;
	ENetPeer* peer;
	int channel;
	ENetPacket* packet;
	uint32_t sendAt;          // ZCom_getCurrentTime() at which to transmit
};

class ZCom_Control {

public:
	ZCom_Control();
	virtual ~ZCom_Control();

	// Class registration
	ZCom_ClassID ZCom_registerClass(const char* name, zU32 flags = 0);
	ZCom_ClassID ZCom_getClassID(const char* name) const;
	std::string ZCom_getClassName(uint32_t classID) const;

	// Main loop
	void ZCom_processOutput();
	void ZCom_processInput(eZCom_BlockMode _block = eZCom_NoBlock);

	// Connection
	ZCom_ConnID ZCom_Connect(const ZCom_Address& addr, ZCom_BitStream* data);
	void ZCom_disconnectAll(ZCom_BitStream* data);
	void ZCom_Disconnect(ZCom_ConnID id, ZCom_BitStream* data);
	void Shutdown();
	void disconnectPeer(uint32_t connID);

	// Peer info
	const ZCom_Address* ZCom_getPeer(ZCom_ConnID id) const;
	const ZCom_ConnStats& ZCom_getConnectionStats(ZCom_ConnID id) const;

	// Sending
	void ZCom_sendData(ZCom_ConnID connID, ZCom_BitStream* stream, eZCom_SendMode mode = eZCom_ReliableOrdered);
	void sendToAll(int mode, ZCom_BitStream* stream);

	// Node management
	bool registerNode(ZCom_Node* node);
	bool registerExistingNode(ZCom_Node* node); // register without reassigning node ID
	void removeNode(ZCom_Node* node);
	void sendNodeAnnouncement(uint32_t connID, ZCom_Node* node, int role);
	void announceNodeWithOwner(ZCom_Node* node); // announce with owner-aware roles
	void announceNodeToAll(ZCom_Node* node, int role);
	void clearAnnouncedNode(uint32_t nodeID); // clear per-peer tracking for re-announce
	void flushPendingAnnounces(); // flush deferred node announcements to peers

	// Enum types (re-exported from network_compat.h)
	using eZCom_SendMode = ::eZCom_SendMode;  // alias to file-scope enum (net_types.h)

	// Address type: nested alias so `ZCom_Control::eZCom_AddressUDP`
	// keeps resolving (the enum is now defined at file scope in net_types.h).
	static constexpr eZCom_AddressType eZCom_AddressUDP = ::eZCom_AddressUDP;

	// Internal: set the ENetHost
	void setHost(ENetHost* host, bool isServer = false) {
		m_host = host;
		m_isServer = isServer;
	}
	ENetHost* getHost() { return m_host; }

	// Find peer by connection ID
	ENetPeer* findPeer(uint32_t connID) const;

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

	// Wire-protocol version this peer advertises (server reply) and accepts
	// (client check). Defaults to PROTOCOL_VERSION. Overridable by tests to
	// exercise the version-mismatch refusal path; production never overrides.
	virtual int getProtocolVersion() const { return PROTOCOL_VERSION; }

	// Group manager
	ZCom_ConnGroupManager& ZCom_getGroupManager() { return m_groupManager; }

	// --- Reference API members (B1b): game-unused; stubs/delegations ---
	bool ZCom_initSockets(bool _useudp, zU16 _udpport, zU16 _localport, zU8 _control_id_size = 0);
	void ZCom_setControlID(zU8 _id);
	void ZCom_setDebugName(const char* _name);
	void ZCom_setUpstreamLimit(zU32 _total_bps, zU32 _perconn_bps);
	void ZCom_setConnectionTimeout(zU32 _ms);  // C6: re-applies to all live peers
	void ZCom_requestDownstreamLimit(ZCom_ConnID _id, zU16 _pps, zU16 _bpp);
	void ZCom_simulateLag(ZCom_ConnID _id, zU32 _lagmsec);
	void ZCom_simulateLoss(ZCom_ConnID _id, zFloat _amount);
	bool ZCom_requestZoidMode(ZCom_ConnID _id, zU8 _level);
	bool ZCom_Discover(const ZCom_Address& _address, ZCom_BitStream* _request);
	void ZCom_setDiscoverListener(eZCom_DiscoverOpt _opt, zU16 _discoverport);
	void ZCom_setUserData(ZCom_ConnID _id, void* _data);
	void* ZCom_getUserData(ZCom_ConnID _id) const;
	static zU32 ZCom_getCurrentTime();
	void ZCom_sendDataToGroup(ZCom_GroupID _gid, ZCom_BitStream* _stream, eZCom_SendMode _mode);
	void ZCom_sendDataRaw(ZCom_Address& _dest, void* _data, zU32 _size);
	zU8 ZCom_getControlID() const { return m_controlID; }
	const std::string& ZCom_getDebugName() const { return m_debugName; }

	// Access to registered nodes
	const std::vector<ZCom_Node*>& getNodesConst() const { return m_nodes; }

	// Access to peer map for iteration
	const std::map<uint32_t, ENetPeer*>& getPeers() const { return m_peerMap; }

	// Node lookup
	ZCom_Node* ZCom_getNode(ZCom_NodeID nid) const;

	/// Returns the role the peer at connID holds for the given node, or
	/// eZCom_RoleUndefined if the node was never announced to that peer.
	/// Used by per-connection reprule routing (Phase D1/D2) and tests.
	eZCom_NodeRole getPeerRole(ZCom_ConnID connID, ZCom_NodeID nid) const;

	/// Route a node event packet to peers whose role for `nid` matches `rules`
	/// under local role `localRole`. `rules==0` preserves legacy broadcast
	/// semantics (send to all peers). Phase D2.
	void routeNodeEvent(ZCom_NodeID nid, eZCom_NodeRole localRole, zU8 rules,
	                    eZCom_SendMode mode, ZCom_BitStream& pkt);

	// --- File transfer (Phase E) ---
	// Sender side: open the local file, allocate a transfer id, send the offer
	// to the destination connection. `node` is the node the events will be
	// delivered to on the peer (its networkID is carried in the offer).
	ZCom_FileTransID ZCom_sendFile(ZCom_Node* node, const char* _path,
		const char* _pathtosend, ZCom_ConnID _destconn,
		ZCom_BitStream* _data, zFloat _aggressivenes);
	// Receiver side: accept (open local file for writing at `_path` or the
	// received path) or deny (send abort + raise File_Aborted locally).
	void ZCom_acceptFile(ZCom_Node* node, ZCom_ConnID _src_id,
		ZCom_FileTransID _ftrans_id, const char* _path, bool _accept);
	// Lookup progress info; returns a struct with id==ZCom_Invalid_ID if unknown.
	const ZCom_FileTransInfo& ZCom_getFileInfo(ZCom_ConnID _conn_id,
		ZCom_FileTransID _ftrans_id) const;
	// Drive ongoing sender transfers (chunked reliable sends). Called from
	// ZCom_processOutput.
	void pumpFileTransfers();
	// Deliver buffered file offers whose target node has just been registered.
	void deliverPendingFileOffers(ZCom_Node* node);

	// Replicator processing
	void ZCom_processReplicators(uint32_t simulation_time_passed);

	// Replay buffered replicator data for a newly registered node
	void replayPendingReplicators(ZCom_Node* node);

	/// Apply the current node-request context's role to a node, if any.
	/// Called by ZCom_Node during registration inside cbNodeRequest_Dynamic.
	void applyRequestRole(ZCom_Node* node);

	/// Apply the current node-request context's node ID to a node, if any.
	/// Called by ZCom_Node during registration inside cbNodeRequest_Dynamic.
	void applyRequestNodeID(ZCom_Node* node);

	/// True while the control is inside a ZCom_cbNodeRequest_Dynamic callback.
	bool isRequestActive() const { return m_requestCtx.active; }

protected:
	ENetHost* m_host;
	bool m_isServer;
	uint32_t m_nextConnID;
	uint32_t m_nextNodeID;
	uint32_t m_nextClassID;
	std::vector<ZCom_ClassInfo> m_classes;
	std::vector<ZCom_Node*> m_nodes;          ///< non-owning (game owns the nodes)
	std::map<uint32_t, ENetPeer*> m_peerMap;  ///< non-owning (ENet owns the peers)
	std::map<uint32_t, ZCom_Address> m_addressMap;
	mutable std::map<ZCom_ConnID, ZCom_ConnStats> m_statsCache; ///< Cache for ZCom_getConnectionStats (reference returns const&)
	zU8 m_controlID = 0;
	std::string m_debugName;
	std::map<ZCom_ConnID, void*> m_userData; ///< Per-connection user data (ZCom_setUserData/getUserData)
	ZCom_BitStream m_disconnectData;
	std::map<uint32_t, ZCom_BitStream> m_pendingDisconnectData; ///< Pre-disconnect reason data per connID
	std::set<uint32_t> m_waitingForReply; ///< Client connIDs waiting for connection reply
	std::map<uint32_t, std::set<uint32_t>> m_announcedNodes; ///< Per-peer set of node IDs already announced

	/// Node IDs registered by registerNode() whose announcement has been
	/// deferred to the next ZCom_processOutput(), so a subsequent setOwner()
	/// (same tick) is reflected as a single owner-aware announce instead of a
	/// premature Proxy announce followed by an Owner re-announce.
	std::set<uint32_t> m_pendingAnnounce;

	/// Nodes whose removal has been deferred to the next ZCom_processOutput().
	/// removeNode() adds here instead of erasing m_nodes directly, so that
	/// destructors triggered mid-iteration (e.g. Game::reset -> deleteThis ->
	/// delete m_node) don't invalidate iterators in active range-for loops
	/// over m_nodes. The actual erase happens at the top of ZCom_processOutput.
	std::unordered_set<ZCom_Node*> m_pendingRemove;

	/// Per-connection role each peer holds for each node (Phase D1 routing).
	/// m_peerRole[connID][nodeID] = the role the peer at connID has for nodeID.
	/// Set by every announcement path; cleared on disconnect / re-announce.
	std::map<uint32_t, std::map<uint32_t, eZCom_NodeRole>> m_peerRole;

	/// Per-connection emulation state (T1.2 lag/loss).
	std::map<uint32_t, PeerNetState> m_peerState;
	std::vector<PendingLagSend> m_lagQueue;      ///< T1.2: packets deferred by lag emulation

	/// Buffered replicator data for nodes that haven't been registered locally yet.
	/// Keyed by nodeID; replayed when the node is registered via registerExistingNode or registerNode.
	std::map<uint32_t, ZCom_BitStream> m_pendingReplicas;

	/// Buffered node events (e.g. sync messages) that arrived before the target
	/// node was registered locally. Keyed by nodeID; replayed on registration.
	struct PendingNodeEvent {
		int type;
		int role;
		uint32_t connID;
		ZCom_BitStream data;
		zU32 estimatedTimeSent = 0;  // ENet-RTT estimate, for replayed events
	};
	std::map<uint32_t, std::vector<PendingNodeEvent>> m_pendingNodeEvents;

	/// Replay buffered node events for a newly registered node.
	void replayPendingNodeEvents(ZCom_Node* node);

	/// Per-transfer state for file transfer (Phase E). A transfer lives in the
	/// control that owns it: the sender control holds the outbound (isReceiver
	/// ==false) entry; the receiver control holds the inbound (isReceiver
	/// ==true) entry. Both use the same ZCom_FileTransID (allocated by sender).
	struct FileTransfer {
		ZCom_FileTransID id = 0;
		ZCom_NodeID nodeID = 0;       ///< node whose event queue receives File_* events
		bool isReceiver = false;
		ZCom_ConnID peerConnID = 0;  ///< destconn (sender) / srcconn (receiver)
		std::string path;            ///< sender: local file path; receiver: save path
		std::string pathtosend;      ///< path advertised to the peer
		zU32 size = 0;
		zU32 transferred = 0;
		zU32 bps = 0;
		zFloat aggressiveness = 1.0f;
		bool accepted = false;       ///< sender: receiver accepted the offer
		bool aborted = false;
		bool done = false;
		std::vector<uint8_t> offerData; ///< captured _data bytes (receiver: for Incoming event)
		std::ifstream inFile;       ///< sender: source file
		std::ofstream outFile;      ///< receiver: destination file
		zU32 bytesThisSec = 0;
		zU32 lastBpsTime = 0;
	};
	std::map<ZCom_FileTransID, FileTransfer> m_fileTransfers;
	/// Offers that arrived before the target node was registered locally,
	/// keyed by nodeID; delivered when the node appears.
	std::map<ZCom_NodeID, std::vector<ZCom_FileTransID>> m_pendingFileOffers;
	mutable ZCom_FileTransInfo m_fileInfoBuf; ///< backing store for ZCom_getFileInfo return
	ZCom_FileTransID m_nextFileTransID = 1;

protected:
	/// Node-request context: while the control is inside a
	/// ZCom_cbNodeRequest_Dynamic callback, this holds the requesting
	/// connection and the role announced by the server. Registration calls
	/// made within that callback (registerNodeDynamic/registerRequestedNode)
	/// auto-assign this role to the new node, matching Zoidcom semantics where
	/// the node's local role is determined by registration context.
	struct NodeRequestContext {
		bool active = false;
		uint32_t connID = 0;
		int role = 0;
		uint32_t net_id = 0;
	};
	NodeRequestContext m_requestCtx;

	ZCom_ConnGroupManager m_groupManager;

	void processENetEvent(ENetEvent& event);
	// T1.1/T1.2: centralized outgoing-packet path applying lag/loss/upstream
	// limiting. `critical` packets (connect/announce/events) bypass emulation
	// and limiting so connections stay alive under load.
	void sendPacket(uint32_t connID, ENetPeer* peer, int channel, ENetPacket* packet, bool critical);
	void drainLagQueue();
	void dropPeerState(uint32_t connID);  // erase per-conn state + queued lag packets
	void dispatchNodeEvent(uint32_t nodeID, int type, int role, uint32_t connID, ZCom_BitStream* data, zU32 estimatedTimeSent = 0);
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