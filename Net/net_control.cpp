#include "net_control.h"
#include "net_bitstream.h"
#include "net_address.h"
#include <cstring>
#include <algorithm>
#include <iostream>
#include <memory>
#include <cstdlib>
#include <ctime>

// Verbose logging for network debugging
#define NET_DEBUG
#ifdef NET_DEBUG
#define NET_LOG(x_)                                                                                                    \
	do {                                                                                                               \
		std::cerr << "[NET] " << x_ << std::endl;                                                                      \
	} while (0)
#else
#define NET_LOG(x_) (void)0
#endif

// Current control for global function dispatch
ZCom_Control *g_currentControl = nullptr;

// C1: see net_control.h. Centralizes the send-mode -> ENet flag mapping so
// ZCom_sendData / sendToAll / routeNodeEvent stay in sync and it is unit-testable.
enet_uint32 enetPacketFlags(eZCom_SendMode mode) {
	switch (mode) {
		case eZCom_ReliableOrdered:
			return ENET_PACKET_FLAG_RELIABLE;
		case eZCom_ReliableUnordered:
			return ENET_PACKET_FLAG_RELIABLE | ENET_PACKET_FLAG_UNSEQUENCED;
		case eZCom_Unreliable:
		case eZCom_UnreliableNotify:
		default:
			return 0;
	}
}

namespace {
// Whether a replicator/event with the given reprule should flow from a node
// whose local role is `local` to a peer whose role for that node is `peer`.
// Mirrors Zoidcom reprule semantics (see zoidcom_node.h reprules).

bool repruleMatches(uint32_t rule, eZCom_NodeRole local, eZCom_NodeRole peer) {
	if (local == eZCom_RoleAuthority) {
		if (peer == eZCom_RoleProxy && (rule & ZCOM_REPRULE_AUTH_2_PROXY))
			return true;
		if (peer == eZCom_RoleOwner && (rule & ZCOM_REPRULE_AUTH_2_OWNER))
			return true;
		return false;
	}
	if (local == eZCom_RoleOwner) {
		if (peer == eZCom_RoleAuthority && (rule & ZCOM_REPRULE_OWNER_2_AUTH))
			return true;
		return false;
	}
	return false;
}

// ENet channel allocation. Traffic classes are separated so ENet's per-channel
// ordering/reliability doesn't let one class stall another: a reliable retransmit
// on the event/control channel must not delay position replicator updates, and
// level-download file chunks (channel 1) must not stall gameplay traffic.
enum { CH_CONTROL = 0, CH_FILE = 1, CH_REPLICATOR = 2 };

// Estimate the sender's local send timestamp of an incoming packet from ENet's
// measured round-trip time for the peer. Zoidcom's estimatedTimeSent is the
// sender's local send time; the receiver computes travel time as
// (ZoidCom::getTime() - estimatedTimeSent). We approximate the sender's send
// time as (now - rtt/2), giving a ~one-way-delay travel estimate. (A fully
// correct value would stamp the sender's clock into the packet, but this uses
// ENet's own RTT data with no wire-format change.)
static zU32 estimatedSendTimeFromPeer(ENetPeer *peer) {
	if (!peer || peer->roundTripTime == 0)
		return 0;
	return ZoidCom::getTime() - (peer->roundTripTime / 2);
}

// Append all written bits of `src` to `dst` at the bit level (no length
// prefix), so per-replicator payloads concatenate without byte-alignment
// gaps. The receiver reads bit-by-bit and stays aligned.
void appendBits(ZCom_BitStream &dst, ZCom_BitStream &src) {
	// Delegates to addBitStream, which resets the source read head and copies
	// all written bits — identical semantics to the former per-bit loop, plus
	// the byte-aligned memcpy fast path (A8) when dst is byte-aligned.
	dst.addBitStream(&src);
}

// Build a file-transfer event BitStream ([fid(ZCOM_FTRANS_ID_BITS)] + optional
// offer bytes) and push it onto the target node's event queue. `offerData` is
// only non-null for eZCom_EventFile_Incoming (carries the sender's _data).
void pushFileEvent(ZCom_Node *node, eZCom_Event type, ZCom_ConnID connID, ZCom_FileTransID fid,
				   const std::vector<uint8_t> *offerData) {
	if (!node)
		return;
	ZCom_BitStream ev;
	ev.addInt(fid, ZCOM_FTRANS_ID_BITS);
	if (offerData && !offerData->empty())
		ev.addBuffer(reinterpret_cast<const char *>(offerData->data()), static_cast<zU16>(offerData->size()));
	node->pushEvent(type, eZCom_RoleAuthority, connID, &ev);
}
} // namespace

ZCom_Control::ZCom_Control()
	: m_logFn(nullptr), m_host(nullptr), m_isServer(false), m_nextConnID(1), m_nextNodeID(1), m_nextClassID(1) {
	g_currentControl = this;
}

ZCom_Control::~ZCom_Control() {
	Shutdown();
	if (g_currentControl == this)
		g_currentControl = nullptr;
}

ZCom_ClassID ZCom_Control::ZCom_registerClass(const char *name, zU32 flags) {
	ZCom_ClassInfo info;
	info.name = name ? name : "";
	info.flags = flags;
	m_classes.push_back(info);
	return static_cast<uint32_t>(m_classes.size());
}

ZCom_ClassID ZCom_Control::ZCom_getClassID(const char *name) const {
	if (!name)
		return 0;
	for (size_t i = 0; i < m_classes.size(); ++i) {
		if (m_classes[i].name == name)
			return static_cast<uint32_t>(i + 1);
	}
	return 0;
}

std::string ZCom_Control::ZCom_getClassName(uint32_t classID) const {
	if (classID == 0 || classID > m_classes.size())
		return "(unknown)";
	return m_classes[classID - 1].name;
}

