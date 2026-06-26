#ifndef NET_TYPES_H
#define NET_TYPES_H

// Standalone type definitions shared across all networking headers.
// No project includes — safe to include from any net_*.h file.

#include <cstdint>
#include <string>
#include <boost/cstdint.hpp>

// ---- Basic ZoidCom type aliases ----
typedef boost::uint32_t ZCom_ClassID;
typedef boost::uint32_t ZCom_ConnID;
typedef boost::uint32_t ZCom_NodeID;
typedef boost::uint32_t ZCom_InterceptID;
typedef boost::uint32_t ZCom_FileTransID;
typedef boost::int32_t  zS32;
typedef boost::uint8_t  zU8;
typedef boost::uint16_t zU16;
typedef boost::uint32_t zU32;
typedef float           zFloat;

#define INVALID_CONN_ID 0
#define ZCom_Invalid_ID 0

// ---- Send mode ----
enum eZCom_SendMode {
	eZCom_Reliable,
	eZCom_Unreliable,
	eZCom_ReliableOrdered,
	eZCom_ReliableUnordered
};

// ---- Node role ----
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
	eZCom_EventUser,
	eZCom_EventInit,
	eZCom_EventRemoved
};

// Extended event types for file transfer
enum {
	eZCom_EventFile_Incoming = 100,
	eZCom_EventFile_Complete = 101,
	eZCom_EventFile_Data = 102,
};

// ---- Connection result ----
enum eZCom_ConnectResult {
	eZCom_ConnAccepted,
	eZCom_ConnDenied,
	eZCom_ConnTimeout,
	eZCom_ConnHostnameFailed,
	eZCom_ConnWrongVersion
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
	eZCom_ZoidDisabled
};

// ---- Replication constants ----
#define ZCOM_REPRULE_AUTH_2_ALL 1
#define ZCOM_REPRULE_OWNER_2_AUTH 2
#define ZCOM_REPRULE_AUTH_2_PROXY 3
#define ZCOM_REPRULE_AUTH_2_OWNER 4
#define ZCOM_REPRULE_NONE 0

#define ZCOM_REPFLAG_MOSTRECENT 1
#define ZCOM_REPFLAG_INTERCEPT 2
#define ZCOM_REPFLAG_RARELYCHANGED 4

#define ZCOM_CLASSFLAG_ANNOUNCEDATA 1
#define ZCOM_REPLICATOR_INITIALIZED 1
#define ZCOM_FTRANS_ID_BITS 32
#define eZCom_NoBlock 0

#endif // NET_TYPES_H