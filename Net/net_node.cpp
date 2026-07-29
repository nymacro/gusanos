#include "net_node.h"
#include "net_bitstream.h"
#include "net_control.h"
#include <thread>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <cstring>

void ZoidCom::Sleep(int ms) {
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

zU32 ZoidCom::getTime() {
	return static_cast<zU32>(
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
			.count());
}

// C6: default connection auto-close timeout (matches Zoidcom reference default).
zU32 ZoidCom::s_connectionTimeout = 20000;

ZCom_Node::ZCom_Node()
	: m_nodeID(0), m_classID(0), m_ownerID(0), m_role(0), m_isUnique(false), m_isPrivate(false),
	  m_eventNotification(false), m_eventNotificationRemove(false), m_authority(false), m_control(nullptr),
	  m_announceData(nullptr), m_userData(nullptr), m_eventInterceptor(nullptr), m_updatePriority(0),
	  m_defaultRelevance(1.0f), m_relevantConnectionCount(0), m_zoidLevel(0), m_interceptID(-1),
	  m_replicationInterceptor(nullptr) {}

ZCom_Node::~ZCom_Node() {
	// Delegate to unregisterNode so m_control is nulled after removal; this
	// guards against re-entrant destruction and would keep this path safe if
	// the owning ZCom_Control were ever destroyed before the node.
	unregisterNode();
	// m_announceData (unique_ptr), m_eventQueue (unique_ptr<ZCom_BitStream>
	// entries) and m_replicators (unique_ptr vector) all auto-drain here — the
	// node now owns its replicators, fixing the pending-event BitStream and
	// replicator leaks on mid-tick node destruction.
}

void ZCom_Node::beginReplicationSetup(int level) {
	(void)level;
}

void ZCom_Node::endReplicationSetup() {}

void ZCom_Node::registerReplicator(void *replicator) {
	(void)replicator;
}

void ZCom_Node::addReplicator(std::unique_ptr<ZCom_Replicator> replicator, bool flag) {
	(void)flag;
	replicator->m_flags |= ZCOM_REPLICATOR_INITIALIZED;
	m_replicators.push_back(std::move(replicator));
}

void ZCom_Node::addReplicationInt(zS32 *val, zU8 bits, bool sign, zU8 flags, zU8 rules, zS16 mindelay, zS16 maxdelay) {
	auto rep = std::make_unique<AutoReplicatorInt>(val, bits, sign);
	rep->flags = flags;
	rep->rule = rules;
	rep->minDelay = mindelay;
	rep->maxDelay = maxdelay;
	m_autoReplications.push_back(std::move(rep));
}

void ZCom_Node::addReplicationFloat(zFloat *val, zU8 mantissa_bits, zU8 flags, zU8 rules, zS16 mindelay,
									zS16 maxdelay) {
	auto rep = std::make_unique<AutoReplicatorFloat>(val, mantissa_bits);
	rep->flags = flags;
	rep->rule = rules;
	rep->minDelay = mindelay;
	rep->maxDelay = maxdelay;
	m_autoReplications.push_back(std::move(rep));
}

void ZCom_Node::addReplicationBool(bool *val, zU8 flags, zU8 rules, zS16 mindelay, zS16 maxdelay) {
	auto rep = std::make_unique<AutoReplicatorBool>(val);
	rep->flags = flags;
	rep->rule = rules;
	rep->minDelay = mindelay;
	rep->maxDelay = maxdelay;
	m_autoReplications.push_back(std::move(rep));
}

void ZCom_Node::setInterceptID(ZCom_InterceptID id) {
	m_interceptID = id;
}

void ZCom_Node::setEventNotification(bool init, bool remove) {
	m_eventNotification = init;
	m_eventNotificationRemove = remove;
}

void ZCom_Node::setAnnounceData(std::unique_ptr<ZCom_BitStream> data) {
	// Ownership of `data` transfers to the node (no internal Duplicate).
	m_announceData = std::move(data);
}

ZCom_BitStream *ZCom_Node::buildAnnounceData(uint32_t to, eZCom_NodeRole remoteRole) {
	// Fire the interceptor so it can populate announce data (e.g. Particle's
	// outPreReplicateNode calls setAnnounceData). The game code's interceptors
	// ignore _to/_remote_role, but we pass them for Zoidcom spec compliance.
	if (m_replicationInterceptor)
		m_replicationInterceptor->outPreReplicateNode(this, to, remoteRole);
	return m_announceData.get(); // may be null; callers already handle that
}

bool ZCom_Node::registerNodeDynamic(uint32_t classID, void *control) {
	m_classID = classID;
	m_control = static_cast<ZCom_Control *>(control);
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

bool ZCom_Node::registerNodeUnique(uint32_t classID, eZCom_NodeRole role, void *control) {
	m_classID = classID;
	m_role = role;
	m_control = static_cast<ZCom_Control *>(control);
	m_isUnique = true;
	return m_control ? m_control->registerNode(this) : false;
}

void ZCom_Node::unregisterNode() {
	if (m_control) {
		m_control->removeNode(this);
		m_control = nullptr;
	}
}

bool ZCom_Node::registerRequestedNode(uint32_t classID, void *control) {
	m_classID = classID;
	m_control = static_cast<ZCom_Control *>(control);
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

void ZCom_Node::applyForZoidLevel(int level) {
	m_zoidLevel = level;
}

void ZCom_Node::setOwner(ZCom_ConnID id, bool auth) {
	m_ownerID = id;
	m_authority = auth;
	// If already registered, re-announce with owner-aware roles
	if (m_control && m_nodeID > 0 && !m_isUnique) {
		m_control->clearAnnouncedNode(m_nodeID);
		m_control->announceNodeWithOwner(this);
	}
}

void ZCom_Node::pushEvent(eZCom_Event type, eZCom_NodeRole role, uint32_t connID, ZCom_BitStream *data,
						  zU32 estimatedTimeSent) {
	NodeEvent ev;
	ev.type = type;
	ev.role = role;
	ev.connID = connID;
	ev.data = data ? data->Duplicate() : nullptr;
	ev.estimatedTimeSent = estimatedTimeSent;
	m_eventQueue.push_back(std::move(ev));
}

void ZCom_Node::sendEvent(eZCom_SendMode mode, zU8 rules, ZCom_BitStream *stream) {
	if (!m_control || !stream)
		return;

	// The reprule is a recipient filter bitmask, not a sender permission
	// (Zoidcom spec: docs/zoidcom/OCommEvents.md, classZCom__Node.md). The
	// sender's role determines only which rule bits are *applicable* to it:
	// an Authority may use AUTH_2_* bits; an Owner may use OWNER_2_AUTH.
	// Bits that don't apply to this sender are masked off, and routing then
	// filters per-peer via repruleMatches().
	//
	// This is critical for combined rules like
	// AUTH_2_PROXY | OWNER_2_AUTH (used by BasePlayer::baseActionStart for
	// every action): an Owner sending such a rule must still reach the
	// Authority via the OWNER_2_AUTH leg. The previous hard-drop of the
	// whole event on any AUTH_* bit silently discarded all client action
	// events (JUMP/RESPAWN/FIRE/...), so the client could never spawn.
	uint8_t effective = rules;
	if (m_role != eZCom_RoleAuthority)
		effective &= ~(ZCOM_REPRULE_AUTH_2_PROXY | ZCOM_REPRULE_AUTH_2_OWNER);
	if (m_role != eZCom_RoleOwner)
		effective &= ~ZCOM_REPRULE_OWNER_2_AUTH;
	// rule==0 keeps the legacy broadcast behaviour (routeNodeEvent sends to
	// all peers). A non-zero rule that fully masked away has no recipients.
	if (rules != 0 && effective == 0)
		return;
	rules = effective;

	ZCom_BitStream pkt;
	pkt.addInt(MSG_NODE_EVENT, 8);
	pkt.addInt(m_nodeID, 16);
	pkt.addBitStream(stream);

	// Route to peers matching the rule (rule==0 broadcasts to all, preserving
	// legacy semantics). Phase D2.
	m_control->routeNodeEvent(m_nodeID, static_cast<eZCom_NodeRole>(m_role), rules, mode, pkt);
}

void ZCom_Node::sendEventDirect(eZCom_SendMode mode, ZCom_BitStream *stream, ZCom_ConnID id) {
	if (!m_control || !stream)
		return;
	ZCom_BitStream pkt;
	pkt.addInt(MSG_NODE_EVENT, 8);
	pkt.addInt(m_nodeID, 16);
	pkt.addBitStream(stream);
	m_control->ZCom_sendData(id, &pkt, mode);
}

bool ZCom_Node::checkEventWaiting() {
	return !m_eventQueue.empty();
}

std::unique_ptr<ZCom_BitStream> ZCom_Node::getNextEvent(eZCom_Event *type, eZCom_NodeRole *role, ZCom_ConnID *connid,
														zU32 *estimated_time_sent) {
	if (m_eventQueue.empty())
		return nullptr;
	NodeEvent &ev = m_eventQueue.front();
	if (type)
		*type = ev.type;
	if (role)
		*role = ev.role;
	if (connid)
		*connid = ev.connID;
	if (estimated_time_sent)
		*estimated_time_sent = ev.estimatedTimeSent;
	auto data = std::move(ev.data);
	m_eventQueue.pop_front();
	return data;
}

// File transfer (Phase E) — delegate to the owning ZCom_Control.
ZCom_FileTransID ZCom_Node::sendFile(const char *_path, const char *_pathtosend, ZCom_ConnID _destconn,
									 ZCom_BitStream *_data, zFloat _aggressivenes) {
	if (!m_control)
		return ZCom_Invalid_ID;
	return m_control->ZCom_sendFile(this, _path, _pathtosend, _destconn, _data, _aggressivenes);
}

void ZCom_Node::acceptFile(ZCom_ConnID _src_id, ZCom_FileTransID _ftrans_id, const char *_path, bool _accept) {
	if (m_control)
		m_control->ZCom_acceptFile(this, _src_id, _ftrans_id, _path, _accept);
}

const ZCom_FileTransInfo &ZCom_Node::getFileInfo(ZCom_ConnID _conn_id, ZCom_FileTransID _ftrans_id) const {
	if (!m_control) {
		static const ZCom_FileTransInfo inv{};
		return inv;
	}
	return m_control->ZCom_getFileInfo(_conn_id, _ftrans_id);
}

void ZCom_Node::packAllReplicators(ZCom_BitStream *stream) {
	for (auto &rep : m_replicators) {
		if (rep->checkState()) {
			stream->addInt(1, 1); // has update
			rep->packData(stream);
		} else {
			stream->addInt(0, 1); // no update
		}
	}
	// Auto replications — write initial state on first pack. The has-update
	// bit is written first (matching the original wire order), then the value
	// bits only when changed.
	for (auto &entry : m_autoReplications) {
		bool changed = entry->detect(/*force=*/false);
		stream->addInt(changed ? 1 : 0, 1);
		if (changed)
			entry->emit(*stream);
	}
}

void ZCom_Node::packReplicatorsForRouting(std::vector<PackedReplicator> &out) {
	out.clear();
	out.reserve(m_replicators.size() + m_autoReplications.size());

	// When set (typically after announcing the node to a freshly connected
	// peer), force every replicator to be packed regardless of its dirty flag
	// so the new peer receives the current state instead of nothing. The flag
	// is cleared after the pass so subsequent packs resume normal dirty-only
	// behaviour.
	const bool forceAll = m_forceReplicationUpdate;
	m_forceReplicationUpdate = false;

	// Call outPreUpdate on the sender side. If it returns false, skip all
	// replicators for this node. The forceAll path bypasses this check so
	// a freshly connecting peer always receives the initial state.
	// M1 (latent): the remote role is hardcoded to eZCom_RoleProxy and the
	// `to` connID to 0; this pack is once-per-node, but the peer role varies
	// per peer in ZCom_processOutput. Correct per-peer interception would move
	// this call into the per-peer loop. Deferred until a consumer reads
	// remote_role (none do today).
	if (!forceAll && m_replicationInterceptor && !m_replicationInterceptor->outPreUpdate(this, 0, eZCom_RoleProxy)) {
		// Still need to push PackedReplicators for all entries (with
		// hasUpdate=false) to keep slot alignment with the receiver side.
		for (auto &rep : m_replicators) {
			PackedReplicator p;
			p.rule = rep->getSetup()->getRules();
			p.hasUpdate = false;
			out.push_back(std::move(p));
		}
		for (auto &entry : m_autoReplications) {
			PackedReplicator p;
			p.rule = entry->rule;
			p.hasUpdate = false;
			out.push_back(std::move(p));
		}
		return;
	}

	// Explicit replicators (ZCom_Replicator subclasses).
	// T1.3: apply per-replicator min/max-delay throttling here, where the
	// dirty/send decision is made. A replicator sent too recently (within
	// minDelay) is skipped but kept dirty so it retries next tick; a
	// replicator not changed but older than maxDelay is force-sent.
	const uint32_t nowTicks = ZoidCom::getTime();
	for (auto &rep : m_replicators) {
		PackedReplicator p;
		p.rule = rep->getSetup()->getRules();
		bool proceed = true;
		// For intercepted replicators, consult outPreUpdateItem on the sender
		// side. The forceAll path bypasses this so freshly connecting peers
		// always receive the initial intercept state (e.g. worm m_playerID).
		if (!forceAll && m_replicationInterceptor && (rep->getSetup()->getFlags() & ZCOM_REPFLAG_INTERCEPT)) {
			proceed = m_replicationInterceptor->outPreUpdateItem(this, 0, eZCom_RoleProxy, rep.get());
		}
		bool send = false;
		if (proceed) {
			bool changed = rep->checkState();
			int minD = rep->getSetup()->getMinDelay();
			int maxD = rep->getSetup()->getMaxDelay();
			bool force = forceAll;
			if (!force && maxD > 0 && (nowTicks - rep->getLastSendTime()) >= static_cast<uint32_t>(maxD))
				force = true;
			if (force) {
				send = true;
			} else if (changed) {
				if (minD > 0 && (nowTicks - rep->getLastSendTime()) < static_cast<uint32_t>(minD))
					send = false; // too soon since last send; keep dirty
				else
					send = true;
			}
		}
		if (send) {
			p.hasUpdate = true;
			rep->packData(&p.data); // clears dirty
			rep->setLastSendTime(nowTicks);
		} else {
			p.hasUpdate = false;
		}
		out.push_back(std::move(p));
	}

	// Auto-replications.
	for (auto &entry : m_autoReplications) {
		PackedReplicator p;
		p.rule = entry->rule;
		p.hasUpdate = entry->detect(forceAll);
		if (p.hasUpdate)
			entry->emit(p.data);
		out.push_back(std::move(p));
	}
}

void ZCom_Node::processReplicators(uint32_t simulation_time_passed) {
	// T1.3: drive per-replicator Process() callbacks (dead-reckoning /
	// interpolation hooks, e.g. ZCom_Interpolate). Throttling (min/max delay)
	// is applied at pack time in packReplicatorsForRouting.
	for (auto &rep : m_replicators) {
		if (rep->callProcess())
			rep->Process(getRole(), simulation_time_passed);
	}
}

void ZCom_Node::unpackAllReplicators(ZCom_BitStream *stream, bool store, uint32_t estimatedTimeSent) {
	// M1 (latent): the remote role passed to inPreUpdate/inPreUpdateItem below
	// is hardcoded to eZCom_RoleAuthority and the `from` connID to 0. This is
	// correct when the authority is the sender (the common case), but wrong when
	// an Owner sends replicas to the Authority. Threading the sender's connID +
	// looked-up role requires adding a connID param here and at the call sites in
	// net_control.cpp; deferred until a game consumer actually reads remote_role
	// (none do today).
	// Call inPreUpdate before processing updates
	if (m_replicationInterceptor && !m_replicationInterceptor->inPreUpdate(this, 0, eZCom_RoleAuthority))
		return;

	for (auto &rep : m_replicators) {
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
				proceed = m_replicationInterceptor->inPreUpdateItem(this, 0, eZCom_RoleAuthority, rep.get(),
																	estimatedTimeSent);
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
	for (auto &entry : m_autoReplications) {
		bool hasUpdate = stream->getInt(1) != 0;
		if (!hasUpdate)
			continue;
		bool intercept = m_replicationInterceptor && (entry->flags & ZCOM_REPFLAG_INTERCEPT) && m_interceptID >= 0;
		if (store && intercept) {
			// For intercepted auto-replications, decode the incoming value into a
			// temporary and expose it via peekData() so the interceptor can read it
			// (matching the Zoidcom contract used by BasePlayer/NetWorm).
			ZCom_ReplicatorSetup tempSetup(entry->flags, entry->rule, m_interceptID);
			ZCom_ReplicatorBasic tempRep(&tempSetup);
			void *decoded = entry->decodeForPeek(*stream);
			tempRep.peekDataStore(decoded);
			bool accept =
				m_replicationInterceptor->inPreUpdateItem(this, 0, eZCom_RoleAuthority, &tempRep, estimatedTimeSent);
			tempRep.peekDataStore(nullptr);
			if (accept)
				entry->commitPeek();
		} else if (store) {
			entry->unpackStore(*stream);
		} else {
			entry->skip(*stream);
		}
	}
}
