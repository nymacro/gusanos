#ifndef NET_ADDRESS_H
#define NET_ADDRESS_H

#include <string>
#include <cstdint>
#include <enet/enet.h>
#include "net_types.h"  // eZCom_AddressType, eZCom_GetIPAddressOption, eZCom_HostnameResult, z-types

// ZCom_Address: ENet-backed implementation of the ZoidCom reference address
// API (zoidcom_address.h). The pimpl `ZCom_Address_Private*` of the reference
// is replaced by flat ENet storage plus a retained hostname string; the public
// method set matches the reference. getENetAddress() is an ENet-interop
// addition (not in the reference) used by net_control.
class ZCom_Address {
public:
	ZCom_Address(void);
	// ENet-interop ctor (our addition; not in reference)
	explicit ZCom_Address(const ENetAddress& addr);
	ZCom_Address(const ZCom_Address& other);
	ZCom_Address& operator=(const ZCom_Address& other);
	~ZCom_Address(void);

	// Set IP and port from string representation ("host:port" or "host").
	// Returns false on invalid syntax. The hostname is resolved immediately
	// (synchronous), and retained for getAddressHostname().
	bool setAddress(eZCom_AddressType type, zU8 controlID, const char* addr);

	// Returns string representation of the resolved IP address in a static
	// buffer ("w.x.y.z:port" or "w.x.y.z"). NULL if not set as an IP.
	const char* getAddressIP(eZCom_GetIPAddressOption with_port = eZCom_AddressWithPort) const;

	// Returns the previously set hostname string (static buffer), or NULL if
	// the address was not set from a hostname string.
	const char* getAddressHostname() const;

	// Full string representation ("[local]::port" or "[udp]:x.x.y.z:port")
	// in a static buffer.
	const char* toString() const;

	// Set IP address by four octets.
	void setIP(zU8 a, zU8 b, zU8 c, zU8 d);
	// Set IP address (host byte order).
	void setIP(zU32 ip);
	// Set port.
	void setPort(zU16 port) { m_address.port = port; m_valid = true; }
	// Set type.
	void setType(eZCom_AddressType type) { m_type = type; }
	// Set control id.
	void setControlID(zU8 id) { m_controlID = id; }

	// Get port.
	zU16 getPort(void) const { return m_address.port; }
	// Get IP as one 32bit value (host byte order).
	zU32 getIP(void) const;
	// Get IP component (0-3).
	zU8 getIP(zU8 pos) const;
	// Get type.
	eZCom_AddressType getType(void) const { return m_type; }
	// Get control ID.
	zU8 getControlID(void) const { return m_controlID; }

	// Compare two addresses.
	bool operator==(const ZCom_Address& other) const;

	// Resolve previously set hostname to IP. This implementation resolves
	// synchronously in setAddress(), so this is effectively a no-op stub.
	// Returns true if a hostname is set and resolution is/was successful.
	bool resolveHostname(bool async, zU32 timeout);
	// Check async hostname resolution status. Synchronous impl never returns
	// InProgress; returns Success if valid, Failed otherwise.
	eZCom_HostnameResult checkHostname();
	// Compute hash key for the address (internal).
	zU32 computeHashKey(zU32 max) const;

	// ENet interop (our addition; not in reference)
	const ENetAddress& getENetAddress() const { return m_address; }
	ENetAddress& getENetAddress() { return m_address; }

private:
	ENetAddress m_address;
	bool m_valid;
	eZCom_AddressType m_type;
	zU8 m_controlID;
	std::string m_hostname;  // retained original setAddress() string (may include ":port")
};

#endif // NET_ADDRESS_H
