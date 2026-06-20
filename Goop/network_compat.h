#ifndef NETWORK_COMPAT_H
#define NETWORK_COMPAT_H

#include <boost/cstdint.hpp>
#include <string>

// ZoidCom compatibility stubs
typedef boost::uint32_t ZCom_ClassID;
typedef boost::uint32_t ZCom_ConnID;
typedef boost::uint32_t ZCom_NodeID;
typedef boost::uint32_t ZCom_InterceptID;
typedef boost::int32_t zS32;
typedef boost::uint8_t zU8;
typedef boost::uint16_t zU16;
typedef boost::uint32_t zU32;
typedef float zFloat;

// INVALID_NODE_ID is defined as const in network.h
#define INVALID_CONN_ID 0
#define ZCom_Invalid_ID 0

enum eZCom_SendMode {
    eZCom_Reliable,
    eZCom_Unreliable,
    eZCom_ReliableOrdered,
    eZCom_ReliableUnordered
};

enum eZCom_NodeRole {
    ZCOM_ROLE_AUTHORITY,
    ZCOM_ROLE_PROXY,
    eZCom_RoleUndefined,
    eZCom_RoleAuthority = ZCOM_ROLE_AUTHORITY,
    eZCom_RoleProxy = ZCOM_ROLE_PROXY
};

enum eZCom_Event {
    eZCom_EventUser,
    eZCom_EventInit,
    eZCom_EventRemoved
};

// ZoidCom file transfer stubs
typedef zU32 ZCom_FileTransID;

struct ZCom_FileTransInfo {
    zU32 m_id;
    const char* m_filename;
    float m_progress;
    zU32 m_bytes_downloaded;
    zU32 m_file_size;
    // Fields used by updater.cpp
    const char* path;
    zU32 bps;
    zU32 transferred;
    zU32 size;
};

// Extended event types for file transfer
enum {
    eZCom_EventFile_Incoming = 100,
    eZCom_EventFile_Complete = 101,
    eZCom_EventFile_Data = 102,
};

#define ZCOM_REPRULE_AUTH_2_ALL 0
#define ZCOM_REPRULE_OWNER_2_AUTH 0
#define ZCOM_REPRULE_AUTH_2_PROXY 0
#define ZCOM_REPRULE_AUTH_2_OWNER 0
#define ZCOM_REPRULE_NONE 0

#define ZCOM_REPFLAG_MOSTRECENT 0
#define ZCOM_REPFLAG_INTERCEPT 0

#define ZCOM_CLASSFLAG_ANNOUNCEDATA 0

#define ZCOM_REPLICATOR_INITIALIZED 1

class ZCom_BitStream {
public:
    void addInt(int val, int bits) {}
    void addSignedInt(int val, int bits) {}
    int getInt(int bits) { return 0; }
    int getSignedInt(int bits) { return 0; }
    void addBool(bool val) {}
    bool getBool() { return false; }
    void addFloat(float val, int bits) {}
    float getFloat(int bits) { return 0.0f; }
    void addString(const char* str) {}
    const char* getStringStatic() { return ""; }
    char* getString() { return (char*)""; }
    void addBitStream(ZCom_BitStream* other) {}
    ZCom_BitStream* Duplicate() { return new ZCom_BitStream(); }
};

class ZCom_ReplicatorSetup {
public:
    ZCom_ReplicatorSetup() {}
    ZCom_ReplicatorSetup(zU32 repFlags, zU32 repRules, int interceptID = -1, int something = -1, int timeout = 0) {}
    ZCom_InterceptID getInterceptID() { return 0; }
    void setInterceptID(ZCom_InterceptID id) {}
};

class ZCom_Replicator {
public:
    virtual ~ZCom_Replicator() {}
    ZCom_ReplicatorSetup* getSetup() { return nullptr; }
    void* peekData() { return nullptr; }
    ZCom_BitStream* getPeekStream() { return nullptr; }
    void peekDataStore(void* data) {}
    void* peekDataRetrieve() { return nullptr; }
};

