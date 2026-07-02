#include "net_control.h"
#include "net_bitstream.h"
#include "net_address.h"
#include <cstring>
#include <algorithm>
#include <iostream>

// Verbose logging for network debugging
#define NET_DEBUG
#ifdef NET_DEBUG
#define NET_LOG(x_) do { std::cerr << "[NET] " << x_ << std::endl; } while(0)
#else
#define NET_LOG(x_) (void)0
#endif

// Current control for global function dispatch
ZCom_Control* g_currentControl = nullptr;

ZCom_Control::ZCom_Control()
	: m_host(nullptr)
	, m_isServer(false)
	, m_nextConnID(1)
	, m_nextNodeID(1)
	, m_nextClassID(1)
	, m_logFn(nullptr)
{
	g_currentControl = this;
}

ZCom_Control::~ZCom_Control()
{
	Shutdown();
	if (g_currentControl == this)
		g_currentControl = nullptr;
}

uint32_t ZCom_Control::ZCom_registerClass(const char* name, uint32_t flags)
{
	ZCom_ClassInfo info;
	info.name = name ? name : "";
	info.flags = flags;
	m_classes.push_back(info);
	return static_cast<uint32_t>(m_classes.size());
}

uint32_t ZCom_Control::ZCom_getClassID(const char* name) const
{
	if (!name) return 0;
	for (size_t i = 0; i < m_classes.size(); ++i) {
		if (m_classes[i].name == name)
			return static_cast<uint32_t>(i + 1);
	}
	return 0;
}

void ZCom_Control::ZCom_processOutput()
{
	if (!m_host) return;
	g_currentControl = this;

	// Only servers broadcast replicator state — clients only receive
	if (!m_isServer) return;

	// Broadcast replicator state for all authority-owned nodes only
	for (auto* node : m_nodes) {
		// Only authority nodes broadcast — proxies receive
		if (node->getRole() != eZCom_RoleAuthority) continue;

		ZCom_BitStream repPacked;
		node->packAllReplicators(&repPacked);

		if (repPacked.getDataLength() > 0) {
			ZCom_BitStream pkt;
			pkt.addInt(MSG_REPLICATORS, 8);
			pkt.addInt(node->getNetworkID(), 16);
			// Add replicator data as raw bytes (no addBitStream wrapper)
			for (size_t i = 0; i < repPacked.getDataLength(); ++i)
				pkt.addInt(repPacked.getData()[i], 8);

			NET_LOG("Broadcasting replicators for nodeID=" << node->getNetworkID()
				<< " classID=" << node->getClassID() << " role=" << node->getRole()
				<< " dataLen=" << repPacked.getDataLength());

			ENetPacket* packet = enet_packet_create(
				pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
			enet_host_broadcast(m_host, 0, packet);
		}
	}

	enet_host_flush(m_host);
}

void ZCom_Control::ZCom_processReplicators(uint32_t simulation_time_passed)
{
	(void)simulation_time_passed;
	// Stub — advanced per-replicator Process() and time-based throttling
	// will be implemented when per-replicator delay tracking is added.
	// Currently replicator state checking happens in ZCom_processOutput.
}

ZCom_Node* ZCom_Control::ZCom_getNode(uint32_t nid)
{
	for (auto* node : m_nodes) {
		if (node->getNetworkID() == nid)
			return node;
	}
	return nullptr;
}

void ZCom_Control::ZCom_processInput(int flags)
{
	if (!m_host) return;
	g_currentControl = this;

	ENetEvent event;
	int timeout = (flags == eZCom_NoBlock) ? 0 : 10;
	
	while (enet_host_service(m_host, &event, timeout) > 0) {
		processENetEvent(event);
		timeout = 0; // no blocking after first event
	}
}

uint32_t ZCom_Control::ZCom_Connect(ZCom_Address& addr, ZCom_BitStream* data)
{
	if (!m_host) return ZCom_Invalid_ID;
	
	ENetPeer* peer = enet_host_connect(m_host, &addr.getENetAddress(), 2, 0);
	if (!peer) {
		if (m_logFn) m_logFn("ENet: Failed to create connection");
		return ZCom_Invalid_ID;
	}
	
	uint32_t connID = allocateConnID();
	m_peerMap[connID] = peer;
	m_addressMap[connID] = addr;
	peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(connID));
	
	// Connection request data is stored but not sent directly —
	// the application-level handshake happens after ENet transport connects.
	(void)data;
	
	return connID;
}