void ZCom_Control::ZCom_processOutput() {
	if (!m_host)
		return;
	g_currentControl = this;

	// Flush deferred node announcements before routing replicators, so a node
	// registered this tick (and possibly owner-assigned via setOwner) has its
	// per-peer role (m_peerRole) recorded before getPeerRole() is consulted.
	flushPendingAnnounces();

	// Both servers and clients may drive replicator output: authority nodes
	// send to proxies/owners (AUTH_2_*), and owner nodes send to the authority
	// (OWNER_2_AUTH). Proxy nodes never originate replicator data.
	m_nodeRegistry.forEach([&](ZCom_Node *node) {
		eZCom_NodeRole localRole = node->getRole();
		if (localRole != eZCom_RoleAuthority && localRole != eZCom_RoleOwner)
			return;

		uint32_t nid = node->getNetworkID();

		// Pre-pack each replicator once (clearing dirty), then route the
		// pre-packed bytes to matching peers. Re-calling packData per peer
		// would no-op since the first call cleared the dirty flag. The scratch
		// vector is a per-control member reused across nodes (A7):
		// packReplicatorsForRouting clears+reserves it, so only the first node
		// pays the reserve; capacity persists for subsequent nodes.
		node->packReplicatorsForRouting(m_routingPacked);
		auto &packed = m_routingPacked;

		bool anyUpdate = false;
		for (auto &p : packed)
			if (p.hasUpdate) {
				anyUpdate = true;
				break;
			}
		if (!anyUpdate)
			return;

		for (auto &pair : m_peerMap) {
			uint32_t connID = pair.first;
			eZCom_NodeRole peerRole = getPeerRole(connID, nid);
			if (peerRole == eZCom_RoleUndefined)
				continue; // node not announced to this peer

			// Build the per-peer packet directly into the reusable scratch
			// BitStream (A7): reset() preserves m_data capacity across peers,
			// and building the payload straight into the packet (header + 1-bit
			// flags + replicator data, in order) is bit-identical to the former
			// repPacked + addBitStream path, but avoids a per-peer payload
			// BitStream allocation and the bit-by-bit addBitStream copy.
			m_routingPkt.reset();
			m_routingPkt.addInt(MSG_REPLICATORS, 8);
			m_routingPkt.addInt(nid, 16);
			size_t payloadStart = m_routingPkt.getBitCount();
			bool peerHasData = false;
			for (auto &p : packed) {
				bool send = p.hasUpdate && repruleMatches(p.rule, localRole, peerRole);
				m_routingPkt.addInt(send ? 1 : 0, 1);
				if (send) {
					peerHasData = true;
					appendBits(m_routingPkt, p.data);
				}
			}
			if (!peerHasData)
				continue;

			zU32 payloadBits = static_cast<zU32>(m_routingPkt.getBitCount() - payloadStart);

			NET_LOG("Routing replicators nodeID=" << nid << " classID=" << node->getClassID() << " ("
												  << ZCom_getClassName(node->getClassID()) << ")"
												  << " local=" << localRole << " peer=" << peerRole
												  << " conn=" << connID << " bits=" << payloadBits);

			ENetPacket *packet =
				enet_packet_create(m_routingPkt.getData(), m_routingPkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
			ENetPeer *peer = findPeer(connID);
			if (peer)
				sendPacket(connID, peer, CH_REPLICATOR, packet, false);
			else
				enet_packet_destroy(packet);
		}
	});

	drainLagQueue();
	pumpFileTransfers();
	enet_host_flush(m_host);
}

void ZCom_Control::ZCom_processReplicators(uint32_t simulation_time_passed) {
	// T1.3 — drive per-replicator Process() callbacks (dead-reckoning /
	// interpolation hooks). Throttling (min/max delay) is applied at pack time
	// in ZCom_Node::packReplicatorsForRouting, which owns the dirty/send decision.
	m_nodeRegistry.forEach([&](ZCom_Node *node) { node->processReplicators(simulation_time_passed); });
}

ZCom_Node *ZCom_Control::ZCom_getNode(ZCom_NodeID nid) const {
	return m_nodeRegistry.find(nid);
}

eZCom_NodeRole ZCom_Control::getPeerRole(ZCom_ConnID connID, ZCom_NodeID nid) const {
	auto it = m_peerRole.find(connID);
	if (it == m_peerRole.end())
		return eZCom_RoleUndefined;
	auto nit = it->second.find(nid);
	if (nit == it->second.end())
		return eZCom_RoleUndefined;
	return nit->second;
}

void ZCom_Control::routeNodeEvent(ZCom_NodeID nid, eZCom_NodeRole localRole, zU8 rules, eZCom_SendMode mode,
								  ZCom_BitStream &pkt) {
	if (!m_host)
		return;

	// rule==0 keeps the legacy broadcast behaviour: several tests and the game
	// send events with no reprule and expect every peer to receive them.
	if (rules == 0) {
		sendToAll(static_cast<int>(mode), &pkt);
		return;
	}

	enet_uint32 flags = enetPacketFlags(mode);
	for (auto &pair : m_peerMap) {
		uint32_t connID = pair.first;
		eZCom_NodeRole peerRole = getPeerRole(connID, nid);
		if (peerRole == eZCom_RoleUndefined)
			continue; // node not announced to this peer
		if (!repruleMatches(rules, localRole, peerRole))
			continue;
		ENetPacket *packet = enet_packet_create(pkt.getData(), pkt.getDataLength(), flags);
		ENetPeer *peer = findPeer(connID);
		if (peer)
			enet_peer_send(peer, 0, packet);
		else
			enet_packet_destroy(packet);
	}
	// No per-call flush: ENet buffers the queued packets and the single batch
	// flush in ZCom_processOutput() drains them (one flush/tick instead of one
	// per routed event), at most one tick of added latency.
}

void ZCom_Control::ZCom_processInput(eZCom_BlockMode _block) {
	if (!m_host)
		return;
	g_currentControl = this;

	ENetEvent event;
	int timeout = (_block == eZCom_NoBlock) ? 0 : 10;

	while (enet_host_service(m_host, &event, timeout) > 0) {
		processENetEvent(event);
		timeout = 0; // no blocking after first event
	}
}

ZCom_ConnID ZCom_Control::ZCom_Connect(const ZCom_Address &addr, ZCom_BitStream *data) {
	if (!m_host)
		return ZCom_Invalid_ID;

	// Channel count must cover the highest channel index we send on.
	// CH_REPLICATOR == 2, so we need 3 channels (0=control/events,
	// 1=file transfer, 2=replicators). ENet sizes each peer's channels from
	// this connect request, so a too-small count would make channel-2 sends
	// silently fail and starve replication.
	ENetPeer *peer = enet_host_connect(m_host, &addr.getENetAddress(), 3, 0);
	if (!peer) {
		if (m_logFn)
			m_logFn("ENet: Failed to create connection");
		return ZCom_Invalid_ID;
	}

	uint32_t connID = allocateConnID();
	m_peerMap[connID] = peer;
	m_addressMap[connID] = addr;
	peer->data = reinterpret_cast<void *>(static_cast<uintptr_t>(connID));

	// C6: apply the global connection-timeout default to the new peer.
	enet_peer_timeout(peer, ENET_PEER_TIMEOUT_LIMIT, ENET_PEER_TIMEOUT_MINIMUM, ZoidCom::getConnectionTimeout());

	// Connection request data is stored but not sent directly —
	// the application-level handshake happens after ENet transport connects.
	(void)data;

	return connID;
}

void ZCom_Control::ZCom_disconnectAll(ZCom_BitStream *data) {
	if (!m_host)
		return;

	// Send disconnect reason data to peers before disconnecting
	if (data) {
		ZCom_BitStream pkt;
		pkt.addInt(MSG_DISCONNECT_DATA, 8);
		for (size_t i = 0; i < data->getDataLength(); ++i)
			pkt.addInt(data->getData()[i], 8);

		ENetPacket *packet = enet_packet_create(pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
		enet_host_broadcast(m_host, 0, packet);
		enet_host_flush(m_host);
	}

	for (auto &pair : m_peerMap) {
		if (pair.second) {
			enet_peer_disconnect(pair.second, 0);
		}
	}
	m_peerMap.clear();
	m_addressMap.clear();
}

void ZCom_Control::ZCom_Disconnect(ZCom_ConnID id, ZCom_BitStream *data) {
	ENetPeer *peer = findPeer(id);

	// Send disconnect reason data before disconnecting
	if (peer && data && data->getDataLength() > 0) {
		ZCom_BitStream pkt;
		pkt.addInt(MSG_DISCONNECT_DATA, 8);
		for (size_t i = 0; i < data->getDataLength(); ++i)
			pkt.addInt(data->getData()[i], 8);

		ENetPacket *packet = enet_packet_create(pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(peer, 0, packet);
		enet_host_flush(m_host);
	}

	if (peer) {
		enet_peer_disconnect(peer, 0);
		m_peerMap.erase(id);
		m_addressMap.erase(id);
	}
}

void ZCom_Control::Shutdown() {
	if (m_host) {
		for (auto &pair : m_peerMap) {
			if (pair.second) {
				enet_peer_disconnect_now(pair.second, 0);
			}
		}
		m_peerMap.clear();
		m_addressMap.clear();
		enet_host_destroy(m_host);
		m_host = nullptr;
	}
	// Null node back-pointers BEFORE clearing the registry: nodes are owned by
	// the game (BasePlayer::m_node, NetWorm::m_node, etc.) and may outlive the
	// control. Without this, a later ~ZCom_Node -> unregisterNode -> removeNode
	// would dereference this freed control (use-after-free crash on server exit:
	// disconnect completion does `delete m_control` while player/worm nodes
	// still hold m_control pointers; then game.unload -> deleteThis -> ~ZCom_Node
	// -> removeNode dereferences the freed control).
	m_nodeRegistry.forEach([](ZCom_Node *node) { node->setControl(nullptr); });
	m_nodeRegistry.clear();
	m_pendingAnnounce.clear();
	m_pendingReplicas.clear();
	m_pendingNodeEvents.clear();
	m_uniqueByClass.clear();
	m_pendingUniqueAnnounce.clear();
	m_pendingFileOffers.clear();
	m_fileTransfers.clear();
}

void ZCom_Control::disconnectPeer(uint32_t connID) {
	ENetPeer *peer = findPeer(connID);
	if (peer) {
		enet_peer_disconnect(peer, 0);
		m_peerMap.erase(connID);
		m_addressMap.erase(connID);
	}
}

ENetPeer *ZCom_Control::findPeer(uint32_t connID) const {
	auto it = m_peerMap.find(connID);
	return (it != m_peerMap.end()) ? it->second : nullptr;
}

void ZCom_Control::ZCom_sendData(ZCom_ConnID connID, ZCom_BitStream *stream, eZCom_SendMode mode) {
	ENetPeer *peer = findPeer(connID);
	if (!peer || !stream)
		return;

	enet_uint32 flags = enetPacketFlags(mode);

	ENetPacket *packet = enet_packet_create(stream->getData(), stream->getDataLength(), flags);
	enet_peer_send(peer, 0, packet);
}

void ZCom_Control::sendToAll(int mode, ZCom_BitStream *stream) {
	if (!m_host || !stream)
		return;

	enet_uint32 flags = enetPacketFlags(static_cast<eZCom_SendMode>(mode));

	ENetPacket *packet = enet_packet_create(stream->getData(), stream->getDataLength(), flags);
	enet_host_broadcast(m_host, 0, packet);
}

void ZCom_Control::replayPendingReplicators(ZCom_Node *node) {
	if (!node)
		return;
	uint32_t nid = node->getNetworkID();
	auto it = m_pendingReplicas.find(nid);
	if (it == m_pendingReplicas.end())
		return;

	NET_LOG("Replaying buffered replicator data for nodeID=" << nid);
	auto &pr = it->second;
	pr.data.resetReadState();
	// L2: pass through the stashed RTT estimate so replayed replicas get the
	// same interpolation timing as a live receive (was hardcoded 0).
	node->unpackAllReplicators(&pr.data, true, pr.estimatedTimeSent);
	m_pendingReplicas.erase(it);
}

bool ZCom_Control::registerExistingNode(ZCom_Node *node) {
	if (!node)
		return false;
	node->setControl(this);
	m_nodeRegistry.insert(node);
	// Replay any buffered replicator data for this node
	replayPendingReplicators(node);
	// Replay any buffered node events for this node
	replayPendingNodeEvents(node);
	// Deliver any buffered file-transfer offers for this node
	deliverPendingFileOffers(node);
	return true;
}

bool ZCom_Control::registerNode(ZCom_Node *node) {
	if (!node)
		return false;
	node->setNodeID(m_nextNodeID++);
	node->setControl(this);
	m_nodeRegistry.insert(node);

	NET_LOG("Registered nodeID=" << node->getNetworkID() << " classID=" << node->getClassID() << " ("
								 << ZCom_getClassName(node->getClassID()) << ")"
								 << " unique=" << node->isUnique() << " role=" << node->getRole()
								 << " owner=" << node->getOwner() << " totalNodes=" << m_nodeRegistry.size());

	// Replay any buffered replicator data for this node
	replayPendingReplicators(node);
	// Replay any buffered node events for this node
	replayPendingNodeEvents(node);
	// Deliver any buffered file-transfer offers for this node
	deliverPendingFileOffers(node);

	if (node->isUnique()) {
		// Track this class's local unique node so a MSG_NODE_ANNOUNCE_UNIQUE
		// arriving later (or already buffered) can link to it by class.
		m_uniqueByClass[node->getClassID()] = node;

		// If a unique-node announce arrived before this local proxy was
		// registered (the common game ordering: server announces at connect
		// time; the client registers its proxy later in ZCom_cbZoidResult),
		// adopt the server-announced nodeID now and replay buffered traffic.
		auto it = m_pendingUniqueAnnounce.find(node->getClassID());
		if (it != m_pendingUniqueAnnounce.end()) {
			PendingUniqueAnnounce pending = std::move(it->second);
			m_pendingUniqueAnnounce.erase(it);
			linkUniqueNode(node, node->getClassID(), pending.net_id, pending.role, pending.connID);
		}
		// Unique nodes are never announced via the dynamic pending-announce
		// path; the server announces them through the connect-spawn loop.
		return true;
	}

	// Defer announcement to ZCom_processOutput() so a subsequent setOwner()
	// (same tick, e.g. server.cpp PLAYER_REQUEST) is reflected as a single
	// owner-aware announce instead of a premature Proxy announce followed by
	// an Owner re-announce. Owner-less nodes (e.g. Particles) are still safe
	// because ZCom_Control::processOutput calls flushPendingAnnounces BEFORE
	// game logic deletes short-lived nodes (the deletion loop runs at the
	// start of the next tick, not this one).
	m_pendingAnnounce.insert(node->getNetworkID());
	return true;
}

void ZCom_Control::sendNodeAnnouncement(uint32_t connID, ZCom_Node *node, int role) {
	if (!m_host || !node)
		return;

	// Skip duplicate announcements to the same peer
	if (m_announcedNodes[connID].count(node->getNetworkID()))
		return;
	m_announcedNodes[connID].insert(node->getNetworkID());
	// Track the role this peer holds for this node (D1 routing).
	m_peerRole[connID][node->getNetworkID()] = static_cast<eZCom_NodeRole>(role);

	NET_LOG("sendNodeAnnouncement: connID=" << connID << " nodeID=" << node->getNetworkID() << " role=" << role);

	ZCom_BitStream pkt;
	pkt.addInt(MSG_NODE_ANNOUNCE, 8);
	pkt.addInt(node->getClassID(), 8);
	pkt.addInt(node->getNetworkID(), 16);
	pkt.addInt(role, 8);

	ZCom_BitStream *ad = node->buildAnnounceData(connID, static_cast<eZCom_NodeRole>(role));
	if (ad && ad->getDataLength() > 0) {
		pkt.addInt(static_cast<int>(ad->getDataLength()), 16);
		for (size_t i = 0; i < ad->getDataLength(); ++i)
			pkt.addInt(ad->getData()[i], 8);
	} else {
		pkt.addInt(0, 16);
	}

	ENetPacket *packet = enet_packet_create(pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);

	ENetPeer *peer = findPeer(connID);
	if (peer)
		enet_peer_send(peer, 0, packet);
	else
		enet_packet_destroy(packet);

	// Push eEvent_Init for nodes with event notification enabled
	if (node->getEventNotification()) {
		node->pushEvent(eZCom_EventInit, static_cast<eZCom_NodeRole>(role), connID, nullptr);
	}
}

void ZCom_Control::sendUniqueAnnouncement(uint32_t connID, ZCom_Node *node) {
	// Announce a unique node to a peer. Same body layout as MSG_NODE_ANNOUNCE
	// (net_control.cpp:507-520) but a distinct msgType so the existing dynamic
	// announce parser and all dynamic tests are untouched. Unique nodes have
	// no owner today, so the announced (local-to-receiver) role is Proxy.
	if (!m_host || !node)
		return;

	int role = eZCom_RoleProxy;

	ZCom_BitStream pkt;
	pkt.addInt(MSG_NODE_ANNOUNCE_UNIQUE, 8);
	pkt.addInt(node->getClassID(), 8);
	pkt.addInt(node->getNetworkID(), 16);
	pkt.addInt(role, 8);

	ZCom_BitStream *ad = node->buildAnnounceData(connID, static_cast<eZCom_NodeRole>(role));
	if (ad && ad->getDataLength() > 0) {
		pkt.addInt(static_cast<int>(ad->getDataLength()), 16);
		for (size_t i = 0; i < ad->getDataLength(); ++i)
			pkt.addInt(ad->getData()[i], 8);
	} else {
		pkt.addInt(0, 16);
	}

	ENetPacket *packet = enet_packet_create(pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
	ENetPeer *peer = findPeer(connID);
	if (peer)
		enet_peer_send(peer, 0, packet);
	else
		enet_packet_destroy(packet);
	// eEvent_Init is pushed by the caller (connect-spawn loop), not here.
}

void ZCom_Control::linkUniqueNode(ZCom_Node *node, uint32_t classID, uint32_t serverNodeID, int role,
								  uint32_t connID) {
	if (!node)
		return;

	// 1. Re-key the node's ID to the server-announced ID. The node was
	//    registered under a local m_nextNodeID; adopt the server's ID so
	//    MSG_REPLICATORS / MSG_NODE_EVENT addressed by serverNodeID resolve.
	//    Also migrate any traffic buffered under the old local ID (safety
	//    net; the local ID is never on the wire so this is usually a no-op).
	if (m_nodeRegistry.contains(node)) {
		uint32_t oldID = node->getNetworkID();
		if (oldID != serverNodeID) {
			m_nodeRegistry.rekey(node, serverNodeID);
			auto repIt = m_pendingReplicas.find(oldID);
			if (repIt != m_pendingReplicas.end()) {
				auto pr = std::move(repIt->second);
				m_pendingReplicas.erase(repIt);
				m_pendingReplicas[serverNodeID] = std::move(pr);
			}
			auto evIt = m_pendingNodeEvents.find(oldID);
			if (evIt != m_pendingNodeEvents.end()) {
				auto vec = std::move(evIt->second);
				m_pendingNodeEvents.erase(evIt);
				m_pendingNodeEvents[serverNodeID] = std::move(vec);
			}
		}
	} else {
		node->setNodeID(serverNodeID);
	}
	// Keep the class->node map pointing at the (now re-keyed) node.
	m_uniqueByClass[classID] = node;

	// 2. Adopt the announced role (Proxy for unique nodes).
	node->setRole(static_cast<eZCom_NodeRole>(role));

	// 3. The sender of the announce is the authority for this node (matches
	//    the dynamic handler at net_control.cpp:1107).
	m_peerRole[connID][serverNodeID] = eZCom_RoleAuthority;

	// 4. Dedupe so a re-announce is a no-op.
	m_announcedNodes[connID].insert(serverNodeID);

	NET_LOG("linkUniqueNode: classID=" << classID << " (" << ZCom_getClassName(classID) << ")"
									   << " serverNodeID=" << serverNodeID << " role=" << role
									   << " connID=" << connID);

	// 5. Replay any traffic buffered under serverNodeID (replicators / events
	//    that arrived while the proxy was not yet linked).
	replayPendingReplicators(node);
	replayPendingNodeEvents(node);
	deliverPendingFileOffers(node);
}

void ZCom_Control::clearAnnouncedNode(uint32_t nodeID) {
	// Clear per-peer tracking for this node so re-announcements (e.g. from setOwner)
	// will go through with the updated role.
	for (auto &pair : m_peerMap) {
		m_announcedNodes[pair.first].erase(nodeID);
		m_peerRole[pair.first].erase(nodeID);
	}
}

void ZCom_Control::flushPendingAnnounces() {
	if (m_pendingAnnounce.empty())
		return;
	// Snapshot then clear so any node re-queued during the pass (e.g. via a
	// setOwner path) is processed on the next processOutput, not this one.
	auto pending = std::move(m_pendingAnnounce);
	m_pendingAnnounce.clear();
	for (uint32_t nid : pending) {
		ZCom_Node *node = ZCom_getNode(nid);
		if (!node)
			continue; // node deleted between queue and flush
		// announceNodeWithOwner de-dupes per-peer via m_announcedNodes, so a
		// node already announced by setOwner() this tick is a harmless no-op.
		announceNodeWithOwner(node);
	}
}

void ZCom_Control::announceNodeWithOwner(ZCom_Node *node) {
	if (!m_host || !node)
		return;

	NET_LOG("announceNodeWithOwner: nodeID=" << node->getNetworkID() << " ownerID=" << node->getOwner()
											 << " peers=" << m_peerMap.size());

	uint32_t ownerID = node->getOwner();

	// Publish to all currently connected peers (owner and proxies).
	// For each peer, if this is a re-announce (e.g. setOwner() after a
	// previous announce), clear any stale Owner role first so the new
	// owner's assignment is tracked correctly. The clear and the new
	// send must happen atomically per peer — if we cleared in a separate
	// loop and then hit the duplicate-skip below, we'd wipe the role
	// without restoring it.
	for (auto &pair : m_peerMap) {
		uint32_t connID = pair.first;

		// Skip duplicate announcements to the same peer
		if (m_announcedNodes[connID].count(node->getNetworkID())) {
			NET_LOG("  peer connID=" << connID << " (skip duplicate)");
			continue;
		}

		// Clear stale Owner role for this peer so the new role assignment
		// below takes effect.
		auto nit = m_peerRole[connID].find(node->getNetworkID());
		if (nit != m_peerRole[connID].end() && nit->second == eZCom_RoleOwner) {
			NET_LOG("Cleared old owner role=" << eZCom_RoleOwner << " for nodeID=" << node->getNetworkID()
											  << " on connID=" << connID);
			m_peerRole[connID].erase(nit);
		}

		NET_LOG("  peer connID=" << connID);
		int role;
		if (ownerID != 0 && connID == ownerID) {
			role = eZCom_RoleOwner;
		} else {
			role = eZCom_RoleProxy;
		}
		sendNodeAnnouncement(connID, node, role);
	}
	// eEvent_Init is pushed per-peer inside sendNodeAnnouncement (with the
	// correct owner-aware role), so no additional push is needed here.
}

void ZCom_Control::removeNode(ZCom_Node *node) {
	if (!node)
		return;
	// Remove immediately from the registry. NodeRegistry uses a std::list so
	// erasing one entry invalidates only that entry's iterator; the forEach
	// helper pre-advances before invoking its callback, so destroying the
	// current node during iteration is safe. Clear per-peer bookkeeping now
	// so the node stops being routed to peers.
	uint32_t nid = node->getNetworkID();
	m_nodeRegistry.remove(node);
	for (auto &pair : m_peerMap) {
		m_announcedNodes[pair.first].erase(nid);
		m_peerRole[pair.first].erase(nid);
	}
	// Unique-node bookkeeping: drop the class->node map entry (only if it
	// still points at this node) and any buffered announce for this class.
	if (node->isUnique()) {
		uint32_t classID = node->getClassID();
		auto it = m_uniqueByClass.find(classID);
		if (it != m_uniqueByClass.end() && it->second == node)
			m_uniqueByClass.erase(it);
		m_pendingUniqueAnnounce.erase(classID);
	}
}

const ZCom_Address *ZCom_Control::ZCom_getPeer(ZCom_ConnID id) const {
	auto it = m_addressMap.find(id);
	return (it != m_addressMap.end()) ? &it->second : nullptr;
}

const ZCom_ConnStats &ZCom_Control::ZCom_getConnectionStats(ZCom_ConnID id) const {
	ZCom_ConnStats &stats = m_statsCache[id];
	stats = ZCom_ConnStats();
	ENetPeer *peer = findPeer(id);
	if (peer) {
		stats.avg_ping = peer->roundTripTime;
		stats.min_ping = peer->lowestRoundTripTime;
		stats.max_ping = peer->roundTripTime != 0 ? peer->roundTripTime + peer->roundTripTimeVariance : 0;
		stats.last_sec_out = peer->lastSendTime;
		stats.last_sec_in = peer->lastReceiveTime;
		stats.total_out = peer->outgoingDataTotal;
		stats.total_in = peer->incomingDataTotal;
		stats.last_sec_loss_percent = peer->packetLoss;
		stats.current_loss_count = peer->packetsLost;
	}
	return stats;
}

uint32_t ZCom_Control::allocateConnID() {
	return m_nextConnID++;
}

// --- B1b: reference API members (game-unused; stubs/delegations) ---
bool ZCom_Control::ZCom_initSockets(bool _useudp, zU16 _udpport, zU16 _localport, zU8 _control_id_size) {
	(void)_useudp;	  // omfgnet is always UDP/ENet
	(void)_localport; // local bind port not implemented (single socket)
	m_controlID = _control_id_size;
	(void)enet_initialize(); // best-effort; may already be initialised by free ZCom_initSockets
	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = _udpport;
	ENetHost *host = enet_host_create(&address, 32, 3, 0, 0);
	if (!host)
		return false;
	// Use ENet's built-in range-coder compression for all traffic (it negotiates
	// per-connection; both ends must enable it). This is the ENet-idiomatic way
	// to shrink the bit-packed replicator/event streams instead of a custom limiter.
	enet_host_compress_with_range_coder(host);
	setHost(host, true);
	return true;
}

void ZCom_Control::ZCom_setControlID(zU8 _id) {
	m_controlID = _id;
}
void ZCom_Control::ZCom_setDebugName(const char *_name) {
	m_debugName = _name ? _name : "";
}
void ZCom_Control::ZCom_setUpstreamLimit(zU32 _total_bps, zU32 _perconn_bps) {
	(void)_perconn_bps;
	// ENet is the single source of truth for upstream bandwidth control:
	// enet_host_bandwidth_limit() queues (throttles) without dropping reliable
	// packets. The former app-level limiter (tryAccountUpstream) was removed
	// because it destroyed packets and caused networked rubber-banding.
	if (m_host)
		enet_host_bandwidth_limit(m_host, 0, _total_bps);
}

void ZCom_Control::ZCom_setConnectionTimeout(zU32 _ms) {
	// C6: store the global default and re-apply it to every live peer.
	ZoidCom::setConnectionTimeout(_ms);
	for (auto &pair : m_peerMap) {
		if (pair.second)
			enet_peer_timeout(pair.second, ENET_PEER_TIMEOUT_LIMIT, ENET_PEER_TIMEOUT_MINIMUM, _ms);
	}
}
void ZCom_Control::ZCom_requestDownstreamLimit(ZCom_ConnID _id, zU16 _pps, zU16 _bpp) {
	ENetPeer *peer = findPeer(_id);
	if (!peer)
		return;
	ZCom_BitStream pkt;
	pkt.addInt(MSG_DOWNSTREAM_REQUEST, 8);
	pkt.addInt(static_cast<int>(_pps), 16);
	pkt.addInt(static_cast<int>(_bpp), 16);
	ENetPacket *p = enet_packet_create(pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(peer, 0, p); // reliable control message
}
void ZCom_Control::ZCom_simulateLag(ZCom_ConnID _id, zU32 _lagmsec) {
	m_peerState[_id].lagMsec = _lagmsec;
}
void ZCom_Control::ZCom_simulateLoss(ZCom_ConnID _id, zFloat _amount) {
	m_peerState[_id].lossPct = _amount;
}

bool ZCom_Control::ZCom_requestZoidMode(ZCom_ConnID _id, zU8 _level) {
	ENetPeer *peer = findPeer(_id);
	if (!peer)
		return false;
	ZCom_BitStream pkt;
	pkt.addInt(MSG_ZOID_REQUEST, 8);
	pkt.addInt(_level, 8);
	ENetPacket *packet = enet_packet_create(pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(peer, 0, packet) == 0;
}

bool ZCom_Control::ZCom_Discover(const ZCom_Address &, ZCom_BitStream *) {
	NET_LOG("ZCom_Discover not implemented");
	return false;
}

void ZCom_Control::ZCom_setDiscoverListener(eZCom_DiscoverOpt, zU16) {}

void ZCom_Control::ZCom_setUserData(ZCom_ConnID _id, void *_data) {
	m_userData[_id] = _data;
}

void *ZCom_Control::ZCom_getUserData(ZCom_ConnID _id) const {
	auto it = m_userData.find(_id);
	return it != m_userData.end() ? it->second : nullptr;
}

zU32 ZCom_Control::ZCom_getCurrentTime() {
	return ZoidCom::getTime();
}

void ZCom_Control::ZCom_sendDataToGroup(ZCom_GroupID _gid, ZCom_BitStream *_stream, eZCom_SendMode _mode) {
	if (!m_host || !_stream)
		return;
	// C9: the "all" group maps directly to enet_host_broadcast. Subset groups
	// have no group-manager membership tracking yet, so they are unsupported.
	if (_gid != ZCOM_CONNGROUP_ALL) {
		NET_LOG("ZCom_sendDataToGroup: non-ALL group " << (unsigned)_gid << " not implemented");
		return;
	}
	ENetPacket *packet = enet_packet_create(_stream->getData(), _stream->getDataLength(), enetPacketFlags(_mode));
	enet_host_broadcast(m_host, 0, packet);
}

void ZCom_Control::ZCom_sendDataRaw(ZCom_Address &, void *, zU32) {
	NET_LOG("ZCom_sendDataRaw not implemented");
}

void ZCom_Control::dropPeerState(uint32_t connID) {
	m_peerState.erase(connID);
	for (size_t i = 0; i < m_lagQueue.size();) {
		if (m_lagQueue[i].connID == connID) {
			enet_packet_destroy(m_lagQueue[i].packet);
			m_lagQueue.erase(m_lagQueue.begin() + i);
		} else {
			++i;
		}
	}

	// The per-nodeID buffers below are shared across ALL peers (keyed by
	// nodeID / classID), so a blanket clear() would wipe other clients'
	// buffered data on a multi-client server. Drop only the entries that came
	// from the disconnecting peer; otherwise data for a node that never
	// registers locally accumulates unbounded (latent leak). Consolidated
	// here so both disconnect paths — the clean DISCONNECT case and the T3.1
	// over-read close at the bottom of processENetEvent — stay in sync.
	for (auto it = m_pendingReplicas.begin(); it != m_pendingReplicas.end();) {
		if (it->second.connID == connID)
			it = m_pendingReplicas.erase(it);
		else
			++it;
	}
	for (auto it = m_pendingNodeEvents.begin(); it != m_pendingNodeEvents.end();) {
		auto &vec = it->second;
		vec.erase(std::remove_if(vec.begin(), vec.end(),
								 [&](const PendingNodeEvent &ev) { return ev.connID == connID; }),
				  vec.end());
		if (vec.empty())
			it = m_pendingNodeEvents.erase(it);
		else
			++it;
	}
	for (auto it = m_pendingUniqueAnnounce.begin(); it != m_pendingUniqueAnnounce.end();) {
		if (it->second.connID == connID)
			it = m_pendingUniqueAnnounce.erase(it);
		else
			++it;
	}
	// Pending file offers are keyed by nodeID -> vector<fileTransID>; the
	// owning FileTransfer records the peer. Drop offers whose transfer came
	// from the disconnecting peer. Offers whose transfer is already gone are
	// harmless: deliverPendingFileOffers skips fids absent from m_fileTransfers.
	for (auto it = m_pendingFileOffers.begin(); it != m_pendingFileOffers.end();) {
		auto &fids = it->second;
		fids.erase(std::remove_if(fids.begin(), fids.end(),
							  [&](ZCom_FileTransID fid) {
								  auto ft = m_fileTransfers.find(fid);
								  return ft != m_fileTransfers.end() && ft->second.peerConnID == connID;
							  }),
				   fids.end());
		if (fids.empty())
			it = m_pendingFileOffers.erase(it);
		else
			++it;
	}
}

void ZCom_Control::sendPacket(uint32_t connID, ENetPeer *peer, int channel, ENetPacket *packet, bool critical) {
	if (!packet)
		return;
	if (!peer) {
		enet_packet_destroy(packet);
		return;
	}

	PeerNetState &ps = m_peerState[connID];

	// T1.2 — loss injection (debug/emulation): drop a fraction of non-critical
	// packets. Critical control messages always go so connections stay alive.
	if (!critical && ps.lossPct > 0.0f) {
		static bool seeded = false;
		if (!seeded) {
			std::srand(static_cast<unsigned>(std::time(nullptr)));
			seeded = true;
		}
		if (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) < ps.lossPct) {
			enet_packet_destroy(packet);
			return;
		}
	}

	// T1.2 — lag injection: defer the actual ENet send until later.
	if (!critical && ps.lagMsec > 0) {
		m_lagQueue.push_back({connID, peer, channel, packet, ZCom_getCurrentTime() + ps.lagMsec});
		return;
	}

	enet_peer_send(peer, channel, packet);
}

void ZCom_Control::drainLagQueue() {
	if (m_lagQueue.empty())
		return;
	uint32_t now = ZCom_getCurrentTime();
	std::vector<PendingLagSend> remaining;
	remaining.reserve(m_lagQueue.size());
	for (auto &e : m_lagQueue) {
		if (now < e.sendAt) {
			remaining.push_back(std::move(e));
			continue;
		}
		ENetPeer *peer = findPeer(e.connID);
		if (!peer) {
			enet_packet_destroy(e.packet);
			continue;
		}
		enet_peer_send(peer, e.channel, e.packet);
	}
	m_lagQueue = std::move(remaining);
}

void ZCom_Control::processENetEvent(ENetEvent &event) {
	uint32_t connID;
	ZCom_BitStream streamData;

	switch (event.type) {
		case ENET_EVENT_TYPE_CONNECT: {
			if (event.peer->data != nullptr) {
				// Outgoing connection (client-side): ENet transport connected.
				// Do NOT fire cbConnectResult yet — wait for the server to
				// send MSG_CONNECTION_REPLY with the reply data.
				connID = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(event.peer->data));
				m_waitingForReply.insert(connID);
				break;
			}

			// Incoming connection (server-side)
			connID = allocateConnID();
			event.peer->data = reinterpret_cast<void *>(static_cast<uintptr_t>(connID));
			m_peerMap[connID] = event.peer;
			m_addressMap[connID] = ZCom_Address(event.peer->address);

			// C6: apply the global connection-timeout default to the new peer.
			enet_peer_timeout(event.peer, ENET_PEER_TIMEOUT_LIMIT, ENET_PEER_TIMEOUT_MINIMUM,
							  ZoidCom::getConnectionTimeout());

			// Call the connection request callback to get reply data
			ZCom_BitStream request;
			ZCom_BitStream reply;

			if (ZCom_cbConnectionRequest(connID, request, reply)) {
				// Send reply data back to client as MSG_CONNECTION_REPLY
				sendConnectionReply(event.peer, reply, true);
			// Sync existing nodes to the new client. Dynamic nodes are
			// announced via MSG_NODE_ANNOUNCE; unique nodes (Game/Updater)
			// are announced via MSG_NODE_ANNOUNCE_UNIQUE so the client links
			// its already-registered counterpart by class.
			m_nodeRegistry.forEach([&](ZCom_Node *node) {
				if (node->isUnique()) {
					// Unique authority node: announce to the new peer. The
					// client links by class to its already-registered (or
					// soon-to-be-registered) proxy, adopting this nodeID.
					if (m_announcedNodes[connID].count(node->getNetworkID()))
						return;
					m_announcedNodes[connID].insert(node->getNetworkID());
					int role = eZCom_RoleProxy; // unique nodes have no owner today
					m_peerRole[connID][node->getNetworkID()] = static_cast<eZCom_NodeRole>(role);
					sendUniqueAnnouncement(connID, node);
					// Push eEvent_Init so the authority's per-new-client logic
					// fires (e.g. Game::gameNetworkInit). Previously dead for
					// unique nodes.
					if (node->getEventNotification())
						node->pushEvent(eZCom_EventInit, static_cast<eZCom_NodeRole>(role), connID, nullptr);
					// Force a full replicator pack so the new peer receives the
					// current state (worm_gravity/teamPlay) on the next output.
					node->forceReplicationUpdate();
					return;
				}
				// Track per-peer so re-announcements are skipped
				if (m_announcedNodes[connID].count(node->getNetworkID()))
					return;
				m_announcedNodes[connID].insert(node->getNetworkID());

					// Determine role for this client: Owner if node is owned by them, otherwise Proxy
					int role = eZCom_RoleProxy;
					uint32_t ownerID = node->getOwner();
					if (ownerID != 0 && connID == ownerID) {
						role = eZCom_RoleOwner;
					}
					m_peerRole[connID][node->getNetworkID()] = static_cast<eZCom_NodeRole>(role);

					ZCom_BitStream pkt;
					pkt.addInt(MSG_NODE_ANNOUNCE, 8);
					pkt.addInt(node->getClassID(), 8);
					pkt.addInt(node->getNetworkID(), 16);
					pkt.addInt(role, 8);

					ZCom_BitStream *ad = node->buildAnnounceData(connID, static_cast<eZCom_NodeRole>(role));
					if (ad && ad->getDataLength() > 0) {
						pkt.addInt(static_cast<int>(ad->getDataLength()), 16);
						for (size_t i = 0; i < ad->getDataLength(); ++i)
							pkt.addInt(ad->getData()[i], 8);
					} else {
						pkt.addInt(0, 16);
					}

					ENetPacket *packet =
						enet_packet_create(pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
					enet_peer_send(event.peer, 0, packet);

					// Push eEvent_Init for nodes with event notification enabled
					if (node->getEventNotification()) {
						node->pushEvent(eZCom_EventInit, static_cast<eZCom_NodeRole>(role), connID, nullptr);
					}

					// Force a full replicator pack on the next processOutput so
					// this new peer receives the current node state. Without this,
					// authority nodes created before any client connected have
					// already had their initial=true entries consumed by empty
					// processOutput passes, so the new peer would only see future
					// dirty updates — missing the worm's position, health, etc.
					node->forceReplicationUpdate();
				});
				// Fire connection spawned after reply is sent
				ZCom_cbConnectionSpawned(connID);
			} else {
				// Send rejection reply, flush, then disconnect
				sendConnectionReply(event.peer, reply, false);
				enet_host_flush(m_host);
				enet_peer_disconnect(event.peer, 0);
				m_peerMap.erase(connID);
				m_addressMap.erase(connID);
			}
			break;
		}

		case ENET_EVENT_TYPE_RECEIVE: {
			connID = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(event.peer->data));

			streamData.assign(static_cast<const uint8_t *>(event.packet->data), event.packet->dataLength);

			// Peek at the first byte to identify internal protocol messages
			int msgType = 0;
			if (event.packet->dataLength > 0) {
				msgType = static_cast<const uint8_t *>(event.packet->data)[0];
			}

			// Handle connection reply (sent by server after ENet connect)
			if (msgType == MSG_CONNECTION_REPLY && m_waitingForReply.count(connID)) {
				streamData.getInt(8); // consume msgType
				eZCom_ConnectResult result = static_cast<eZCom_ConnectResult>(streamData.getInt(8));
				int serverVersion = streamData.getInt(8);
				int byteLen = streamData.getInt(16);

				// Protocol-version gate: refuse a version-skewed server cleanly
				// instead of silently desyncing on the reply payload that follows.
				if (serverVersion != PROTOCOL_VERSION) {
					NET_LOG("Connection refused: protocol version mismatch" << " (server=" << serverVersion
																			<< " local=" << PROTOCOL_VERSION << ")");
					m_waitingForReply.erase(connID);
					ZCom_BitStream emptyReply;
					ZCom_cbConnectResult(connID, eZCom_ConnWrongVersion, emptyReply);
					enet_peer_disconnect(event.peer, 0);
					enet_packet_destroy(event.packet);
					break;
				}

				std::vector<uint8_t> buf(byteLen);
				for (int i = 0; i < byteLen; ++i)
					buf[i] = streamData.getInt(8);

				ZCom_BitStream reply;
				reply.assign(buf.data(), byteLen);
				m_waitingForReply.erase(connID);

				ZCom_cbConnectResult(connID, result, reply);
				enet_packet_destroy(event.packet);
				break;
			}

			// Handle zoid mode request (client -> server)
			if (msgType == MSG_ZOID_REQUEST) {
				streamData.getInt(8); // consume msgType
				int requestedLevel = streamData.getInt(8);

				ZCom_BitStream reason;
				bool accepted = ZCom_cbZoidRequest(connID, static_cast<uint8_t>(requestedLevel), reason);

				// Send result back to client
				ZCom_BitStream result;
				result.addInt(MSG_ZOID_RESULT, 8);
				result.addInt(accepted ? static_cast<int>(eZCom_ZoidEnabled) : static_cast<int>(eZCom_ZoidDisabled), 8);
				result.addInt(requestedLevel, 8);

				ENetPacket *pkt =
					enet_packet_create(result.getData(), result.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
				enet_peer_send(event.peer, 0, pkt);

				enet_packet_destroy(event.packet);
				break;
			}

			// Handle zoid mode result (server -> client)
			if (msgType == MSG_ZOID_RESULT) {
				streamData.getInt(8); // consume msgType
				int resultCode = streamData.getInt(8);
				int newLevel = streamData.getInt(8);

				ZCom_BitStream reason;
				ZCom_cbZoidResult(connID, static_cast<eZCom_ZoidResult>(resultCode), static_cast<uint8_t>(newLevel),
								  reason);

				enet_packet_destroy(event.packet);
				break;
			}

			// Handle node events (msgType == 0 means node event)
			if (msgType == MSG_NODE_EVENT) {
				streamData.getInt(8); // consume msgType
				int nodeID = streamData.getInt(16);
				// addBitStream inlines the payload bits directly (no length
				// prefix), so the remaining bits are the event payload.
				// Pass ENet's RTT-based send-time estimate so event consumers
				// (interceptors) can do time-correct interpolation.
				zU32 est = estimatedSendTimeFromPeer(event.peer);
				// M1: report the remote peer's actual role for this node (recorded
				// when the node was announced to us) instead of a hardcoded Proxy.
				// Falls back to Proxy if unknown (e.g. event arrived before the
				// announce). Currently latent — no game consumer reads remote_role.
				eZCom_NodeRole remoteRole = getPeerRole(connID, nodeID);
				if (remoteRole == eZCom_RoleUndefined)
					remoteRole = eZCom_RoleProxy;
				dispatchNodeEvent(nodeID, eZCom_EventUser, remoteRole, connID, &streamData, est);

				enet_packet_destroy(event.packet);
				break;
			}

			// Handle replicator state updates
			if (msgType == MSG_REPLICATORS) {
				streamData.getInt(8); // consume msgType
				uint32_t repNodeID = streamData.getInt(16);

				// Find the matching local node by networkID
				ZCom_Node *repNode = ZCom_getNode(repNodeID);

				// Feed ENet's RTT-based send-time estimate into the replicator
				// unpack so interceptors / interpolation get a real travel time
				// instead of the hardcoded 0 (enables snapshot interpolation
				// and dead-reckoning, previously impossible). Also stashed into the
				// buffered entry below for replay (L2).
				zU32 est = estimatedSendTimeFromPeer(event.peer);

				if (repNode) {
					NET_LOG("Received MSG_REPLICATORS for nodeID="
							<< repNodeID << " classID=" << repNode->getClassID() << " ("
							<< ZCom_getClassName(repNode->getClassID()) << ")"
							<< " role=" << repNode->getRole() << " dataLen=" << (event.packet->dataLength - 3));

					// Replicator data starts at byte offset 3 (msgType=1 + nodeID=2)
					size_t offset = 3;
					ZCom_BitStream repData(event.packet->data + offset, event.packet->dataLength - offset);
					repNode->unpackAllReplicators(&repData, true, est);
				} else {
					NET_LOG("MSG_REPLICATORS: nodeID=" << repNodeID << " NOT FOUND locally — buffering for later ("
													   << m_nodeRegistry.size() << " nodes)");

					// Buffer the replica data so it can be replayed when the node is
					// registered. Record the sending peer (M2: so its disconnect can
					// drop only this entry, not every peer's) and the RTT estimate
					// (L2: so replay feeds interpolation a real travel time).
					size_t offset = 3;
					auto &pr = m_pendingReplicas[repNodeID];
					pr.connID = connID;
					pr.estimatedTimeSent = est;
					pr.data.assign(event.packet->data + offset, event.packet->dataLength - offset);
				}

				enet_packet_destroy(event.packet);
				break;
			}

			// Handle downstream-limit request (T1.1): the requester (connID) asks
			// us to cap our upstream traffic to it at _pps bytes/sec.
			if (msgType == MSG_DOWNSTREAM_REQUEST) {
				streamData.getInt(8); // consume msgType
				zU16 pps = static_cast<zU16>(streamData.getInt(16));
				/* zU16 bpp = */ static_cast<zU16>(streamData.getInt(16));
				m_peerState[connID].upstreamLimit = pps;
				enet_packet_destroy(event.packet);
				break;
			}

		// Handle unique-node announcement — link the local unique counterpart
		// by class (Zoidcom unique-replication contract: the client counterpart
		// is expected to be registered already, or buffered until it is).
		if (msgType == MSG_NODE_ANNOUNCE_UNIQUE) {
			streamData.getInt(8); // consume msgType
			uint32_t classID = streamData.getInt(8);
			uint32_t net_id = streamData.getInt(16);
			int role = streamData.getInt(8);
			int announceLen = streamData.getInt(16);
			std::vector<uint8_t> announceData;
			if (announceLen > 0) {
				announceData.resize(announceLen);
				for (int i = 0; i < announceLen; ++i)
					announceData[i] = static_cast<uint8_t>(streamData.getInt(8));
			}

			NET_LOG("Received MSG_NODE_ANNOUNCE_UNIQUE classID="
					<< classID << " (" << ZCom_getClassName(classID) << ")"
					<< " net_id=" << net_id << " role=" << role << " announceLen=" << announceLen);

			auto it = m_uniqueByClass.find(classID);
			if (it != m_uniqueByClass.end() && it->second != nullptr) {
				// Local proxy already registered (rare reverse order, e.g.
				// synthetic tests): re-key it to the server nodeID now.
				linkUniqueNode(it->second, classID, net_id, role, connID);
			} else {
				// Common case: announce arrived before the proxy registered
				// (server announces at connect; client registers its proxy
				// later in ZCom_cbZoidResult). Buffer by class until then.
				auto &p = m_pendingUniqueAnnounce[classID];
				p.net_id = net_id;
				p.role = role;
				p.connID = connID;
				p.announceData = std::move(announceData);
			}
			// Unique nodes are app-silent: no ZCom_cbNodeRequest_Dynamic.
			enet_packet_destroy(event.packet);
			break;
		}

		// Handle node announcement — dispatch to ZCom_cbNodeRequest_Dynamic
		if (msgType == MSG_NODE_ANNOUNCE) {
			streamData.getInt(8); // consume msgType
			uint32_t classID = streamData.getInt(8);
			uint32_t net_id = streamData.getInt(16);
			int role = streamData.getInt(8);
			int announceLen = streamData.getInt(16);

			// If node already exists locally, update its role (handles re-announcements from setOwner)
			ZCom_Node *existingNode = ZCom_getNode(net_id);
			if (existingNode) {
				existingNode->setRole(static_cast<eZCom_NodeRole>(role));
				// The sender of an announcement is the authority for this node.
				m_peerRole[connID][net_id] = eZCom_RoleAuthority;
				NET_LOG("Updating role for existing nodeID=" << net_id << " to " << role);
				enet_packet_destroy(event.packet);
				break;
			}

			// Skip duplicate announcements to this peer
			if (m_announcedNodes[connID].count(net_id)) {
				NET_LOG("Skipping duplicate MSG_NODE_ANNOUNCE for connID=" << connID << " net_id=" << net_id);
				enet_packet_destroy(event.packet);
				break;
			}
			m_announcedNodes[connID].insert(net_id);
			// The sender of an announcement is the authority for this node;
			// the announced role is the local peer's role (Owner/Proxy).
			m_peerRole[connID][net_id] = eZCom_RoleAuthority;

			NET_LOG("Received MSG_NODE_ANNOUNCE classID=" << classID << " (" << ZCom_getClassName(classID) << ")"
														  << " net_id=" << net_id << " role=" << role
														  << " announceLen=" << announceLen);

			std::unique_ptr<ZCom_BitStream> announceData;
			if (announceLen > 0) {
				std::vector<uint8_t> buf(announceLen);
				for (int i = 0; i < announceLen; ++i)
					buf[i] = streamData.getInt(8);
				announceData = std::make_unique<ZCom_BitStream>(buf.data(), buf.size());
			}

			// Set the node-request context so that registerNodeDynamic /
			// registerRequestedNode calls made inside the callback auto-assign
			// the announced role to the new local node.
			m_requestCtx.active = true;
			m_requestCtx.connID = connID;
			m_requestCtx.role = role;
			m_requestCtx.net_id = net_id;

			ZCom_cbNodeRequest_Dynamic(connID, classID, announceData.get(), role, net_id);

			m_requestCtx.active = false;

			// announceData (unique_ptr) frees at scope end
			enet_packet_destroy(event.packet);
			break;
		}

			// Handle disconnect reason data (sent before disconnect)
			if (msgType == MSG_DISCONNECT_DATA) {
				streamData.getInt(8); // consume msgType
				m_pendingDisconnectData[connID] = streamData;
				enet_packet_destroy(event.packet);
				break;
			}

			// --- File transfer protocol (Phase E) ---

			// Sender -> receiver: a transfer is starting. Carries the sender's
			// nodeID (so the receiver knows which node's queue to deliver to), the
			// transfer id, file size, the advertised path, and the optional _data.
			if (msgType == MSG_FILE_OFFER) {
				streamData.getInt(8); // consume msgType
				uint32_t senderNodeID = streamData.getInt(16);
				ZCom_FileTransID fid = streamData.getInt(ZCOM_FTRANS_ID_BITS);
				zU32 fsize = streamData.getInt(ZCOM_FTRANS_SIZE_BITS);
				char pathbuf[1024];
				streamData.getString(pathbuf, sizeof(pathbuf));
				zU16 offerLen = streamData.getBufferMax();
				std::vector<uint8_t> offerBuf(offerLen);
				if (offerLen)
					streamData.getBuffer(reinterpret_cast<char *>(offerBuf.data()), offerLen);
				else
					streamData.getBuffer(nullptr, 0);

				auto &ft = m_fileTransfers[fid];
				ft.id = fid;
				ft.nodeID = senderNodeID;
				ft.isReceiver = true;
				ft.peerConnID = connID;
				ft.path = pathbuf;
				ft.pathtosend = pathbuf;
				ft.size = fsize;
				ft.offerData = offerBuf;

				ZCom_Node *tgt = ZCom_getNode(senderNodeID);
				if (tgt)
					pushFileEvent(tgt, eZCom_EventFile_Incoming, connID, fid, &ft.offerData);
				else
					m_pendingFileOffers[senderNodeID].push_back(fid);

				NET_LOG("MSG_FILE_OFFER fid=" << fid << " size=" << fsize << " nodeID=" << senderNodeID
											  << " path=" << pathbuf << " localNode=" << (tgt ? "found" : "buffered"));
				enet_packet_destroy(event.packet);
				break;
			}

			// Receiver -> sender: accept or deny the offer.
			if (msgType == MSG_FILE_ACCEPT) {
				streamData.getInt(8); // consume msgType
				ZCom_FileTransID fid = streamData.getInt(ZCOM_FTRANS_ID_BITS);
				bool accept = streamData.getInt(1) != 0;
				auto it = m_fileTransfers.find(fid);
				if (it != m_fileTransfers.end() && !it->second.isReceiver) {
					if (accept)
						it->second.accepted = true;
					else {
						it->second.aborted = true;
						if (it->second.inFile.is_open())
							it->second.inFile.close();
					}
				}
				enet_packet_destroy(event.packet);
				break;
			}

			// Sender -> receiver: a file data chunk.
			if (msgType == MSG_FILE_DATA) {
				streamData.getInt(8); // consume msgType
				ZCom_FileTransID fid = streamData.getInt(ZCOM_FTRANS_ID_BITS);
				zU16 chunkLen = streamData.getBufferMax();
				std::vector<char> chunk(chunkLen);
				if (chunkLen)
					streamData.getBuffer(chunk.data(), chunkLen);
				else
					streamData.getBuffer(nullptr, 0);

				auto it = m_fileTransfers.find(fid);
				if (it != m_fileTransfers.end() && it->second.isReceiver && it->second.accepted &&
					!it->second.aborted && !it->second.done) {
					auto &ft = it->second;
					if (ft.outFile.is_open()) {
						ft.outFile.write(chunk.data(), chunkLen);
						ft.transferred += chunkLen;
						ft.bytesThisSec += chunkLen;
					}
					pushFileEvent(ZCom_getNode(ft.nodeID), eZCom_EventFile_Data, connID, fid, nullptr);
				}
				enet_packet_destroy(event.packet);
				break;
			}

			// Either side: abort the transfer.
			if (msgType == MSG_FILE_ABORT) {
				streamData.getInt(8); // consume msgType
				ZCom_FileTransID fid = streamData.getInt(ZCOM_FTRANS_ID_BITS);
				auto it = m_fileTransfers.find(fid);
				if (it != m_fileTransfers.end()) {
					auto &ft = it->second;
					ft.aborted = true;
					if (ft.isReceiver) {
						if (ft.outFile.is_open())
							ft.outFile.close();
						pushFileEvent(ZCom_getNode(ft.nodeID), eZCom_EventFile_Aborted, connID, fid, nullptr);
					} else if (ft.inFile.is_open()) {
						ft.inFile.close();
					}
				}
				enet_packet_destroy(event.packet);
				break;
			}

			// Sender -> receiver: all chunks sent, transfer complete.
			if (msgType == MSG_FILE_COMPLETE) {
				streamData.getInt(8); // consume msgType
				ZCom_FileTransID fid = streamData.getInt(ZCOM_FTRANS_ID_BITS);
				auto it = m_fileTransfers.find(fid);
				if (it != m_fileTransfers.end() && it->second.isReceiver) {
					auto &ft = it->second;
					ft.done = true;
					if (ft.outFile.is_open())
						ft.outFile.close();
					pushFileEvent(ZCom_getNode(ft.nodeID), eZCom_EventFile_Complete, connID, fid, nullptr);
				}
				enet_packet_destroy(event.packet);
				break;
			}

			// Normal data received — forward full stream to data callback
			ZCom_cbDataReceived(connID, streamData);
			enet_packet_destroy(event.packet);
			break;
		}

		case ENET_EVENT_TYPE_DISCONNECT: {
			connID = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(event.peer->data));
			event.peer->data = nullptr; // Clear so reconnecting peer is treated as new incoming
			m_peerMap.erase(connID);
			m_addressMap.erase(connID);
			m_waitingForReply.erase(connID);
			m_announcedNodes.erase(connID);
			m_peerRole.erase(connID);
			// Per-connID purge of the shared per-nodeID buffers (pending
			// replicas / events / unique-announces / file-offers) lives in
			// dropPeerState() so this path and the T3.1 over-read close below
			// stay in sync.
			// m_pendingAnnounce holds LOCAL node IDs awaiting outbound announce
			// to all currently-connected peers; do NOT clear it here, or nodes
			// registered this tick would never reach the remaining peers.
			dropPeerState(connID);

			// Use pending disconnect data if available, otherwise empty
			ZCom_BitStream reasonData;
			auto it = m_pendingDisconnectData.find(connID);
			if (it != m_pendingDisconnectData.end()) {
				reasonData = it->second;
				m_pendingDisconnectData.erase(it);
			}
			ZCom_cbConnectionClosed(connID, eZCom_ClosedDisconnect, reasonData);
			break;
		}

		default:
			break;
	}

	// T3.1: detect BitStream over-read on the dispatch stream. A protocol
	// desync (e.g. version skew, missing replicator slot) makes getInt/...
	// return 0 past end-of-stream; without this check the garbage cascades
	// silently through every following read. On over-read, close the
	// connection (cbConnectionClosed fires on the subsequent DISCONNECT
	// event, matching the rejection path at line ~886). Only RECEIVE sets
	// streamData content, so non-receive events skip this (flag is false).
	if (streamData.getReadError()) {
		NET_LOG("BitStream over-read on conn " << connID << " — protocol desync, closing connection");
		ENetPeer *peer = findPeer(connID);
		if (peer)
			enet_peer_disconnect(peer, 0);
		m_peerMap.erase(connID);
		m_addressMap.erase(connID);
		m_waitingForReply.erase(connID);
		m_announcedNodes.erase(connID);
		m_peerRole.erase(connID);
		dropPeerState(connID);
	}
}

void ZCom_Control::sendConnectionReply(ENetPeer *peer, ZCom_BitStream &reply, bool accepted) {
	// Serialize: MSG_CONNECTION_REPLY (8 bits) + result (8 bits)
	// + PROTOCOL_VERSION (8 bits) + byteLen (16 bits) + raw reply bytes.
	// The version lets the client refuse a version-skewed server cleanly
	// (eZCom_ConnWrongVersion) instead of silently desyncing.
	size_t replyBits = reply.getBitLength();
	size_t replyBytes = (replyBits + 7) / 8;
	const uint8_t *replyData = reply.getData();

	ZCom_BitStream pkt;
	pkt.addInt(MSG_CONNECTION_REPLY, 8);
	pkt.addInt(accepted ? static_cast<int>(eZCom_ConnAccepted) : static_cast<int>(eZCom_ConnDenied), 8);
	pkt.addInt(getProtocolVersion(), 8);
	pkt.addInt(static_cast<int>(replyBytes), 16);
	for (size_t i = 0; i < replyBytes; ++i) {
		pkt.addInt(replyData[i], 8);
	}

	ENetPacket *packet = enet_packet_create(pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(peer, 0, packet);
}

void ZCom_Control::dispatchNodeEvent(uint32_t nodeID, int type, int role, uint32_t connID, ZCom_BitStream *data,
									 zU32 estimatedTimeSent) {
	// O(1) lookup by node ID instead of a linear scan over every registered
	// node on every incoming event. IDs are unique (the registry keys by ID),
	// so at most one node matches.
	ZCom_Node *node = m_nodeRegistry.find(nodeID);
	if (node) {
		node->pushEvent(static_cast<eZCom_Event>(type), static_cast<eZCom_NodeRole>(role), connID, data,
						estimatedTimeSent);
		return;
	}
	if (data) {
		// Node not registered locally yet (e.g. the announcement is still in
		// flight). Buffer the event so it can be replayed once the node exists.
		PendingNodeEvent ev;
		ev.type = type;
		ev.role = role;
		ev.connID = connID;
		ev.estimatedTimeSent = estimatedTimeSent;
		// Copy the remaining (unread) portion of the stream, preserving the
		// current read position so the payload can be decoded on replay.
		auto remaining = data->Duplicate();
		ev.data = *remaining;
		ev.data.resetReadState();
		// remaining (unique_ptr) frees at scope end
		m_pendingNodeEvents[nodeID].push_back(std::move(ev));
		NET_LOG("Buffered node event for nodeID=" << nodeID << " type=" << type << " ("
												  << m_pendingNodeEvents[nodeID].size() << " pending)");
	}
}

void ZCom_Control::replayPendingNodeEvents(ZCom_Node *node) {
	if (!node)
		return;
	uint32_t nid = node->getNetworkID();
	auto it = m_pendingNodeEvents.find(nid);
	if (it == m_pendingNodeEvents.end())
		return;

	NET_LOG("Replaying " << it->second.size() << " buffered node event(s) for nodeID=" << nid);
	for (auto &ev : it->second) {
		node->pushEvent(static_cast<eZCom_Event>(ev.type), static_cast<eZCom_NodeRole>(ev.role), ev.connID, &ev.data,
						ev.estimatedTimeSent);
	}
	m_pendingNodeEvents.erase(it);
}

void ZCom_Control::applyRequestNodeID(ZCom_Node *node) {
	if (!node)
		return;
	// Assign the server-announced node ID to the local proxy/owner node.
	// Only meaningful inside cbNodeRequest_Dynamic (request active); outside
	// the callback, registerNode() assigns a fresh authority-side ID.
	// Push/announce/replay are handled by registerNode/registerExistingNode,
	// which registerNodeDynamic/registerRequestedNode call right after this.
	if (m_requestCtx.active) {
		node->setNodeID(m_requestCtx.net_id);
		NET_LOG("Assigned announced nodeID=" << node->getNetworkID() << " classID=" << node->getClassID() << " ("
											 << ZCom_getClassName(node->getClassID()) << ")"
											 << " role=" << node->getRole());
	}
}

void ZCom_Control::applyRequestRole(ZCom_Node *node) {
	if (!node || !m_requestCtx.active)
		return;
	// Per Zoidcom semantics, a node registered inside cbNodeRequest_Dynamic
	// takes the role announced by the server (Owner or Proxy). The node must
	// not remain eZCom_RoleAuthority, otherwise sendEvent() rule checks
	// (e.g. OWNER_2_AUTH used by selectWeapons) would be wrong.
	node->setRole(static_cast<eZCom_NodeRole>(m_requestCtx.role));
}
// ---- Global ZoidCom functions ----

bool ZCom_initSockets(bool isServer, int port, int maxClients, int something) {
	(void)something;

	if (enet_initialize() != 0) {
		return false;
	}

	ENetHost *host = nullptr;
	if (isServer) {
		ENetAddress address;
		address.host = ENET_HOST_ANY;
		address.port = static_cast<uint16_t>(port);
		host = enet_host_create(&address, static_cast<size_t>(maxClients > 0 ? maxClients : 1),
								3, // channels: control/events=0, file transfer=1, replicators=2
								0, // unlimited incoming bandwidth
								0  // unlimited outgoing bandwidth
		);
	} else {
		host = enet_host_create(nullptr,
								1, // only 1 outgoing connection for client
								3, // channels: control/events=0, file transfer=1, replicators=2
								0, // unlimited incoming bandwidth
								0  // unlimited outgoing bandwidth
		);
	}
	// Enable ENet's built-in range-coder compression (negotiated per-connection).
	enet_host_compress_with_range_coder(host);

	if (!host) {
		return false;
	}

	if (g_currentControl) {
		g_currentControl->setHost(host, isServer);
	}

	return true;
}

void ZCom_simulateLag(int val, int lag) {
	if (g_currentControl)
		g_currentControl->ZCom_simulateLag(static_cast<ZCom_ConnID>(val), static_cast<zU32>(lag));
}

void ZCom_simulateLoss(int val, float loss) {
	if (g_currentControl)
		g_currentControl->ZCom_simulateLoss(static_cast<ZCom_ConnID>(val), static_cast<zFloat>(loss));
}

void ZCom_setControlID(int id) {
	(void)id;
}

void ZCom_setDebugName(const char *name) {
	(void)name;
}

void ZCom_setUpstreamLimit(int limit, int something) {
	if (g_currentControl)
		g_currentControl->ZCom_setUpstreamLimit(static_cast<zU32>(limit), static_cast<zU32>(something));
}

void ZCom_sendData(uint32_t connID, ZCom_BitStream *stream, eZCom_SendMode mode) {
	if (g_currentControl) {
		g_currentControl->ZCom_sendData(connID, stream, mode);
	}
}

void ZCom_requestDownstreamLimit(uint32_t connID, int pps, int bpp) {
	if (g_currentControl)
		g_currentControl->ZCom_requestDownstreamLimit(connID, static_cast<zU16>(pps), static_cast<zU16>(bpp));
}

void ZCom_requestZoidMode(uint32_t connID, uint8_t level) {
	if (!g_currentControl)
		return;

	ENetPeer *peer = g_currentControl->findPeer(connID);
	if (!peer)
		return;

	// Send zoid mode request to the server
	ZCom_BitStream pkt;
	pkt.addInt(MSG_ZOID_REQUEST, 8);
	pkt.addInt(level, 8);

	ENetPacket *packet = enet_packet_create(pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(peer, 0, packet);
}

void ZCom_Disconnect(uint32_t id, ZCom_BitStream *data) {
	if (g_currentControl) {
		g_currentControl->ZCom_Disconnect(id, data);
	}
}

// ============================================================================
// File transfer (Phase E)
// ============================================================================

ZCom_FileTransID ZCom_Control::ZCom_sendFile(ZCom_Node *node, const char *_path, const char *_pathtosend,
											 ZCom_ConnID _destconn, ZCom_BitStream *_data, zFloat _aggressivenes) {
	if (!_path || _destconn == 0)
		return ZCom_Invalid_ID;
	ENetPeer *peer = findPeer(_destconn);
	if (!peer)
		return ZCom_Invalid_ID;

	std::ifstream f(_path, std::ios::binary | std::ios::ate);
	if (!f.is_open())
		return ZCom_Invalid_ID;
	std::streamsize fsize = f.tellg();
	if (fsize < 0)
		fsize = 0;
	f.seekg(0, std::ios::beg);

	ZCom_FileTransID fid = m_nextFileTransID++;
	auto &ft = m_fileTransfers[fid];
	ft.id = fid;
	ft.nodeID = node ? node->getNetworkID() : 0;
	ft.isReceiver = false;
	ft.peerConnID = _destconn;
	ft.path = _path;
	ft.pathtosend = _pathtosend ? _pathtosend : _path;
	ft.size = static_cast<zU32>(fsize);
	ft.transferred = 0;
	ft.bps = 0;
	ft.aggressiveness = _aggressivenes > 0.0f ? _aggressivenes : 1.0f;
	ft.accepted = false;
	ft.aborted = false;
	ft.done = false;
	ft.inFile = std::move(f);
	if (_data) {
		size_t bl = _data->getDataLength();
		ft.offerData.assign(_data->getData(), _data->getData() + bl);
	}

	ZCom_BitStream pkt;
	pkt.addInt(MSG_FILE_OFFER, 8);
	pkt.addInt(ft.nodeID, 16);
	pkt.addInt(fid, ZCOM_FTRANS_ID_BITS);
	pkt.addInt(ft.size, ZCOM_FTRANS_SIZE_BITS);
	pkt.addString(ft.pathtosend.c_str());
	if (_data && !ft.offerData.empty())
		pkt.addBuffer(reinterpret_cast<const char *>(ft.offerData.data()), static_cast<zU16>(ft.offerData.size()));
	else
		pkt.addBuffer("", 0);

	ENetPacket *p = enet_packet_create(pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(peer, 0, p);
	enet_host_flush(m_host);

	NET_LOG("ZCom_sendFile fid=" << fid << " size=" << ft.size << " destconn=" << _destconn << " path=" << _path);
	return fid;
}

void ZCom_Control::ZCom_acceptFile(ZCom_Node *node, ZCom_ConnID _src_id, ZCom_FileTransID _ftrans_id, const char *_path,
								   bool _accept) {
	auto it = m_fileTransfers.find(_ftrans_id);
	if (it == m_fileTransfers.end() || !it->second.isReceiver)
		return;
	auto &ft = it->second;

	if (_accept) {
		std::string savePath = _path ? _path : ft.pathtosend;
		ft.path = savePath;
		ft.accepted = true;
		ft.outFile.open(savePath, std::ios::binary | std::ios::trunc);

		ZCom_BitStream pkt;
		pkt.addInt(MSG_FILE_ACCEPT, 8);
		pkt.addInt(ft.id, ZCOM_FTRANS_ID_BITS);
		pkt.addInt(1, 1);
		if (ENetPeer *peer = findPeer(_src_id)) {
			ENetPacket *p = enet_packet_create(pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
			enet_peer_send(peer, 0, p);
			enet_host_flush(m_host);
		}
		NET_LOG("ZCom_acceptFile accept fid=" << ft.id << " savePath=" << savePath);
	} else {
		ft.aborted = true;
		if (ft.outFile.is_open())
			ft.outFile.close();

		ZCom_BitStream pkt;
		pkt.addInt(MSG_FILE_ABORT, 8);
		pkt.addInt(ft.id, ZCOM_FTRANS_ID_BITS);
		if (ENetPeer *peer = findPeer(_src_id)) {
			ENetPacket *p = enet_packet_create(pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
			enet_peer_send(peer, 0, p);
			enet_host_flush(m_host);
		}
		// Raise the Aborted event on the receiver's own node.
		pushFileEvent(node, eZCom_EventFile_Aborted, _src_id, ft.id, nullptr);
		NET_LOG("ZCom_acceptFile deny fid=" << ft.id);
	}
}

const ZCom_FileTransInfo &ZCom_Control::ZCom_getFileInfo(ZCom_ConnID _conn_id, ZCom_FileTransID _ftrans_id) const {
	(void)_conn_id;
	auto it = m_fileTransfers.find(_ftrans_id);
	if (it == m_fileTransfers.end()) {
		m_fileInfoBuf = ZCom_FileTransInfo{};
		m_fileInfoBuf.id = ZCom_Invalid_ID;
		return m_fileInfoBuf;
	}
	const auto &ft = it->second;
	m_fileInfoBuf = ZCom_FileTransInfo{};
	m_fileInfoBuf.id = ft.id;
	m_fileInfoBuf.size = ft.size;
	m_fileInfoBuf.transferred = ft.transferred;
	m_fileInfoBuf.bps = ft.bps;
	m_fileInfoBuf.path = ft.path.c_str();
	// legacy/extension fields
	m_fileInfoBuf.m_id = ft.id;
	m_fileInfoBuf.m_file_size = ft.size;
	m_fileInfoBuf.m_bytes_downloaded = ft.transferred;
	m_fileInfoBuf.m_filename = ft.path.c_str();
	m_fileInfoBuf.m_progress = ft.size ? float(ft.transferred) / float(ft.size) : 0.0f;
	return m_fileInfoBuf;
}

void ZCom_Control::pumpFileTransfers() {
	if (!m_host)
		return;
	zU32 now = ZCom_getCurrentTime();
	bool anySent = false;

	for (auto &kv : m_fileTransfers) {
		auto &ft = kv.second;
		if (ft.isReceiver || !ft.accepted || ft.aborted || ft.done)
			continue;
		if (!ft.inFile.is_open())
			continue;

		// Bytes to send this tick, scaled by aggressiveness. Cap at 65535:
		// addBuffer encodes the length in ZCOM_FTRANS_CHUNK_BITS (16) so a
		// 65536-byte chunk would overflow zU16 to 0 and be silently dropped.
		zU32 budget = static_cast<zU32>(32768u * ft.aggressiveness);
		if (budget < 1024)
			budget = 1024;
		if (budget > 65535)
			budget = 65535;
		std::vector<char> chunk(budget);
		ft.inFile.read(chunk.data(), budget);
		std::streamsize got = ft.inFile.gcount();

		if (got <= 0) {
			if (ft.transferred >= ft.size) {
				// Clean EOF: every byte was sent — complete the transfer.
				ft.done = true;
				ft.inFile.close();
				ZCom_BitStream pkt;
				pkt.addInt(MSG_FILE_COMPLETE, 8);
				pkt.addInt(ft.id, ZCOM_FTRANS_ID_BITS);
				if (ENetPeer *peer = findPeer(ft.peerConnID)) {
					ENetPacket *p = enet_packet_create(pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
					// File data + complete share channel 1 so they stay ordered
					// among themselves AND don't head-of-line-block gameplay
					// traffic on channel 0 during large level downloads.
					enet_peer_send(peer, CH_FILE, p);
					anySent = true;
				}
				// Notify the SENDER's own node that the transfer completed,
				// mirroring the aborted branch below and the receiver-side
				// MSG_FILE_COMPLETE handler. Without this the sender's game
				// code never sees eZCom_EventFile_Complete, so its sendingFile
				// flag stays set and the connection flow stalls (the updater
				// never dequeues the trailing MsgRequestDone, and the client
				// never reconnects to load the freshly-downloaded level).
				pushFileEvent(ZCom_getNode(ft.nodeID), eZCom_EventFile_Complete, ft.peerConnID, ft.id, nullptr);
			} else {
				// L1: premature EOF / read error before all bytes were sent.
				// Without this the transfer would stall forever (no complete,
				// no abort, no event) and the receiver would hang waiting.
				ft.aborted = true;
				if (ft.inFile.is_open())
					ft.inFile.close();
				ZCom_BitStream pkt;
				pkt.addInt(MSG_FILE_ABORT, 8);
				pkt.addInt(ft.id, ZCOM_FTRANS_ID_BITS);
				if (ENetPeer *peer = findPeer(ft.peerConnID)) {
					ENetPacket *p = enet_packet_create(pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
					enet_peer_send(peer, CH_FILE, p);
					anySent = true;
				}
				pushFileEvent(ZCom_getNode(ft.nodeID), eZCom_EventFile_Aborted, ft.peerConnID, ft.id, nullptr);
			}
			continue;
		}

		zU16 sendLen = static_cast<zU16>(got);
		ZCom_BitStream pkt;
		pkt.addInt(MSG_FILE_DATA, 8);
		pkt.addInt(ft.id, ZCOM_FTRANS_ID_BITS);
		pkt.addBuffer(chunk.data(), sendLen);
		if (ENetPeer *peer = findPeer(ft.peerConnID)) {
			ENetPacket *p = enet_packet_create(pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
			// Channel 1 (dedicated file stream): chunks are reliable+ordered
			// among themselves but independent of game traffic on channel 0.
			enet_peer_send(peer, 1, p);
			ft.transferred += sendLen;
			ft.bytesThisSec += sendLen;
			anySent = true;
		}

		// Update bps once per second.
		if (now - ft.lastBpsTime >= 1000) {
			if (ft.lastBpsTime != 0)
				ft.bps = ft.bytesThisSec * 1000 / (now - ft.lastBpsTime);
			ft.bytesThisSec = 0;
			ft.lastBpsTime = now;
		}
	}

	// M3: reap finished transfers after a short grace. Completed/aborted
	// entries were never erased, so a long-running server leaked every
	// transfer's std::string/vector/fstream forever. The 2s grace lets the
	// game's event handler (which calls getFileInfo on the queued
	// Complete/Aborted event) still see valid info before we drop the entry.
	for (auto it = m_fileTransfers.begin(); it != m_fileTransfers.end();) {
		auto &ft = it->second;
		if (ft.done || ft.aborted) {
			if (ft.finishedTime == 0)
				ft.finishedTime = now;
			if (now - ft.finishedTime > 2000) {
				it = m_fileTransfers.erase(it);
				continue;
			}
		}
		++it;
	}

	if (anySent)
		enet_host_flush(m_host);
}

void ZCom_Control::deliverPendingFileOffers(ZCom_Node *node) {
	if (!node)
		return;
	uint32_t nid = node->getNetworkID();
	auto it = m_pendingFileOffers.find(nid);
	if (it == m_pendingFileOffers.end())
		return;
	for (ZCom_FileTransID fid : it->second) {
		auto fit = m_fileTransfers.find(fid);
		if (fit == m_fileTransfers.end())
			continue;
		pushFileEvent(node, eZCom_EventFile_Incoming, fit->second.peerConnID, fid, &fit->second.offerData);
	}
	m_pendingFileOffers.erase(it);
}