class ZCom_ReplicatorBasic : public ZCom_Replicator {
public:
    ZCom_ReplicatorBasic() {}
    ZCom_ReplicatorBasic(ZCom_ReplicatorSetup* setup) {}
    zU32 m_flags;
};

class ZCom_Node {
public:
    void beginReplicationSetup(int level) {}
    void endReplicationSetup() {}
    void beginReplication() {}
    void endReplication() {}
    void registerReplicator(void* replicator) {}
    void addReplicator(ZCom_Replicator* replicator, bool flag) {}
    void addReplicationInt(zS32* val, int bits, bool sign, zU32 flags, zU32 rule, ZCom_NodeID id) {}
    void addReplicationFloat(zFloat* val, int bits, zU32 flags, zU32 rule) {}
    void setInterceptID(ZCom_InterceptID id) {}
    void setReplicationInterceptor(void* interceptor) {}
    void setEventNotification(bool init, bool remove) {}
    void setAnnounceData(ZCom_BitStream* data) {}
    bool registerNodeDynamic(ZCom_ClassID id, void* control) { return true; }
    bool registerNodeUnique(ZCom_ClassID id, eZCom_NodeRole role, void* control) { return true; }
    bool registerRequestedNode(ZCom_ClassID id, void* control) { return true; }
    void applyForZoidLevel(int level) {}
    void setOwner(ZCom_ConnID id, bool auth) {}
    void sendEvent(eZCom_SendMode mode, zU32 rules, ZCom_BitStream* stream) {}
    void sendEventDirect(eZCom_SendMode mode, ZCom_BitStream* stream, ZCom_ConnID id) {}
    ZCom_NodeID getNetworkID() { return 0; }
    bool checkEventWaiting() { return false; }
    ZCom_BitStream* getNextEvent(eZCom_Event* type, eZCom_NodeRole* role, ZCom_ConnID* id) { return nullptr; }
    eZCom_NodeRole getRole() { return eZCom_RoleUndefined; }
    void removeFromZoidLevel(int) {}
    ZCom_FileTransID sendFile(const char* filename, int something, ZCom_ConnID connID, int flags, float ratio) { return 0; }
    void acceptFile(ZCom_ConnID& connID, ZCom_FileTransID& fid, int flags, bool writable) {}
    ZCom_FileTransInfo getFileInfo(ZCom_ConnID connID, ZCom_FileTransID fid) { return ZCom_FileTransInfo{}; }
};

#define ZCOM_FTRANS_ID_BITS 32

class ZCom_Control {
public:
    virtual ~ZCom_Control() {}
    ZCom_ClassID ZCom_registerClass(const char* name, zU32 flags) { return 0; }
    void ZCom_processOutput() {}
    void ZCom_processInput(int flags) {}
    void ZCom_Connect(class ZCom_Address& addr, ZCom_BitStream* data) {}
    void ZCom_disconnectAll(ZCom_BitStream* data) {}
    void ZCom_Disconnect(ZCom_ConnID id, ZCom_BitStream* data) {}
    void Shutdown() {}
    class ZCom_Address const* ZCom_getPeer(ZCom_ConnID id) { return nullptr; }
    struct ConnStats { int avg_ping; };
    ConnStats ZCom_getConnectionStats(ZCom_ConnID id) { return {0}; }
};

#define eZCom_NoBlock 0

class ZCom_Address {
public:
    void setAddress(int type, int port, const char* addr) {}
    zU32 getIP() const { return 0; }
};
#define eZCom_AddressUDP 0

class ZoidCom {
public:
    ZoidCom(void (*log)(const char*) = nullptr) {}
    void setLogLevel(int level) {}
    bool Init() { return true; }
};

class ZCom_NodeReplicationInterceptor {
public:
    virtual ~ZCom_NodeReplicationInterceptor() {}
};

#endif // NETWORK_COMPAT_H