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

ZCom_Node::ZCom_Node()
	: m_nodeID(0), m_classID(0), m_ownerID(0)
	, m_role(0), m_eventNotification(false)
	, m_eventNotificationRemove(false), m_authority(false)
	, m_control(nullptr), m_announceData(nullptr)
	, m_userData(nullptr), m_eventInterceptor(nullptr)
	, m_replicationInterceptor(nullptr)
	, m_isUnique(false), m_zoidLevel(0), m_interceptID(-1)
{
}

ZCom_Node::~ZCom_Node()
{
	if (m_control)
		m_control->removeNode(this);
	delete m_announceData;
	for (auto* r : m_replicators)
		delete r;
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

void ZCom_Node::addReplicationInt(int32_t* val, int bits, bool sign, uint32_t flags, uint32_t rule, uint32_t id)
{
	(void)id;
	ReplicationEntry entry;
	entry.type = ReplicationEntry::TypeInt;
	entry.ptr = val;
	entry.bits = bits;
	entry.sign = sign;
	entry.flags = flags;
	entry.rule = rule;
	entry.oldInt = val ? *val : 0;
	entry.initial = true;
	m_autoReplications.push_back(entry);
}

void ZCom_Node::addReplicationFloat(float* val, int bits, uint32_t flags, uint32_t rule)
{
	ReplicationEntry entry;
	entry.type = ReplicationEntry::TypeFloat;
	entry.ptr = val;
	entry.bits = bits;
	entry.sign = false;
	entry.flags = flags;
	entry.rule = rule;
	entry.oldFloat = val ? *val : 0.0f;
	entry.initial = true;
	m_autoReplications.push_back(entry);
}

void ZCom_Node::addReplicationBool(bool* val, uint32_t flags, uint32_t rule)
{
	ReplicationEntry entry;
	entry.type = ReplicationEntry::TypeInt; // reuse int type for bool (1 bit)
	entry.ptr = val;
	entry.bits = 1;
	entry.sign = false;
	entry.flags = flags;
	entry.rule = rule;
	entry.oldInt = val ? (*val ? 1 : 0) : 0;
	entry.initial = true;
	m_autoReplications.push_back(entry);
}

void ZCom_Node::setInterceptID(int id)
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
	if (m_control && m_nodeID > 0)
		return m_control->registerExistingNode(this);
	return m_control ? m_control->registerNode(this) : false;
}

bool ZCom_Node::registerNodeUnique(uint32_t classID, int role, void* control)
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
	// Requested nodes already have a server-assigned ID — don't auto-assign
	return m_control ? m_control->registerExistingNode(this) : false;
}

void ZCom_Node::applyForZoidLevel(int level)
{
	m_zoidLevel = level;
}

void ZCom_Node::setOwner(uint32_t id, bool auth)
{
	m_ownerID = id;
	m_authority = auth;
	// If already registered, re-announce with owner-aware roles
	if (m_control && m_nodeID > 0 && !m_isUnique) {
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

void ZCom_Node::sendEvent(int mode, uint32_t rules, ZCom_BitStream* stream)
{
	if (!m_control || !stream) return;

	// Determine if we are allowed to send based on local role
	if ((rules & (ZCOM_REPRULE_AUTH_2_PROXY | ZCOM_REPRULE_AUTH_2_OWNER)))
		if (m_role != eZCom_RoleAuthority && m_role != ZCOM_ROLE_AUTHORITY)
			return; // Only authority can send AUTH_* rules

	if ((rules & ZCOM_REPRULE_OWNER_2_AUTH))
		if (m_role != eZCom_RoleOwner)
			return; // Only owner can send OWNER_2_AUTH rules

	ZCom_BitStream pkt;
	pkt.addInt(MSG_NODE_EVENT, 8);
	pkt.addInt(m_nodeID, 16);
	pkt.addBitStream(stream);

	m_control->sendToAll(static_cast<int>(mode), &pkt);
}

void ZCom_Node::sendEventDirect(int mode, ZCom_BitStream* stream, uint32_t id)
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

ZCom_BitStream* ZCom_Node::getNextEvent(eZCom_Event* type, eZCom_NodeRole* role, uint32_t* id)
{
	if (m_eventQueue.empty()) return nullptr;
	NodeEvent& ev = m_eventQueue.front();
	if (type) *type = ev.type;
	if (role) *role = ev.role;
	if (id) *id = ev.connID;
	ZCom_BitStream* data = ev.data;
	m_eventQueue.pop_front();
	return data;
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
		}
		if (changed) {
			stream->addInt(1, 1);
			if (entry.type == ReplicationEntry::TypeInt) {
				stream->addInt(*static_cast<int32_t*>(entry.ptr), entry.bits);
			} else {
				stream->addFloat(*static_cast<float*>(entry.ptr), entry.bits);
			}
		} else {
			stream->addInt(0, 1);
		}
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
			// Call inPreUpdateItem interceptor
			if (!m_replicationInterceptor || m_replicationInterceptor->inPreUpdateItem(this, 0, eZCom_RoleAuthority, rep, estimatedTimeSent)) {
				rep->unpackData(stream, store, estimatedTimeSent);
			} else {
				rep->unpackData(stream, false, estimatedTimeSent);
			}
		}
	}
	for (auto& entry : m_autoReplications) {
		bool hasUpdate = stream->getInt(1) != 0;
		if (hasUpdate && store && entry.ptr) {
			// Call interceptor for intercepted auto-replications
			if (m_replicationInterceptor && (entry.flags & ZCOM_REPFLAG_INTERCEPT) && m_interceptID >= 0) {
				ZCom_ReplicatorSetup tempSetup(entry.flags, entry.rule, m_interceptID);
				ZCom_ReplicatorBasic tempRep(&tempSetup);
				tempRep.peekDataStore(entry.ptr);
				if (!m_replicationInterceptor->inPreUpdateItem(this, 0, eZCom_RoleAuthority, &tempRep, estimatedTimeSent)) {
					stream->skipInt(entry.bits);
					continue;
				}
			}
			if (entry.type == ReplicationEntry::TypeInt) {
				*static_cast<int32_t*>(entry.ptr) = static_cast<int32_t>(stream->getInt(entry.bits));
			} else {
				*static_cast<float*>(entry.ptr) = stream->getFloat(entry.bits);
			}
		} else if (hasUpdate) {
			if (entry.type == ReplicationEntry::TypeInt) {
				stream->getInt(entry.bits);
			} else {
				stream->getFloat(entry.bits);
			}
		}
	}
}
