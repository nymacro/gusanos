#ifndef NET_TYPES_H
#define NET_TYPES_H

// Standalone type definitions shared across all networking headers.
// No project includes — safe to include from any net_*.h file.

#include <cstdint>
#include <climits>
typedef int8_t zS8;
typedef uint8_t zU8;
typedef int16_t zS16;
typedef uint16_t zU16;
typedef int32_t zS32;
typedef uint32_t zU32;
typedef int64_t zS64;
typedef uint64_t zU64;
typedef float zFloat;
typedef double zDouble;

typedef uint32_t ZCom_ClassID;
typedef uint32_t ZCom_ConnID;
typedef uint32_t ZCom_NodeID;
typedef zU8 ZCom_InterceptID;
typedef uint32_t ZCom_FileTransID;
typedef uint32_t ZCom_GroupID;

#define zU8_MAX UCHAR_MAX
#define zU16_MAX USHRT_MAX
#define zU32_MAX UINT_MAX
#define zU64_MAX ULLONG_MAX
#define zS32_MAX INT_MAX
#define zS32_MIN INT_MIN

#define INVALID_CONN_ID 0
#define ZCom_Invalid_ID 0

// ---- Send mode (matches reference zoidcom.h) ----
enum eZCom_SendMode {
	eZCom_ReliableUnordered = 0,
	eZCom_ReliableOrdered = 1,
	eZCom_Unreliable = 2,
	eZCom_UnreliableNotify = 3,
	eZCom_Reliable = eZCom_ReliableOrdered // alias for convenience
};

// ---- Block mode (aligned to reference zoidcom.h: Block=0, NoBlock=1) ----
// Replaces the former `#define eZCom_NoBlock 0` (which was inverted vs ref).
enum eZCom_BlockMode { eZCom_Block = 0, eZCom_NoBlock = 1 };

// ---- Address type (aligned to reference zoidcom.h) ----
enum eZCom_AddressType {
	eZCom_AddressLocal = 0,
	eZCom_AddressTCP = 1,
	eZCom_AddressUDP = 2,
	eZCom_AddressBroadcast = 3
};

// ---- Address IP string option (reference zoidcom.h) ----
enum eZCom_GetIPAddressOption { eZCom_AddressWithPort = 0, eZCom_AddressWithoutPort = 1 };

// ---- Node role (aligned to reference zoidcom.h: Undefined=0, Proxy=1, Owner=2, Authority=3) ----
// Note: eZCom_RoleAll and the anonymous ZCOM_ROLE_* enumerators from the
// old impl have been removed (no reference equivalent). Wire-serialized
// as 8 bits; renumbering is consistent across both ends of this layer.
enum eZCom_NodeRole { eZCom_RoleUndefined = 0, eZCom_RoleProxy = 1, eZCom_RoleOwner = 2, eZCom_RoleAuthority = 3 };

// ---- Node event types (aligned to reference zoidcom.h) ----
// File transfer events are folded into eZCom_Event (reference uses a single
// enum); the old separate anonymous file-event enum is gone. Event types are
// NOT wire-serialized (local pushEvent/dispatch only), so renumbering is safe.
enum eZCom_Event {
	eZCom_EventNoEvent = 0,
	eZCom_EventInit = 1,
	eZCom_EventSyncRequest = 2,
	eZCom_EventRemoved = 3,
	eZCom_EventFile_Incoming = 4,
	eZCom_EventFile_Data = 5,
	eZCom_EventFile_Aborted = 6,
	eZCom_EventFile_Complete = 7,
	eZCom_EventReplicator = 8,
	eZCom_EventUser = 9
};

// ---- Connection result (aligned to reference zoidcom.h) ----
// eZCom_ConnDenied is the primary name (=1); eZCom_ConnRefused kept as an
// alias for any existing references.
enum eZCom_ConnectResult {
	eZCom_ConnAccepted = 0,
	eZCom_ConnDenied = 1,
	eZCom_ConnRefused = eZCom_ConnDenied, // alias for backward compat
	eZCom_ConnTimeout = 2,
	eZCom_ConnHostnameFailed = 3,
	eZCom_ConnWrongVersion = 4
};

// ---- Close reason ----
enum eZCom_CloseReason { eZCom_ClosedDisconnect, eZCom_ClosedTimeout, eZCom_ClosedReconnect, eZCom_ClosedKicked };

// ---- Zoid result (matches reference zoidcom.h) ----
enum eZCom_ZoidResult {
	eZCom_ZoidEnabled,
	eZCom_ZoidDenied,
	eZCom_ZoidFailed_System,
	eZCom_ZoidFailed_Node,
	eZCom_ZoidDisabled
};

// ---- Hostname resolution result (reference zoidcom.h; signatures only) ----
enum eZCom_HostnameResult {
	eZCom_HostnameIdle = 0,
	eZCom_HostnameFailed = 1,
	eZCom_HostnameSuccess = 2,
	eZCom_HostnameInProgress = 3
};

// ---- Discovery option (reference zoidcom.h; signatures only) ----
enum eZCom_DiscoverOpt { eZCom_DiscoverEnable = 0, eZCom_DiscoverDisableAndKeep = 1, eZCom_DiscoverDisable = 2 };

// ---- Replicator internal flags ----
#define ZCOM_REPLICATOR_CALLPROCESS (1 << 0)
#define ZCOM_REPLICATOR_ZCOMOWNAGE (1 << 1)
#define ZCOM_REPLICATOR_INITIALIZED (1 << 2)
#define ZCOM_REPLICATOR_BASIC (1 << 3)
#define ZCOM_REPLICATOR_ADVANCED (1 << 4)
#define ZCOM_REPLICATOR_MODIFIED (1 << 5)
#define ZCOM_REPLICATOR_USER1 (1 << 6)
#define ZCOM_REPLICATOR_USER2 (1 << 7)

// ---- Replication constants ----
#define ZCOM_REPRULE_AUTH_2_PROXY (1 << 0)
#define ZCOM_REPRULE_AUTH_2_OWNER (1 << 1)
#define ZCOM_REPRULE_AUTH_2_ALL (ZCOM_REPRULE_AUTH_2_PROXY | ZCOM_REPRULE_AUTH_2_OWNER)
#define ZCOM_REPRULE_OWNER_2_AUTH (1 << 2)
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
#define ZCOM_FTRANS_SIZE_BITS 32
#define ZCOM_FTRANS_CHUNK_BITS 16
// ZCOM_CONNGROUP_ALL: reference value is 1; impl uses 0xFFFFFFFF for
// compatibility with existing internal group management. UNUSED by the game;
// left as-is (documented deviation).
#define ZCOM_CONNGROUP_ALL 0xFFFFFFFF

#endif // NET_TYPES_H