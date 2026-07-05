#include "net_node.h"
#include "net_bitstream.h"
#include "net_control.h"
#include <thread>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <stdexcept>

void ZoidCom::Sleep(int ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

zU32 ZoidCom::getTime()
{
	return static_cast<zU32>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}

ZCom_Node::ZCom_Node()
	: m_nodeID(0), m_classID(0), m_ownerID(0)
	, m_role(0), m_eventNotification(false)
	, m_eventNotificationRemove(false), m_authority(false)
	, m_control(nullptr), m_announceData(nullptr)
	, m_userData(nullptr), m_eventInterceptor(nullptr)
	, m_replicationInterceptor(nullptr)
	, m_isUnique(false), m_isPrivate(false), m_zoidLevel(0), m_interceptID(-1)
	, m_updatePriority(0), m_defaultRelevance(1.0f), m_relevantConnectionCount(0)
{
}

ZCom_Node::~ZCom_Node()
{
	if (m_control)
		m_control->removeNode(this);
	delete m_announceData;
	// Caller owns replicators — do not delete
}

void ZCom_Node::beginReplicationSetup(int level)
{
	(void)level;
}

void ZCom_Node::endReplicationSetup()
{
}

void ZCom_Node::registerReplicator(void* replicator)
{
	(void)replicator;
}

void ZCom_Node::addReplicator(ZCom_Replicator* replicator, bool flag)
{
	(void)flag;
	replicator->m_flags |= ZCOM_REPLICATOR_INITIALIZED;
	m_replicators.push_back(replicator);
}

void ZCom_Node::addReplicationInt(zS32* val, zU8 bits, bool sign, zU8 flags, zU8 rules, zS16 mindelay, zS16 maxdelay)
{
	ReplicationEntry entry;
	entry.type = ReplicationEntry::TypeInt;
	entry.ptr = val;
	entry.bits = bits;
	entry.sign = sign;
	entry.flags = flags;
	entry.rule = rules;
	entry.oldInt = val ? *val : 0;
	entry.initial = true;
	entry.minDelay = mindelay;
	entry.maxDelay = maxdelay;
	m_autoReplications.push_back(entry);
}

void ZCom_Node::addReplicationFloat(zFloat* val, zU8 mantissa_bits, zU8 flags, zU8 rules, zS16 mindelay, zS16 maxdelay)
{
	ReplicationEntry entry;
	entry.type = ReplicationEntry::TypeFloat;
	entry.ptr = val;
	entry.bits = mantissa_bits;
	entry.sign = false;
	entry.flags = flags;
	entry.rule = rules;
	entry.oldFloat = val ? *val : 0.0f;
	entry.initial = true;
	entry.minDelay = mindelay;
	entry.maxDelay = maxdelay;
	m_autoReplications.push_back(entry);
}

void ZCom_Node::addReplicationBool(bool* val, zU8 flags, zU8 rules, zS16 mindelay, zS16 maxdelay)
{
	ReplicationEntry entry;
	entry.type = ReplicationEntry::TypeInt; // reuse int type for bool (1 bit)
	entry.ptr = val;
	entry.bits = 1;
	entry.sign = false;
	entry.flags = flags;
	entry.rule = rules;
	entry.oldInt = val ? (*val ? 1 : 0) : 0;
	entry.initial = true;
	entry.minDelay = mindelay;
	entry.maxDelay = maxdelay;
	m_autoReplications.push_back(entry);
}

void ZCom_Node::setInterceptID(ZCom_InterceptID id)
{
	m_interceptID = id;
}

void ZCom_Node::setEventNotification(bool init, bool remove)
{
	m_eventNotification = init;
	m_eventNotificationRemove = remove;
}

void ZCom_Node::setAnnounceData(ZCom_BitStream* data)
{
	delete m_announceData;
	m_announceData = data ? data->Duplicate() : nullptr;
}

bool ZCom_Node::registerNodeDynamic(uint32_t classID, void* control)
{
	m_classID = classID;
	m_control = static_cast<ZCom_Control*>(control);
	// Per Zoidcom semantics: when registered outside cbNodeRequest_Dynamic
	// this is an authority node; when registered inside the callback (client
	// side) the control assigns the announced role (Proxy/Owner).
	if (m_control && !m_control->isRequestActive())
		m_role = eZCom_RoleAuthority;
	if (m_control)
		m_control->applyRequestRole(this);
	if (m_control)
		m_control->applyRequestNodeID(this);
	if (m_control && m_nodeID > 0)
		return m_control->registerExistingNode(this);
	return m_control ? m_control->registerNode(this) : false;
}

bool ZCom_Node::registerNodeUnique(uint32_t classID, eZCom_NodeRole role, void* control)
{
	m_classID = classID;
	m_role = role;
	m_control = static_cast<ZCom_Control*>(control);
	m_isUnique = true;
	return m_control ? m_control->registerNode(this) : false;
}

void ZCom_Node::unregisterNode()
{
	if (m_control) {
		m_control->removeNode(this);
		m_control = nullptr;
	}
}

bool ZCom_Node::registerRequestedNode(uint32_t classID, void* control)
{
	m_classID = classID;
	m_control = static_cast<ZCom_Control*>(control);
	// Requested nodes are always client-side; the control assigns the role
	// announced by the server (Proxy/Owner) via the request context.
	if (m_control)
		m_control->applyRequestRole(this);
	// Requested nodes already have a server-assigned ID — auto-assign from request context
	if (m_control)
		m_control->applyRequestNodeID(this);
	// Requested nodes already have a server-assigned ID — don't auto-assign
	return m_control ? m_control->registerExistingNode(this) : false;
}

void ZCom_Node::applyForZoidLevel(int level)
{
	m_zoidLevel = level;
}

void ZCom_Node::setOwner(ZCom_ConnID id, bool auth)
{
	m_ownerID = id;
	m_authority = auth;
	// If already registered, re-announce with owner-aware roles
	if (m_control && m_nodeID > 0 && !m_isUnique) {
		m_control->clearAnnouncedNode(m_nodeID);
		m_control->announceNodeWithOwner(this);
	}
}

void ZCom_Node::pushEvent(eZCom_Event type, eZCom_NodeRole role, uint32_t connID, ZCom_BitStream* data)
{
	NodeEvent ev;
	ev.type = type;
	ev.role = role;
	ev.connID = connID;
	ev.data = data ? data->Duplicate() : nullptr;
	m_eventQueue.push_back(ev);
}

void ZCom_Node::sendEvent(eZCom_SendMode mode, zU8 rules, ZCom_BitStream* stream)
{
	if (!m_control || !stream) return;

	// Determine if we are allowed to send based on local role
	if ((rules & (ZCOM_REPRULE_AUTH_2_PROXY | ZCOM_REPRULE_AUTH_2_OWNER)))
		if (m_role != eZCom_RoleAuthority)
			return; // Only authority can send AUTH_* rules

	if ((rules & ZCOM_REPRULE_OWNER_2_AUTH))
		if (m_role != eZCom_RoleOwner)
			return; // Only owner can send OWNER_2_AUTH rules

	ZCom_BitStream pkt;
	pkt.addInt(MSG_NODE_EVENT, 8);
	pkt.addInt(m_nodeID, 16);
	pkt.addBitStream(stream);

	// Route to peers matching the rule (rule==0 broadcasts to all, preserving
	// legacy semantics). Phase D2.
	m_control->routeNodeEvent(m_nodeID, static_cast<eZCom_NodeRole>(m_role), rules, mode, pkt);
}

void ZCom_Node::sendEventDirect(eZCom_SendMode mode, ZCom_BitStream* stream, ZCom_ConnID id)
{
	if (!m_control || !stream) return;
	ZCom_BitStream pkt;
	pkt.addInt(MSG_NODE_EVENT, 8);
	pkt.addInt(m_nodeID, 16);
	pkt.addBitStream(stream);
	m_control->ZCom_sendData(id, &pkt, mode);
}

bool ZCom_Node::checkEventWaiting()
{
	return !m_eventQueue.empty();
}

ZCom_BitStream* ZCom_Node::getNextEvent(eZCom_Event* type, eZCom_NodeRole* role, ZCom_ConnID* connid, zU32* estimated_time_sent)
{
	if (m_eventQueue.empty()) return nullptr;
	NodeEvent& ev = m_eventQueue.front();
	if (type) *type = ev.type;
	if (role) *role = ev.role;
	if (connid) *connid = ev.connID;
 	if (estimated_time_sent) *estimated_time_sent = 0;
 	ZCom_BitStream* data = ev.data;
 	m_eventQueue.pop_front();
 	return data;
 }

// File transfer (Phase E) — delegate to the owning ZCom_Control.
ZCom_FileTransID ZCom_Node::sendFile(const char* _path, const char* _pathtosend,
	ZCom_ConnID _destconn, ZCom_BitStream* _data, zFloat _aggressivenes)
{
	if (!m_control) return ZCom_Invalid_ID;
	return m_control->ZCom_sendFile(this, _path, _pathtosend, _destconn, _data, _aggressivenes);
}

void ZCom_Node::acceptFile(ZCom_ConnID _src_id, ZCom_FileTransID _ftrans_id,
	const char* _path, bool _accept)
{
	if (m_control) m_control->ZCom_acceptFile(this, _src_id, _ftrans_id, _path, _accept);
}

const ZCom_FileTransInfo& ZCom_Node::getFileInfo(ZCom_ConnID _conn_id,
	ZCom_FileTransID _ftrans_id) const
{
	if (!m_control) { static const ZCom_FileTransInfo inv{}; return inv; }
	return m_control->ZCom_getFileInfo(_conn_id, _ftrans_id);
}

void ZCom_Node::packAllReplicators(ZCom_BitStream* stream)
{
	for (auto* rep : m_replicators) {
		if (rep->checkState()) {
			stream->addInt(1, 1); // has update
			rep->packData(stream);
		} else {
			stream->addInt(0, 1); // no update
		}
	}
	// Auto replications — write initial state on first pack
	for (auto& entry : m_autoReplications) {
		bool changed = false;
		if (entry.type == ReplicationEntry::TypeInt && entry.ptr) {
			int32_t val = *static_cast<int32_t*>(entry.ptr);
			if (entry.initial || val != static_cast<int32_t>(entry.oldInt)) {
				changed = true;
				entry.oldInt = val;
				entry.initial = false;
			}
		} else if (entry.type == ReplicationEntry::TypeFloat && entry.ptr) {
			float val = *static_cast<float*>(entry.ptr);
			if (entry.initial || val != entry.oldFloat) {
				changed = true;
				entry.oldFloat = val;
				entry.initial = false;
			}
		} else if (entry.type == ReplicationEntry::TypeBool && entry.ptr) {
			bool val = *static_cast<bool*>(entry.ptr);
			bool old = entry.initial ? false : (entry.oldInt != 0); // oldInt stores bool value
			if (entry.initial || val != old) {
				changed = true;
				entry.oldInt = val ? 1 : 0;
				entry.initial = false;
			}
		}
		if (changed) {
			stream->addInt(1, 1);
			if (entry.type == ReplicationEntry::TypeInt) {
				stream->addInt(*static_cast<int32_t*>(entry.ptr), entry.bits);
			} else if (entry.type == ReplicationEntry::TypeFloat) {
				stream->addFloat(*static_cast<float*>(entry.ptr), entry.bits);
			} else {
				stream->addInt(*static_cast<bool*>(entry.ptr) ? 1 : 0, 1);
			}
		} else {
			stream->addInt(0, 1);
		}
	}
}

void ZCom_Node::packReplicatorsForRouting(std::vector<PackedReplicator>& out)
{
	out.clear();
	out.reserve(m_replicators.size() + m_autoReplications.size());

	// Explicit replicators (ZCom_Replicator subclasses).
	for (auto* rep : m_replicators) {
		PackedReplicator p;
		p.rule = rep->getSetup()->getRules();
		if (rep->checkState()) {
			p.hasUpdate = true;
			rep->packData(&p.data); // clears dirty
		} else {
			p.hasUpdate = false;
		}
		out.push_back(std::move(p));
	}

	// Auto-replications (inline ReplicationEntry).
	for (auto& entry : m_autoReplications) {
		PackedReplicator p;
		p.rule = entry.rule;
		bool changed = false;
		if (entry.type == ReplicationEntry::TypeInt && entry.ptr) {
			int32_t val = *static_cast<int32_t*>(entry.ptr);
			if (entry.initial || val != static_cast<int32_t>(entry.oldInt)) {
				changed = true;
				entry.oldInt = val;
				entry.initial = false;
			}
		} else if (entry.type == ReplicationEntry::TypeFloat && entry.ptr) {
			float val = *static_cast<float*>(entry.ptr);
			if (entry.initial || val != entry.oldFloat) {
				changed = true;
				entry.oldFloat = val;
				entry.initial = false;
			}
		} else if (entry.type == ReplicationEntry::TypeBool && entry.ptr) {
			bool val = *static_cast<bool*>(entry.ptr);
			bool old = entry.initial ? false : (entry.oldInt != 0);
			if (entry.initial || val != old) {
				changed = true;
				entry.oldInt = val ? 1 : 0;
				entry.initial = false;
			}
		}
		if (changed) {
			p.hasUpdate = true;
			if (entry.type == ReplicationEntry::TypeInt)
				p.data.addInt(*static_cast<int32_t*>(entry.ptr), entry.bits);
			else if (entry.type == ReplicationEntry::TypeFloat)
				p.data.addFloat(*static_cast<float*>(entry.ptr), entry.bits);
			else
				p.data.addInt(*static_cast<bool*>(entry.ptr) ? 1 : 0, 1);
		} else {
			p.hasUpdate = false;
		}
		out.push_back(std::move(p));
	}
}

void ZCom_Node::unpackAllReplicators(ZCom_BitStream* stream, bool store, uint32_t estimatedTimeSent)
{
	// Call inPreUpdate before processing updates
	if (m_replicationInterceptor && !m_replicationInterceptor->inPreUpdate(this, 0, eZCom_RoleAuthority))
		return;

	for (auto* rep : m_replicators) {
		bool hasUpdate = stream->getInt(1) != 0;
		if (hasUpdate) {
			// Provide a peek stream positioned at this replicator's data so that
			// an interceptor's peekData() call can read the incoming value without
			// disturbing the main stream's read position.
			ZCom_BitStream::BitPos savedRead;
			stream->saveReadState(savedRead);
			bool proceed = true;
			if (m_replicationInterceptor) {
				rep->setPeekStream(stream);
				proceed = m_replicationInterceptor->inPreUpdateItem(this, 0, eZCom_RoleAuthority, rep, estimatedTimeSent);
				rep->clearPeekData();
				rep->setPeekStream(nullptr);
				// peekData() may have advanced the read position; restore it so
				// unpackData() reads from the same location.
				stream->restoreReadState(savedRead);
			}
			if (proceed)
				rep->unpackData(stream, store, estimatedTimeSent);
			else
				rep->unpackData(stream, false, estimatedTimeSent);
		}
	}
	for (auto& entry : m_autoReplications) {
		bool hasUpdate = stream->getInt(1) != 0;
		if (hasUpdate && store && entry.ptr) {
			// For intercepted auto-replications, decode the incoming value into a
			// temporary and expose it via peekData() so the interceptor can read it
			// (matching the Zoidcom contract used by BasePlayer/NetWorm).
			if (m_replicationInterceptor && (entry.flags & ZCOM_REPFLAG_INTERCEPT) && m_interceptID >= 0) {
				ZCom_ReplicatorSetup tempSetup(entry.flags, entry.rule, m_interceptID);
				ZCom_ReplicatorBasic tempRep(&tempSetup);

				int32_t decodedInt = 0;
				float decodedFloat = 0.0f;
				if (entry.type == ReplicationEntry::TypeInt) {
					decodedInt = static_cast<int32_t>(stream->getInt(entry.bits));
					tempRep.peekDataStore(&decodedInt);
				} else if (entry.type == ReplicationEntry::TypeBool) {
					int decodedInt = stream->getInt(1);
					tempRep.peekDataStore(&decodedInt);
				} else {
					decodedFloat = stream->getFloat(entry.bits);
					tempRep.peekDataStore(&decodedFloat);
				}
				bool accept = m_replicationInterceptor->inPreUpdateItem(this, 0, eZCom_RoleAuthority, &tempRep, estimatedTimeSent);
				tempRep.peekDataStore(nullptr);
				if (!accept)
					continue;
				if (entry.type == ReplicationEntry::TypeInt)
					*static_cast<int32_t*>(entry.ptr) = decodedInt;
				else if (entry.type == ReplicationEntry::TypeBool)
					*static_cast<bool*>(entry.ptr) = decodedInt != 0;
				else
					*static_cast<float*>(entry.ptr) = decodedFloat;
			} else {
				if (entry.type == ReplicationEntry::TypeInt) {
					*static_cast<int32_t*>(entry.ptr) = static_cast<int32_t>(stream->getInt(entry.bits));
				} else if (entry.type == ReplicationEntry::TypeBool) {
					*static_cast<bool*>(entry.ptr) = stream->getInt(1) != 0;
				} else {
					*static_cast<float*>(entry.ptr) = stream->getFloat(entry.bits);
				}
			}
		} else if (hasUpdate) {
			if (entry.type == ReplicationEntry::TypeInt) {
				stream->getInt(entry.bits);
			} else if (entry.type == ReplicationEntry::TypeBool) {
				stream->getInt(1);
			} else {
				stream->getFloat(entry.bits);
			}
		}
	}
}
