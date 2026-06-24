#ifndef NET_NODE_H
#define NET_NODE_H

#include "net_bitstream.h"
#include "net_replicator.h"
#include "net_types.h"
#include <vector>
#include <list>
#include <string>
#include <cstdint>
#include <cassert>
#include <enet/enet.h>

// Forward declarations
class ZCom_Control;


// Auto replication entry
struct ReplicationEntry {
	enum Type { TypeInt, TypeFloat };
	Type type;
	void* ptr;
	int bits;
	bool sign;
	uint32_t flags;
	uint32_t rule;
	uint32_t oldInt;
	float oldFloat;
};

struct NodeEvent {
	eZCom_Event type;
	eZCom_NodeRole role;
	uint32_t connID;
	ZCom_BitStream* data;
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
	void addReplicator(ZCom_Replicator* replicator, bool flag);
	void addReplicationInt(int32_t* val, int bits, bool sign, uint32_t flags, uint32_t rule, uint32_t id = 0);
	void addReplicationFloat(float* val, int bits, uint32_t flags, uint32_t rule);
	void setInterceptID(int id);
	void setReplicationInterceptor(void* interceptor);
	void setEventNotification(bool init, bool remove);
	void setAnnounceData(ZCom_BitStream* data);
	bool registerNodeDynamic(uint32_t classID, void* control);
	bool registerNodeUnique(uint32_t classID, int role, void* control);
	bool registerRequestedNode(uint32_t classID, void* control);
	void applyForZoidLevel(int level);
	void setOwner(uint32_t id, bool auth);
	void sendEvent(int mode, uint32_t rules, ZCom_BitStream* stream);
	void sendEventDirect(int mode, ZCom_BitStream* stream, uint32_t id);
	uint32_t getNetworkID() { return m_nodeID; }
	void setNetworkID(uint32_t id) { m_nodeID = id; }
	bool checkEventWaiting();
	ZCom_BitStream* getNextEvent(eZCom_Event* type, eZCom_NodeRole* role, uint32_t* id);
	int getRole() { return m_role; }

	void removeFromZoidLevel(int) {}

	// File transfer stubs (to be implemented with ENet later)
	uint32_t sendFile(const char* filename, int something, uint32_t connID, int flags, float ratio) { return 0; }
	void acceptFile(uint32_t& connID, uint32_t& fid, int flags, bool writable) {}
	ZCom_FileTransInfo getFileInfo(uint32_t connID, uint32_t fid) { return ZCom_FileTransInfo{}; }

	// Internal
	uint32_t getOwner() const { return m_ownerID; }
	void setNodeID(uint32_t id) { m_nodeID = id; }
	void setRole(int role) { m_role = role; }
	ZCom_Control* getControl() { return m_control; }
	void setControl(ZCom_Control* c) { m_control = c; }

	uint32_t getClassID() const { return m_classID; }
	void setClassID(uint32_t id) { m_classID = id; }

	void pushEvent(eZCom_Event type, eZCom_NodeRole role, uint32_t connID, ZCom_BitStream* data);
	
	void packAllReplicators(ZCom_BitStream* stream);
	void unpackAllReplicators(ZCom_BitStream* stream, bool store, uint32_t estimatedTimeSent);

private:
	uint32_t m_nodeID;
	uint32_t m_classID;
	uint32_t m_ownerID;
	int m_role;
	bool m_eventNotification;
	bool m_authority;
	ZCom_Control* m_control;
	ZCom_BitStream* m_announceData;
	
	std::vector<ZCom_Replicator*> m_replicators;
	std::vector<ReplicationEntry> m_autoReplications;
	std::list<NodeEvent> m_eventQueue;
};

#endif // NET_NODE_H