#include "net_control.h"
#include "net_bitstream.h"
#include "net_address.h"
#include <cstring>
#include <iostream>
#include <algorithm>

// Current control for global function dispatch
ZCom_Control* g_currentControl = nullptr;

// Track currently bound control for server/client operations
static ZCom_Control* s_boundControl = nullptr;

ZCom_Control::ZCom_Control()
	: m_host(nullptr)
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

void ZCom_Control::ZCom_processOutput()
{
	if (!m_host) return;
	g_currentControl = this;

	// Broadcast replicator state for all nodes
	for (auto* node : m_nodes) {
		ZCom_BitStream repPacked;
		node->packAllReplicators(&repPacked);

		if (repPacked.getDataLength() > 0) {
			ZCom_BitStream pkt;
			pkt.addInt(MSG_REPLICATORS, 8);
			pkt.addInt(node->getNetworkID(), 16);
			// Add replicator data as raw bytes (no addBitStream wrapper)
			for (size_t i = 0; i < repPacked.getDataLength(); ++i)
				pkt.addInt(repPacked.getData()[i], 8);

			ENetPacket* packet = enet_packet_create(
				pkt.getData(), pkt.getDataLength(), 0);
			enet_host_broadcast(m_host, 0, packet);
		}
	}

	enet_host_flush(m_host);
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
	
	for (auto& pair : m_peerMap) {
		if (pair.second) {
			enet_peer_disconnect(pair.second, 0);
		}
	}
	m_peerMap.clear();
	m_addressMap.clear();
	
	(void)data;
}

void ZCom_Control::ZCom_Disconnect(uint32_t id, ZCom_BitStream* data)
{
	ENetPeer* peer = findPeer(id);
	if (peer) {
		enet_peer_disconnect(peer, 0);
		m_peerMap.erase(id);
		m_addressMap.erase(id);
	}
	(void)data;
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
		case eZCom_Reliable:
		case eZCom_ReliableOrdered:
			flags = ENET_PACKET_FLAG_RELIABLE;
			break;
		case eZCom_Unreliable:
		case eZCom_ReliableUnordered:
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

void ZCom_Control::sendToAll(eZCom_SendMode mode, ZCom_BitStream* stream)
{
	if (!m_host || !stream) return;
	
	enet_uint32 flags = 0;
	switch (mode) {
		case eZCom_Reliable:
		case eZCom_ReliableOrdered:
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

bool ZCom_Control::registerNode(ZCom_Node* node)
{
	if (!node) return false;
	node->setNodeID(m_nextNodeID++);
	node->setControl(this);
	m_nodes.push_back(node);
	return true;
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
	ZCom_ConnStats stats = {0};
	ENetPeer* peer = findPeer(id);
	if (peer) {
		stats.avg_ping = peer->roundTripTime;
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
			// (read directly without consuming from the bitstream)
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
				int eventDataLen = streamData.getInt(16); // consume addBitStream length prefix
				// Remaining data is the event payload (without the addBitStream length prefix)
				dispatchNodeEvent(nodeID, eZCom_EventUser, eZCom_RoleProxy, connID, &streamData);

				// Server forwarding: re-broadcast to all other connected peers
				if (m_peerMap.size() > 1) {
					for (auto it = m_peerMap.begin(); it != m_peerMap.end(); ++it) {
						if (it->first != connID) {
							ENetPacket* fwdPkt = enet_packet_create(
								event.packet->data, event.packet->dataLength,
								ENET_PACKET_FLAG_RELIABLE);
							enet_peer_send(it->second, 0, fwdPkt);
						}
					}
				}

				enet_packet_destroy(event.packet);
				break;
			}

			// Handle replicator state updates
			if (msgType == MSG_REPLICATORS) {
				streamData.getInt(8); // consume msgType
				uint32_t repNodeID = streamData.getInt(16);

				// Find the matching local node by networkID
				ZCom_Node* repNode = nullptr;
				for (auto* node : m_nodes) {
					if (node->getNetworkID() == repNodeID) {
						repNode = node;
						break;
					}
				}

				if (repNode) {
					// Replicator data starts at byte offset 3 (msgType=1 + nodeID=2)
					size_t offset = 3;
					ZCom_BitStream repData(
						event.packet->data + offset,
						event.packet->dataLength - offset
					);
					repNode->unpackAllReplicators(&repData, true, 0);
				}

				enet_packet_destroy(event.packet);
				break;
			}
			
			// Handle player created (server -> client)
			if (msgType == MSG_PLAYER_CREATED) {
				streamData.getInt(8); // consume msgType
				uint32_t wormNodeID = streamData.getInt(16);
				uint32_t playerNodeID = streamData.getInt(16);
				const char* name = streamData.getStringStatic();
				if (!name) name = "";
				int colour = streamData.getInt(24);
				int team = streamData.getSignedInt(8);
				ZCom_cbPlayerCreated(connID, name, colour, team, wormNodeID, playerNodeID);
				enet_packet_destroy(event.packet);
				break;
			}
			
			// Handle server nodes info (server -> client)
			if (msgType == MSG_SERVER_NODES) {
				streamData.getInt(8); // consume msgType
				uint32_t wormNodeID = streamData.getInt(16);
				uint32_t playerNodeID = streamData.getInt(16);
				const char* name = streamData.getStringStatic();
				if (!name) name = "";
				int colour = streamData.getInt(24);
				int team = streamData.getSignedInt(8);
				ZCom_cbServerNodes(connID, name, colour, team, wormNodeID, playerNodeID);
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
			m_peerMap.erase(connID);
			m_addressMap.erase(connID);
			m_waitingForReply.erase(connID);
			
			ZCom_BitStream reasonData;
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
	std::cout << "[DEBUG] dispatchNodeEvent: nodeID=" << nodeID << " found=" << found << " totalNodes=" << m_nodes.size() << std::endl;
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
		g_currentControl->setHost(host);
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