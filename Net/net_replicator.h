#ifndef NET_REPLICATOR_H
#define NET_REPLICATOR_H

#include <cstdint>

// Forward declarations
class ZCom_BitStream;
class ZCom_Node;

// ---- Replicator Setup ----
class ZCom_ReplicatorSetup {
public:
	ZCom_ReplicatorSetup() : m_interceptID(0), m_repFlags(0), m_repRules(0) {}
	ZCom_ReplicatorSetup(uint32_t repFlags, uint32_t repRules, int interceptID = -1, int something = -1, int timeout = 0)
		: m_interceptID(interceptID), m_repFlags(repFlags), m_repRules(repRules) {}

	int getInterceptID() { return m_interceptID; }
	void setInterceptID(int id) { m_interceptID = id; }

	int m_interceptID;
	uint32_t m_repFlags;
	uint32_t m_repRules;
};

// ---- Base Replicator ----
class ZCom_Replicator {
public:
	virtual ~ZCom_Replicator() {}

	ZCom_ReplicatorSetup* getSetup() { return &m_setup; }

	virtual bool checkState() { return false; }
	virtual bool checkInitialState() { return true; }
	virtual void packData(ZCom_BitStream* stream) {}
	virtual void unpackData(ZCom_BitStream* stream, bool store, uint32_t estimatedTimeSent) {}
	virtual void Process(int localRole, uint32_t simulationTimePassed) {}
	virtual void* peekData() { return nullptr; }
	virtual ZCom_BitStream* getPeekStream() { return m_peekStream; }
	virtual void peekDataStore(void* data) { m_peekData = data; }
	virtual void* peekDataRetrieve() { return m_peekData; }

	ZCom_ReplicatorSetup m_setup;
	ZCom_BitStream* m_peekStream = nullptr;
	void* m_peekData = nullptr;
	uint32_t m_flags = 0;
};

// ---- Basic Replicator (default implementation holder) ----
class ZCom_ReplicatorBasic : public ZCom_Replicator {
public:
	ZCom_ReplicatorBasic() : m_flags(0) {}
	ZCom_ReplicatorBasic(ZCom_ReplicatorSetup* setup) {
		if (setup) m_setup = *setup;
		m_flags = 0;
	}
	uint32_t m_flags;
};

// ---- Node Replication Interceptor (callback base) ----
class ZCom_NodeReplicationInterceptor {
public:
	virtual ~ZCom_NodeReplicationInterceptor() {}
	virtual void ZCom_cbNodeReplicationIntercept(ZCom_BitStream* stream, ZCom_Node* node, int mode, int event, int role, uint32_t connID) {}
};

// ---- File transfer info struct ----
struct ZCom_FileTransInfo {
	uint32_t m_id;
	const char* m_filename;
	float m_progress;
	uint32_t m_bytes_downloaded;
	uint32_t m_file_size;
	const char* path;
	uint32_t bps;
	uint32_t transferred;
	uint32_t size;
};

#endif // NET_REPLICATOR_H