#ifndef NET_NODE_H
#define NET_NODE_H

#include "net_bitstream.h"
#include "net_replicator.h"
#include "net_types.h"
#include "net_auto_replicator.h"
#include <vector>
#include <list>
#include <map>
#include <string>
#include <cstdint>
#include <cassert>
#include <memory>
#include <enet/enet.h>

// Forward declarations
class ZCom_Control;
class ZCom_NodeEventInterceptor;

// ---- ConnGroupManager ----
class ZCom_ConnGroupManager {
public:
	ZCom_ConnGroupManager() : m_nextGroupID(1) {
		m_groups[0xFFFFFFFF] = std::vector<uint32_t>();
	}

	uint32_t createGroup(int maxSize) {
		(void)maxSize;
		uint32_t id = m_nextGroupID++;
		m_groups[id] = std::vector<uint32_t>();
		return id;
	}

	bool destroyGroup(uint32_t gid) {
		if (gid == 0xFFFFFFFF) return false;
		return m_groups.erase(gid) > 0;
	}

	bool checkGroupExists(uint32_t gid) {
		return m_groups.find(gid) != m_groups.end();
	}

	bool addConnection(uint32_t gid, uint32_t connID) {
		auto it = m_groups.find(gid);
		if (it == m_groups.end()) return false;
		it->second.push_back(connID);
		return true;
	}

	bool removeConnection(uint32_t gid, uint32_t connID) {
		auto it = m_groups.find(gid);
		if (it == m_groups.end()) return false;
		auto& vec = it->second;
		for (auto vi = vec.begin(); vi != vec.end(); ++vi) {
			if (*vi == connID) {
				vec.erase(vi);
				return true;
			}
		}
		return false;
	}

	uint32_t getGroupSize(uint32_t gid) {
		auto it = m_groups.find(gid);
		if (it == m_groups.end()) return 0;
		return static_cast<uint32_t>(it->second.size());
	}

	uint32_t getFirstConnection(uint32_t gid, uint32_t& iterator) {
		auto it = m_groups.find(gid);
		if (it == m_groups.end() || it->second.empty()) {
			iterator = 0;
			return 0;
		}
		iterator = 1;
		return it->second[0];
	}

	uint32_t getNextConnection(uint32_t gid, uint32_t& iterator) {
		auto it = m_groups.find(gid);
		if (it == m_groups.end() || iterator >= it->second.size()) {
			iterator = 0;
			return 0;
		}
		uint32_t result = it->second[iterator];
		iterator++;
		return result;
	}

private:
	uint32_t m_nextGroupID;
	std::map<uint32_t, std::vector<uint32_t>> m_groups;
};

// ---- ZoidCom global class ----
class ZoidCom {
public:
	ZoidCom(void (*logfunc)(const char*)) : m_logFunc(logfunc) {}
	bool Init() { return true; }

	static void Sleep(int ms);
	static zU32 getTime();

private:
	void (*m_logFunc)(const char*);
};

// Auto replication entry (see net_auto_replicator.h).
// Replaces the former POD ReplicationEntry struct (hand-rolled tagged union
// with a Type{Int,Float,Bool} enum). The polymorphic AutoReplicator owns the
// bound pointer, the "old" snapshot and the type-specific (de)serialization,
// so net_node.cpp carries no per-type dispatch.
struct PackedReplicator {
	bool hasUpdate;
	uint32_t rule;
	ZCom_BitStream data;
};

struct NodeEvent {
	eZCom_Event type;
	eZCom_NodeRole role;
	uint32_t connID;
	std::unique_ptr<ZCom_BitStream> data;
};

class ZCom_Node {
public:
	ZCom_Node();
	~ZCom_Node();

	// Replication setup
	void beginReplicationSetup(int level);
	void endReplicationSetup();
	void beginReplication() {}
	void endReplication() {}
	void registerReplicator(void* replicator);
	void addReplicator(std::unique_ptr<ZCom_Replicator> replicator, bool flag);
	void addReplicationInt(zS32* val, zU8 bits, bool sign, zU8 flags, zU8 rules, zS16 mindelay = -1, zS16 maxdelay = -1);
	void addReplicationFloat(zFloat* val, zU8 mantissa_bits, zU8 flags, zU8 rules, zS16 mindelay = -1, zS16 maxdelay = -1);
	void addReplicationBool(bool* val, zU8 flags, zU8 rules, zS16 mindelay = -1, zS16 maxdelay = -1);
	void setInterceptID(ZCom_InterceptID id);
	void setReplicationInterceptor(ZCom_NodeReplicationInterceptor* interceptor) { m_replicationInterceptor = interceptor; }
	void setEventNotification(bool init, bool remove);
	void setAnnounceData(std::unique_ptr<ZCom_BitStream> data);
	void setEventInterceptor(ZCom_NodeEventInterceptor* interceptor) { m_eventInterceptor = interceptor; }
	bool registerNodeDynamic(uint32_t classID, void* control);
	bool registerNodeUnique(uint32_t classID, eZCom_NodeRole role, void* control);
	void unregisterNode();
	bool registerRequestedNode(uint32_t classID, void* control);
	void applyForZoidLevel(int level);
	void setOwner(ZCom_ConnID id, bool auth);
	void sendEvent(eZCom_SendMode mode, zU8 rules, ZCom_BitStream* stream);
	void sendEventDirect(eZCom_SendMode mode, ZCom_BitStream* stream, ZCom_ConnID id);
	uint32_t getNetworkID() { return m_nodeID; }
	void setNetworkID(uint32_t id) { m_nodeID = id; }
	// Force the next packReplicatorsForRouting to pack all entries regardless of
	// dirty flags. Used by ZCom_Control when a new peer connects to ensure the
	// peer receives the current state of authority nodes created earlier.
	void forceReplicationUpdate() { m_forceReplicationUpdate = true; }

