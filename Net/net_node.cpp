#include "net_node.h"
#include "net_bitstream.h"
#include "net_control.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

ZCom_Node::ZCom_Node()
	: m_nodeID(0), m_classID(0), m_ownerID(0)
	, m_role(0), m_eventNotification(false)
	, m_authority(false), m_control(nullptr)
	, m_announceData(nullptr)
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
	m_autoReplications.push_back(entry);
}

void ZCom_Node::setInterceptID(int id)
{
	(void)id;
}

void ZCom_Node::setReplicationInterceptor(void* interceptor)
{
	(void)interceptor;
}

void ZCom_Node::setEventNotification(bool init, bool remove)
{
	m_eventNotification = init;
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
	return m_control ? m_control->registerNode(this) : false;
}

bool ZCom_Node::registerNodeUnique(uint32_t classID, int role, void* control)
{
	m_classID = classID;
	m_role = role;
	m_control = static_cast<ZCom_Control*>(control);
	return m_control ? m_control->registerNode(this) : false;
}

bool ZCom_Node::registerRequestedNode(uint32_t classID, void* control)
{
	m_classID = classID;
	m_control = static_cast<ZCom_Control*>(control);
	return m_control ? m_control->registerNode(this) : false;
}

void ZCom_Node::applyForZoidLevel(int level)
{
	(void)level;
}

void ZCom_Node::setOwner(uint32_t id, bool auth)
{
	m_ownerID = id;
	m_authority = auth;
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
	(void)rules;
	if (!m_control || !stream) return;
	std::cout << "[DEBUG] ZCom_Node::sendEvent nodeID=" << m_nodeID << " control=" << (void*)m_control << " mode=" << mode << std::endl;
	// Prefix the event with an "event" tag so the receiver knows it's a node event
	ZCom_BitStream pkt;
	pkt.addInt(0, 8); // 0 = node event
	pkt.addInt(m_nodeID, 16);
	pkt.addBitStream(stream);
	m_control->sendToAll(static_cast<eZCom_SendMode>(mode), &pkt);
}

void ZCom_Node::sendEventDirect(int mode, ZCom_BitStream* stream, uint32_t id)
{
	if (!m_control || !stream) return;
	ZCom_BitStream pkt;
	pkt.addInt(0, 8); // 0 = node event
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
	// Auto replications
	for (auto& entry : m_autoReplications) {
		bool changed = false;
		if (entry.type == ReplicationEntry::TypeInt && entry.ptr) {
			int32_t val = *static_cast<int32_t*>(entry.ptr);
			if (val != entry.oldInt) {
				changed = true;
				entry.oldInt = val;
			}
		} else if (entry.type == ReplicationEntry::TypeFloat && entry.ptr) {
			float val = *static_cast<float*>(entry.ptr);
			if (val != entry.oldFloat) {
				changed = true;
				entry.oldFloat = val;
			}
		}
		stream->addBool(changed);
		if (changed) {
			if (entry.type == ReplicationEntry::TypeInt) {
				if (entry.sign)
					stream->addSignedInt(*static_cast<int32_t*>(entry.ptr), entry.bits);
				else
					stream->addInt(*static_cast<int32_t*>(entry.ptr), entry.bits);
			} else if (entry.type == ReplicationEntry::TypeFloat) {
				stream->addFloat(*static_cast<float*>(entry.ptr), entry.bits);
			}
		}
	}
}

void ZCom_Node::unpackAllReplicators(ZCom_BitStream* stream, bool store, uint32_t estimatedTimeSent)
{
	for (auto* rep : m_replicators) {
		bool hasUpdate = stream->getBool();
		if (hasUpdate) {
			rep->unpackData(stream, store, estimatedTimeSent);
		}
	}
	for (auto& entry : m_autoReplications) {
		bool changed = stream->getBool();
		if (changed && entry.ptr) {
			if (entry.type == ReplicationEntry::TypeInt) {
				if (entry.sign)
					*static_cast<int32_t*>(entry.ptr) = stream->getSignedInt(entry.bits);
				else
					*static_cast<int32_t*>(entry.ptr) = stream->getInt(entry.bits);
			} else if (entry.type == ReplicationEntry::TypeFloat) {
				*static_cast<float*>(entry.ptr) = stream->getFloat(entry.bits);
			}
		}
	}
}