#ifndef NET_TYPES_H
#define NET_TYPES_H

// Standalone type definitions shared across all networking headers.
// No project includes — safe to include from any net_*.h file.

#include <cstdint>
#include <climits>
typedef uint32_t ZCom_ClassID;
typedef uint32_t ZCom_ConnID;
typedef uint32_t ZCom_NodeID;
typedef uint32_t ZCom_InterceptID;
typedef uint32_t ZCom_FileTransID;
typedef uint32_t ZCom_GroupID;
typedef int32_t   zS32;
typedef uint8_t   zU8;
typedef uint16_t  zU16;
typedef uint32_t  zU32;
typedef uint64_t  zU64;
typedef int64_t   zS64;
typedef float     zFloat;
typedef double    zDouble;

#define zU8_MAX   UCHAR_MAX
#define zU16_MAX  USHRT_MAX
#define zU32_MAX  UINT_MAX
#define zU64_MAX  ULLONG_MAX
#define zS32_MAX  INT_MAX
#define zS32_MIN  INT_MIN

#define INVALID_CONN_ID 0
#define ZCom_Invalid_ID 0

// ---- Send mode (matches reference zoidcom.h) ----
enum eZCom_SendMode {
	eZCom_ReliableUnordered = 0,
	eZCom_ReliableOrdered = 1,
	eZCom_Unreliable = 2,
	eZCom_UnreliableNotify = 3,
	eZCom_Reliable = eZCom_ReliableOrdered  // alias for convenience
};

// ---- Node role ----
// NOTE: ZCOM_ROLE_AUTHORITY=0 and ZCOM_ROLE_PROXY=1 are anonymous
// enumerators in eZCom_NodeRole. Some game code relies on
// eZCom_RoleAuthority being 0 (the default ZCom_Node role), which
// differs from the ZoidCom reference (where eZCom_RoleAuthority=3).
// We keep this layout for compatibility with the game codebase.

enum eZCom_NodeRole {
	ZCOM_ROLE_AUTHORITY,
	ZCOM_ROLE_PROXY,
	eZCom_RoleUndefined,
	eZCom_RoleAuthority = ZCOM_ROLE_AUTHORITY,
	eZCom_RoleProxy = ZCOM_ROLE_PROXY,
	eZCom_RoleOwner,
	eZCom_RoleAll
};

// ---- Node event types ----
enum eZCom_Event {
	eZCom_EventNoEvent = 0,
	eZCom_EventInit = 1,
	eZCom_EventSyncRequest = 2,
	eZCom_EventRemoved = 3,
	eZCom_EventReplicator = 50,
	eZCom_EventUser = 100
};

// Extended event types for file transfer (must not overlap with eZCom_Event)
enum {
	eZCom_EventFile_Incoming = 200,
	eZCom_EventFile_Aborted = 201,
	eZCom_EventFile_Complete = 202,
	eZCom_EventFile_Data = 203,
};

// ---- Connection result (matches reference zoidcom.h) ----
enum eZCom_ConnectResult {
	eZCom_ConnAccepted = 0,
	eZCom_ConnRefused = 1,
	eZCom_ConnTimeout = 2,
	eZCom_ConnDenied = eZCom_ConnRefused  // alias for backward compat
};

// ---- Close reason ----
enum eZCom_CloseReason {
	eZCom_ClosedDisconnect,
	eZCom_ClosedTimeout,
	eZCom_ClosedReconnect,
	eZCom_ClosedKicked
};

// ---- Zoid result ----
enum eZCom_ZoidResult {
	eZCom_ZoidEnabled,
	eZCom_ZoidDenied,
	eZCom_ZoidFailed_System,
	eZCom_ZoidFailed_Node,
	eZCom_ZoidDisabled
};

// ---- Replicator internal flags ----
#define ZCOM_REPLICATOR_CALLPROCESS     (1<<0)
#define ZCOM_REPLICATOR_ZCOMOWNAGE      (1<<1)
#define ZCOM_REPLICATOR_INITIALIZED     (1<<2)
#define ZCOM_REPLICATOR_BASIC           (1<<3)
#define ZCOM_REPLICATOR_ADVANCED        (1<<4)
#define ZCOM_REPLICATOR_MODIFIED        (1<<5)
#define ZCOM_REPLICATOR_USER1           (1<<6)
#define ZCOM_REPLICATOR_USER2           (1<<7)

// ---- Replication constants ----
#define ZCOM_REPRULE_AUTH_2_PROXY (1<<0)
#define ZCOM_REPRULE_AUTH_2_OWNER (1<<1)
#define ZCOM_REPRULE_AUTH_2_ALL (ZCOM_REPRULE_AUTH_2_PROXY|ZCOM_REPRULE_AUTH_2_OWNER)
#define ZCOM_REPRULE_OWNER_2_AUTH (1<<2)
#define ZCOM_REPRULE_NONE 0

#define ZCOM_REPFLAG_NONE 0
#define ZCOM_REPFLAG_UNRELIABLE 1
#define ZCOM_REPFLAG_MOSTRECENT 2
#define ZCOM_REPFLAG_RARELYCHANGED 4
#define ZCOM_REPFLAG_ONLYONCE 8
#define ZCOM_REPFLAG_INTERCEPT 16
#define ZCOM_REPFLAG_SETUPPERSISTS 32
#define ZCOM_REPFLAG_SETUPAUTODELETE 64
#define ZCOM_REPFLAG_STARTCLEAN 128

#define ZCOM_CLASSFLAG_ANNOUNCEDATA 1
#define ZCOM_FTRANS_ID_BITS 32
#define ZCOM_CONNGROUP_ALL 0xFFFFFFFF
#define eZCom_NoBlock 0
#define eZCom_EventNoEvent 0
#define eZCom_EventUser 100
#define eZCom_EventReplicator 50

#endif // NET_TYPES_H