	// Build announce data by firing the outPreReplicateNode interceptor (if set),
	// then returning the announce data stream. Must be called before the node
	// announcement is serialized (every code path that reads announce data
	// should call this instead of getAnnounceData()). The _to/_remote_role
	// params identify the peer the node is being announced to.
	ZCom_BitStream* buildAnnounceData(uint32_t to, eZCom_NodeRole remoteRole);

	int getZoidLevel() const { return m_zoidLevel; }
	bool checkEventWaiting();
	std::unique_ptr<ZCom_BitStream> getNextEvent(eZCom_Event* type, eZCom_NodeRole* role, ZCom_ConnID* connid, zU32* estimated_time_sent = nullptr);
	eZCom_NodeRole getRole() const { return (eZCom_NodeRole)m_role; }
	bool getEventNotification() const { return m_eventNotification; }

	void setUserData(void* data) { m_userData = data; }
	void* getUserData() { return m_userData; }

	void setPrivate(bool priv) { m_isPrivate = priv; }
	bool isPrivate() const { return m_isPrivate; }
	void setUpdatePriority(zU16 pri) { m_updatePriority = pri; }
	void setDefaultRelevance(float rel) { m_defaultRelevance = rel; }
	uint32_t getRelevantConnectionCount() { return m_relevantConnectionCount; }

	void removeFromZoidLevel(int) {}

	// File transfer (Phase E) — delegate to the owning ZCom_Control.
	// Reference signatures (zoidcom_node.h:801-820). The game calls these with
	// null pointer literals (0) for _pathtosend/_data/_path which bind fine.
	// Defined out-of-line in net_node.cpp (ZCom_Control is only forward
	// declared in this header).
	ZCom_FileTransID sendFile(const char* _path, const char* _pathtosend,
		ZCom_ConnID _destconn, ZCom_BitStream* _data, zFloat _aggressivenes);
	void acceptFile(ZCom_ConnID _src_id, ZCom_FileTransID _ftrans_id,
		const char* _path, bool _accept);
	const ZCom_FileTransInfo& getFileInfo(ZCom_ConnID _conn_id, ZCom_FileTransID _ftrans_id) const;

	// Internal
	ZCom_BitStream* getAnnounceData() const { return m_announceData.get(); }
	uint32_t getOwner() const { return m_ownerID; }
	void setNodeID(uint32_t id) { m_nodeID = id; }
	void setRole(eZCom_NodeRole role) { m_role = role; }
	ZCom_Control* getControl() { return m_control; }
	void setControl(ZCom_Control* c) { m_control = c; }

	bool isUnique() const { return m_isUnique; }
	uint32_t getClassID() const { return m_classID; }
	void setClassID(uint32_t id) { m_classID = id; }

	void pushEvent(eZCom_Event type, eZCom_NodeRole role, uint32_t connID, ZCom_BitStream* data);
	
	void packAllReplicators(ZCom_BitStream* stream);
	void unpackAllReplicators(ZCom_BitStream* stream, bool store, uint32_t estimatedTimeSent);

	// Pre-pack every replicator once (clearing dirty flags) into per-replicator
	// buffers with their reprule, so callers can route the pre-packed bytes to
	// matching peers without re-calling packData (which would no-op after the
	// first call clears dirty). Mirrors packAllReplicators' iteration order
	// (m_replicators then m_autoReplications) so receivers stay aligned.
	void packReplicatorsForRouting(std::vector<PackedReplicator>& out);

private:
	uint32_t m_nodeID;
	uint32_t m_classID;
	uint32_t m_ownerID;
	int m_role;
	bool m_isUnique;
	bool m_isPrivate;
	bool m_eventNotification;
	bool m_eventNotificationRemove;
	bool m_authority;
	ZCom_Control* m_control;            // non-owning (game owns the node)
	std::unique_ptr<ZCom_BitStream> m_announceData;
	void* m_userData;                   // non-owning (set by game)
	ZCom_NodeEventInterceptor* m_eventInterceptor;  // non-owning
	int m_updatePriority;
	float m_defaultRelevance;
	uint32_t m_relevantConnectionCount;

	std::vector<std::unique_ptr<ZCom_Replicator>> m_replicators;
	std::vector<std::unique_ptr<AutoReplicator>> m_autoReplications;
	std::list<NodeEvent> m_eventQueue;
	int m_zoidLevel;
	int m_interceptID;
	ZCom_NodeReplicationInterceptor* m_replicationInterceptor;  // non-owning

	// Set when a new peer needs the initial replicator state (set during the
	// connection-time node sync, consumed by the next packReplicatorsForRouting).
	// Without this, server-side authority nodes created before any client
	// connected would have their initial=true entries consumed by an empty
	// processOutput, leaving newly connecting peers without any state.
	bool m_forceReplicationUpdate = false;
};

#endif // NET_NODE_H