void ZCom_Control::ZCom_disconnectAll(ZCom_BitStream* data)
{
	if (!m_host) return;

	// Send disconnect reason data to peers before disconnecting
	if (data) {
		ZCom_BitStream pkt;
		pkt.addInt(MSG_DISCONNECT_DATA, 8);
		for (size_t i = 0; i < data->getDataLength(); ++i)
			pkt.addInt(data->getData()[i], 8);

		ENetPacket* packet = enet_packet_create(
			pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
		enet_host_broadcast(m_host, 0, packet);
		enet_host_flush(m_host);
	}

	for (auto& pair : m_peerMap) {
		if (pair.second) {
			enet_peer_disconnect(pair.second, 0);
		}
	}
	m_peerMap.clear();
	m_addressMap.clear();
}

void ZCom_Control::ZCom_Disconnect(uint32_t id, ZCom_BitStream* data)
{
	ENetPeer* peer = findPeer(id);

	// Send disconnect reason data before disconnecting
	if (peer && data && data->getDataLength() > 0) {
		ZCom_BitStream pkt;
		pkt.addInt(MSG_DISCONNECT_DATA, 8);
		for (size_t i = 0; i < data->getDataLength(); ++i)
			pkt.addInt(data->getData()[i], 8);

		ENetPacket* packet = enet_packet_create(
			pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(peer, 0, packet);
		enet_host_flush(m_host);
	}

	if (peer) {
		enet_peer_disconnect(peer, 0);
		m_peerMap.erase(id);
		m_addressMap.erase(id);
	}
}

void ZCom_Control::Shutdown()
{
	if (m_host) {
		for (auto& pair : m_peerMap) {
			if (pair.second) {
				enet_peer_disconnect_now(pair.second, 0);
			}
		}
		m_peerMap.clear();
		m_addressMap.clear();
		enet_host_destroy(m_host);
		m_host = nullptr;
	}
	m_nodes.clear();
	m_pendingReplicas.clear();
}

void ZCom_Control::disconnectPeer(uint32_t connID)
{
	ENetPeer* peer = findPeer(connID);
	if (peer) {
		enet_peer_disconnect(peer, 0);
		m_peerMap.erase(connID);
		m_addressMap.erase(connID);
	}
}

ENetPeer* ZCom_Control::findPeer(uint32_t connID)
{
	auto it = m_peerMap.find(connID);
	return (it != m_peerMap.end()) ? it->second : nullptr;
}

void ZCom_Control::ZCom_sendData(uint32_t connID, ZCom_BitStream* stream, int mode)
{
	ENetPeer* peer = findPeer(connID);
	if (!peer || !stream) return;
	
	enet_uint32 flags = 0;
	switch (mode) {
		case eZCom_ReliableOrdered:
		case eZCom_ReliableUnordered:
			flags = ENET_PACKET_FLAG_RELIABLE;
			break;
		case eZCom_Unreliable:
		case eZCom_UnreliableNotify:
		default:
			flags = 0;
			break;
	}
	
	ENetPacket* packet = enet_packet_create(
		stream->getData(),
		stream->getDataLength(),
		flags
	);
	enet_peer_send(peer, 0, packet);
}

void ZCom_Control::sendToAll(int mode, ZCom_BitStream* stream)
{
	if (!m_host || !stream) return;
	
	enet_uint32 flags = 0;
	switch (mode) {
		case eZCom_ReliableOrdered:
		case eZCom_ReliableUnordered:
			flags = ENET_PACKET_FLAG_RELIABLE;
			break;
		default:
			flags = 0;
			break;
	}
	
	ENetPacket* packet = enet_packet_create(
		stream->getData(),
		stream->getDataLength(),
		flags
	);
	enet_host_broadcast(m_host, 0, packet);
}

void ZCom_Control::replayPendingReplicators(ZCom_Node* node)
{
	if (!node) return;
	uint32_t nid = node->getNetworkID();
	auto it = m_pendingReplicas.find(nid);
	if (it == m_pendingReplicas.end()) return;

	NET_LOG("Replaying buffered replicator data for nodeID=" << nid);
	it->second.resetReadState();
	node->unpackAllReplicators(&it->second, true, 0);
	m_pendingReplicas.erase(it);
}

bool ZCom_Control::registerExistingNode(ZCom_Node* node)
{
	if (!node) return false;
	node->setControl(this);
	m_nodes.push_back(node);
	// Replay any buffered replicator data for this node
	replayPendingReplicators(node);
	return true;
}

bool ZCom_Control::registerNode(ZCom_Node* node)
{
	if (!node) return false;
	node->setNodeID(m_nextNodeID++);
	node->setControl(this);
	m_nodes.push_back(node);

	// Replay any buffered replicator data for this node
	replayPendingReplicators(node);

	// Auto-announce non-unique nodes to all connected peers
	if (!node->isUnique()) {
		announceNodeWithOwner(node);
	}
	return true;
}

void ZCom_Control::sendNodeAnnouncement(uint32_t connID, ZCom_Node* node, int role)
{
    if (!m_host || !node) return;

    // Skip duplicate announcements to the same peer
    if (m_announcedNodes[connID].count(node->getNetworkID())) return;
    m_announcedNodes[connID].insert(node->getNetworkID());

    NET_LOG("sendNodeAnnouncement: connID=" << connID
        << " nodeID=" << node->getNetworkID()
        << " role=" << role);

	ZCom_BitStream pkt;
	pkt.addInt(MSG_NODE_ANNOUNCE, 8);
	pkt.addInt(node->getClassID(), 8);
	pkt.addInt(node->getNetworkID(), 16);
	pkt.addInt(role, 8);
	
	ZCom_BitStream* ad = node->getAnnounceData();
	if (ad && ad->getDataLength() > 0) {
		pkt.addInt(static_cast<int>(ad->getDataLength()), 16);
		for (size_t i = 0; i < ad->getDataLength(); ++i)
			pkt.addInt(ad->getData()[i], 8);
	} else {
		pkt.addInt(0, 16);
	}
	
	ENetPacket* packet = enet_packet_create(
		pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
	
	ENetPeer* peer = findPeer(connID);
	if (peer)
		enet_peer_send(peer, 0, packet);
	else
		enet_packet_destroy(packet);

	// Push eEvent_Init for nodes with event notification enabled
	if (node->getEventNotification()) {
		node->pushEvent(eZCom_EventInit, static_cast<eZCom_NodeRole>(role), connID, nullptr);
	}
}

void ZCom_Control::announceNodeToAll(ZCom_Node* node, int role)
{
	if (!m_host || !node) return;

	ZCom_BitStream pkt;
	pkt.addInt(MSG_NODE_ANNOUNCE, 8);
	pkt.addInt(node->getClassID(), 8);
	pkt.addInt(node->getNetworkID(), 16);
	pkt.addInt(role, 8);

	ZCom_BitStream* ad = node->getAnnounceData();
	if (ad && ad->getDataLength() > 0) {
		pkt.addInt(static_cast<int>(ad->getDataLength()), 16);
		for (size_t i = 0; i < ad->getDataLength(); ++i)
			pkt.addInt(ad->getData()[i], 8);
	} else {
		pkt.addInt(0, 16);
	}

	ENetPacket* packet = enet_packet_create(
		pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
	enet_host_broadcast(m_host, 0, packet);

	// Track per-peer so re-announcements are skipped
	for (auto& pair : m_peerMap) {
		if (m_announcedNodes[pair.first].count(node->getNetworkID())) continue;
		m_announcedNodes[pair.first].insert(node->getNetworkID());
	}

	// Push eEvent_Init for each peer if node has event notification enabled
	if (node->getEventNotification()) {
		for (auto& pair : m_peerMap) {
			node->pushEvent(eZCom_EventInit, static_cast<eZCom_NodeRole>(role), pair.first, nullptr);
		}
	}
}

void ZCom_Control::clearAnnouncedNode(uint32_t nodeID)
{
	// Clear per-peer tracking for this node so re-announcements (e.g. from setOwner)
	// will go through with the updated role.
	for (auto& pair : m_peerMap) {
		m_announcedNodes[pair.first].erase(nodeID);
	}
}

void ZCom_Control::announceNodeWithOwner(ZCom_Node* node)
{
    if (!m_host || !node) return;

    NET_LOG("announceNodeWithOwner: nodeID=" << node->getNetworkID()
        << " ownerID=" << node->getOwner()
        << " peers=" << m_peerMap.size());
    uint32_t ownerID = node->getOwner();
    for (auto& pair : m_peerMap) {
        NET_LOG("  peer connID=" << pair.first);
        // Skip duplicate announcements to the same peer
        if (m_announcedNodes[pair.first].count(node->getNetworkID())) {
            NET_LOG("  skipping duplicate for connID=" << pair.first);
            continue;
        }

        int role;
        if (ownerID != 0 && pair.first == ownerID) {
            role = eZCom_RoleOwner;
        } else {
            role = eZCom_RoleProxy;
        }
        sendNodeAnnouncement(pair.first, node, role);
    }

    // Also push eEvent_Init for each peer
    if (node->getEventNotification()) {
        for (auto& pair : m_peerMap) {
            node->pushEvent(eZCom_EventInit, eZCom_RoleProxy, pair.first, nullptr);
        }
    }
}

void ZCom_Control::syncNodesToPeer(ENetPeer* peer)
{
	if (!m_host || !peer) return;
	
	for (auto* node : m_nodes) {
		if (node->isUnique()) continue;
		ZCom_BitStream pkt;
		pkt.addInt(MSG_NODE_ANNOUNCE, 8);
		pkt.addInt(node->getClassID(), 8);
		pkt.addInt(node->getNetworkID(), 16);
		pkt.addInt(node->getRole(), 8);
		
		ZCom_BitStream* ad = node->getAnnounceData();
		if (ad && ad->getDataLength() > 0) {
			pkt.addInt(static_cast<int>(ad->getDataLength()), 16);
			for (size_t i = 0; i < ad->getDataLength(); ++i)
				pkt.addInt(ad->getData()[i], 8);
		} else {
			pkt.addInt(0, 16);
		}
		
		ENetPacket* packet = enet_packet_create(
			pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(peer, 0, packet);
	}
	enet_host_flush(m_host);
}

void ZCom_Control::removeNode(ZCom_Node* node)
{
	if (!node) return;
	auto it = std::find(m_nodes.begin(), m_nodes.end(), node);
	if (it != m_nodes.end())
		m_nodes.erase(it);
}

ZCom_Address const* ZCom_Control::ZCom_getPeer(uint32_t id)
{
	auto it = m_addressMap.find(id);
	return (it != m_addressMap.end()) ? &it->second : nullptr;
}

ZCom_ConnStats ZCom_Control::ZCom_getConnectionStats(uint32_t id)
{
	ZCom_ConnStats stats = ZCom_ConnStats();
	ENetPeer* peer = findPeer(id);
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

uint32_t ZCom_Control::allocateConnID()
{
	return m_nextConnID++;
}

void ZCom_Control::processENetEvent(ENetEvent& event)
{
	uint32_t connID;
	ZCom_BitStream streamData;
	
	switch (event.type) {
		case ENET_EVENT_TYPE_CONNECT: {
			if (event.peer->data != nullptr) {
				// Outgoing connection (client-side): ENet transport connected.
				// Do NOT fire cbConnectResult yet — wait for the server to
				// send MSG_CONNECTION_REPLY with the reply data.
				connID = static_cast<uint32_t>(
					reinterpret_cast<uintptr_t>(event.peer->data));
				m_waitingForReply.insert(connID);
				break;
			}
			
			// Incoming connection (server-side)
			connID = allocateConnID();
			event.peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(connID));
			m_peerMap[connID] = event.peer;
			m_addressMap[connID] = ZCom_Address(event.peer->address);
			
			// Call the connection request callback to get reply data
			ZCom_BitStream request;
			ZCom_BitStream reply;
			
			if (ZCom_cbConnectionRequest(connID, request, reply)) {
				// Send reply data back to client as MSG_CONNECTION_REPLY
				sendConnectionReply(event.peer, reply, true);
				// Sync existing nodes to the new client (skip unique nodes — registered locally)
				for (auto* node : m_nodes) {
					if (node->isUnique()) continue;
					// Track per-peer so re-announcements are skipped
					if (m_announcedNodes[connID].count(node->getNetworkID())) continue;
					m_announcedNodes[connID].insert(node->getNetworkID());

					// Determine role for this client: Owner if node is owned by them, otherwise Proxy
					int role = eZCom_RoleProxy;
					uint32_t ownerID = node->getOwner();
					if (ownerID != 0 && connID == ownerID) {
						role = eZCom_RoleOwner;
					}

					ZCom_BitStream pkt;
					pkt.addInt(MSG_NODE_ANNOUNCE, 8);
					pkt.addInt(node->getClassID(), 8);
					pkt.addInt(node->getNetworkID(), 16);
					pkt.addInt(role, 8);
					
					ZCom_BitStream* ad = node->getAnnounceData();
					if (ad && ad->getDataLength() > 0) {
						pkt.addInt(static_cast<int>(ad->getDataLength()), 16);
						for (size_t i = 0; i < ad->getDataLength(); ++i)
							pkt.addInt(ad->getData()[i], 8);
					} else {
						pkt.addInt(0, 16);
					}
					
					ENetPacket* packet = enet_packet_create(
						pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
					enet_peer_send(event.peer, 0, packet);

					// Push eEvent_Init for nodes with event notification enabled
					if (node->getEventNotification()) {
						node->pushEvent(eZCom_EventInit, static_cast<eZCom_NodeRole>(role), connID, nullptr);
					}
				}
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
			connID = static_cast<uint32_t>(
				reinterpret_cast<uintptr_t>(event.peer->data));
			
			streamData.assign(
				static_cast<const uint8_t*>(event.packet->data),
				event.packet->dataLength
			);
			
			// Peek at the first byte to identify internal protocol messages
			int msgType = 0;
			if (event.packet->dataLength > 0) {
				msgType = static_cast<const uint8_t*>(event.packet->data)[0];
			}
			
			// Handle connection reply (sent by server after ENet connect)
			if (msgType == MSG_CONNECTION_REPLY && m_waitingForReply.count(connID)) {
				streamData.getInt(8); // consume msgType
				eZCom_ConnectResult result = static_cast<eZCom_ConnectResult>(streamData.getInt(8));
				int byteLen = streamData.getInt(16);
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
				bool accepted = ZCom_cbZoidRequest(connID,
					static_cast<uint8_t>(requestedLevel), reason);
				
				// Send result back to client
				ZCom_BitStream result;
				result.addInt(MSG_ZOID_RESULT, 8);
				result.addInt(accepted ? static_cast<int>(eZCom_ZoidEnabled)
					: static_cast<int>(eZCom_ZoidDisabled), 8);
				result.addInt(requestedLevel, 8);
				
				ENetPacket* pkt = enet_packet_create(
					result.getData(), result.getDataLength(),
					ENET_PACKET_FLAG_RELIABLE);
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
				ZCom_cbZoidResult(connID,
					static_cast<eZCom_ZoidResult>(resultCode),
					static_cast<uint8_t>(newLevel), reason);
				
				enet_packet_destroy(event.packet);
				break;
			}
			
			// Handle node events (msgType == 0 means node event)
			if (msgType == MSG_NODE_EVENT) {
				streamData.getInt(8); // consume msgType
				int nodeID = streamData.getInt(16);
				streamData.getInt(16); // consume addBitStream length prefix
				// Remaining data is the event payload (without the addBitStream length prefix)
				dispatchNodeEvent(nodeID, eZCom_EventUser, eZCom_RoleProxy, connID, &streamData);

				enet_packet_destroy(event.packet);
				break;
			}

			// Handle replicator state updates
			if (msgType == MSG_REPLICATORS) {
				streamData.getInt(8); // consume msgType
				uint32_t repNodeID = streamData.getInt(16);

				// Find the matching local node by networkID
				ZCom_Node* repNode = ZCom_getNode(repNodeID);

				if (repNode) {
					NET_LOG("Received MSG_REPLICATORS for nodeID=" << repNodeID
						<< " classID=" << repNode->getClassID() << " role=" << repNode->getRole()
						<< " dataLen=" << (event.packet->dataLength - 3));

					// Replicator data starts at byte offset 3 (msgType=1 + nodeID=2)
					size_t offset = 3;
					ZCom_BitStream repData(
						event.packet->data + offset,
						event.packet->dataLength - offset
					);
					repNode->unpackAllReplicators(&repData, true, 0);
				} else {
					NET_LOG("MSG_REPLICATORS: nodeID=" << repNodeID << " NOT FOUND locally — buffering for later (" << m_nodes.size() << " nodes)");

					// Buffer the replica data so it can be replayed when the node is registered
					size_t offset = 3;
					m_pendingReplicas[repNodeID].assign(
						event.packet->data + offset,
						event.packet->dataLength - offset
					);
				}

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
				ZCom_Node* existingNode = ZCom_getNode(net_id);
				if (existingNode) {
					existingNode->setRole(role);
					NET_LOG("Updating role for existing nodeID=" << net_id << " to " << role);
					enet_packet_destroy(event.packet);
					break;
				}

				// Skip duplicate announcements to this peer
				if (m_announcedNodes[connID].count(net_id)) {
					NET_LOG("Skipping duplicate MSG_NODE_ANNOUNCE for connID=" << connID
						<< " net_id=" << net_id);
					enet_packet_destroy(event.packet);
					break;
				}
				m_announcedNodes[connID].insert(net_id);

				NET_LOG("Received MSG_NODE_ANNOUNCE classID=" << classID
					<< " net_id=" << net_id << " role=" << role << " announceLen=" << announceLen);

				ZCom_BitStream* announceData = nullptr;
				if (announceLen > 0) {
					std::vector<uint8_t> buf(announceLen);
					for (int i = 0; i < announceLen; ++i)
						buf[i] = streamData.getInt(8);
					announceData = new ZCom_BitStream(buf.data(), buf.size());
				}

				ZCom_cbNodeRequest_Dynamic(connID, classID, announceData,
					role, net_id);

				delete announceData;
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

		// Normal data received — forward full stream to data callback
			ZCom_cbDataReceived(connID, streamData);
			enet_packet_destroy(event.packet);
			break;
		}
		
		case ENET_EVENT_TYPE_DISCONNECT: {
			connID = static_cast<uint32_t>(
				reinterpret_cast<uintptr_t>(event.peer->data));
			event.peer->data = nullptr; // Clear so reconnecting peer is treated as new incoming
			m_peerMap.erase(connID);
			m_addressMap.erase(connID);
			m_waitingForReply.erase(connID);
			m_announcedNodes.erase(connID);
			m_pendingReplicas.clear(); // stale replica data from disconnected peer

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
}

void ZCom_Control::sendConnectionReply(ENetPeer* peer, ZCom_BitStream& reply, bool accepted)
{
	// Serialize: MSG_CONNECTION_REPLY (8 bits) + result (8 bits) + byteLen (16 bits) + raw reply bytes
	size_t replyBits = reply.getBitLength();
	size_t replyBytes = (replyBits + 7) / 8;
	const uint8_t* replyData = reply.getData();
	
	ZCom_BitStream pkt;
	pkt.addInt(MSG_CONNECTION_REPLY, 8);
	pkt.addInt(accepted ? static_cast<int>(eZCom_ConnAccepted) : static_cast<int>(eZCom_ConnDenied), 8);
	pkt.addInt(static_cast<int>(replyBytes), 16);
	for (size_t i = 0; i < replyBytes; ++i) {
		pkt.addInt(replyData[i], 8);
	}
	
	ENetPacket* packet = enet_packet_create(
		pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(peer, 0, packet);
}

void ZCom_Control::dispatchNodeEvent(uint32_t nodeID, int type, int role, uint32_t connID, ZCom_BitStream* data)
{
	bool found = false;
	for (auto* node : m_nodes) {
		if (node->getNetworkID() == nodeID) {
			node->pushEvent(static_cast<eZCom_Event>(type),
				static_cast<eZCom_NodeRole>(role), connID, data);
			found = true;
			break;
		}
	}
	(void)found;
}
// ---- Global ZoidCom functions ----

bool ZCom_initSockets(bool isServer, int port, int maxClients, int something)
{
	(void)something;
	
	if (enet_initialize() != 0) {
		return false;
	}
	
	ENetHost* host = nullptr;
	if (isServer) {
		ENetAddress address;
		address.host = ENET_HOST_ANY;
		address.port = static_cast<uint16_t>(port);
		host = enet_host_create(&address, 
			static_cast<size_t>(maxClients > 0 ? maxClients : 1),
			2, // channels
			0, // unlimited incoming bandwidth
			0  // unlimited outgoing bandwidth
		);
	} else {
		host = enet_host_create(nullptr,
			1,  // only 1 outgoing connection for client
			2,  // channels
			0,  // unlimited incoming bandwidth
			0   // unlimited outgoing bandwidth
		);
	}
	
	if (!host) {
		return false;
	}
	
	if (g_currentControl) {
		g_currentControl->setHost(host, isServer);
	}
	
	return true;
}

void ZCom_simulateLag(int val, int lag)
{
	(void)val;
	(void)lag;
}

void ZCom_simulateLoss(int val, float loss)
{
	(void)val;
	(void)loss;
}

void ZCom_setControlID(int id)
{
	(void)id;
}

void ZCom_setDebugName(const char* name)
{
	(void)name;
}

void ZCom_setUpstreamLimit(int limit, int something)
{
	(void)limit;
	(void)something;
}

void ZCom_sendData(uint32_t connID, ZCom_BitStream* stream, eZCom_SendMode mode)
{
	if (g_currentControl) {
		g_currentControl->ZCom_sendData(connID, stream, mode);
	}
}

void ZCom_requestDownstreamLimit(uint32_t connID, int pps, int bpp)
{
	(void)connID;
	(void)pps;
	(void)bpp;
}

void ZCom_requestZoidMode(uint32_t connID, uint8_t level)
{
	if (!g_currentControl) return;

	ENetPeer* peer = g_currentControl->findPeer(connID);
	if (!peer) return;

	// Send zoid mode request to the server
	ZCom_BitStream pkt;
	pkt.addInt(MSG_ZOID_REQUEST, 8);
	pkt.addInt(level, 8);

	ENetPacket* packet = enet_packet_create(
		pkt.getData(), pkt.getDataLength(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(peer, 0, packet);
}

void ZCom_Disconnect(uint32_t id, ZCom_BitStream* data)
{
	if (g_currentControl) {
		g_currentControl->ZCom_Disconnect(id, data);
	}
}
