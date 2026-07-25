#include "net_address.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <arpa/inet.h>

ZCom_Address::ZCom_Address() : m_valid(false), m_type(eZCom_AddressLocal), m_controlID(0) {
	memset(&m_address, 0, sizeof(m_address));
}

ZCom_Address::ZCom_Address(const ENetAddress &addr)
	: m_address(addr), m_valid(true), m_type(eZCom_AddressLocal), m_controlID(0) {}

ZCom_Address::ZCom_Address(const ZCom_Address &other)
	: m_address(other.m_address), m_valid(other.m_valid), m_type(other.m_type), m_controlID(other.m_controlID),
	  m_hostname(other.m_hostname) {}

ZCom_Address &ZCom_Address::operator=(const ZCom_Address &other) {
	m_address = other.m_address;
	m_valid = other.m_valid;
	m_type = other.m_type;
	m_controlID = other.m_controlID;
	m_hostname = other.m_hostname;
	return *this;
}

ZCom_Address::~ZCom_Address() {}

bool ZCom_Address::setAddress(eZCom_AddressType type, zU8 controlID, const char *addr) {
	m_type = type;
	m_controlID = controlID;
	m_hostname = addr ? std::string(addr) : std::string();

	if (!addr) {
		m_valid = true; // local address, can leave host=0
		return true;
	}

	// Zoidcom allows setAddress(type, controlID, "host:port") or just "host"
	const char *colon = strrchr(addr, ':');
	int port = 0;

	if (colon) {
		port = std::atoi(colon + 1);
		// Use only the host part if port is valid
		if (port > 0 && port <= 65535) {
			std::string hostStr(addr, colon - addr);
			if (enet_address_set_host(&m_address, hostStr.c_str()) != 0) {
				m_valid = false;
				return false;
			}
		} else {
			if (enet_address_set_host(&m_address, addr) != 0) {
				m_valid = false;
				return false;
			}
		}
	} else {
		if (enet_address_set_host(&m_address, addr) != 0) {
			m_valid = false;
			return false;
		}
	}

	m_address.port = static_cast<uint16_t>(port);
	m_valid = true;
	return true;
}

void ZCom_Address::setIP(zU8 a, zU8 b, zU8 c, zU8 d) {
	zU32 h = (static_cast<zU32>(a) << 24) | (static_cast<zU32>(b) << 16) | (static_cast<zU32>(c) << 8) |
			 static_cast<zU32>(d);
	m_address.host = htonl(h);
	m_valid = true;
}

void ZCom_Address::setIP(zU32 ip) {
	m_address.host = htonl(ip);
	m_valid = true;
}

zU32 ZCom_Address::getIP(void) const {
	return ntohl(m_address.host);
}

zU8 ZCom_Address::getIP(zU8 pos) const {
	// ENet stores host in network byte order; convert for consistent extraction
	zU32 h = ntohl(m_address.host);
	switch (pos) {
		case 0:
			return static_cast<zU8>((h >> 24) & 0xFF);
		case 1:
			return static_cast<zU8>((h >> 16) & 0xFF);
		case 2:
			return static_cast<zU8>((h >> 8) & 0xFF);
		case 3:
			return static_cast<zU8>(h & 0xFF);
		default:
			return 0;
	}
}

bool ZCom_Address::operator==(const ZCom_Address &other) const {
	return m_address.host == other.m_address.host && m_address.port == other.m_address.port && m_type == other.m_type &&
		   m_controlID == other.m_controlID;
}

const char *ZCom_Address::getAddressIP(eZCom_GetIPAddressOption with_port) const {
	static char buf[80];
	if (!m_valid)
		return nullptr;
	char host[64];
	if (enet_address_get_host_ip(&m_address, host, sizeof(host)) != 0)
		return nullptr;
	if (with_port == eZCom_AddressWithPort)
		std::snprintf(buf, sizeof(buf), "%s:%u", host, static_cast<unsigned>(m_address.port));
	else
		std::snprintf(buf, sizeof(buf), "%s", host);
	return buf;
}

const char *ZCom_Address::getAddressHostname() const {
	return m_hostname.empty() ? nullptr : m_hostname.c_str();
}

const char *ZCom_Address::toString() const {
	static char buf[128];
	if (!m_valid) {
		std::snprintf(buf, sizeof(buf), "(invalid)");
		return buf;
	}
	if (m_type == eZCom_AddressLocal) {
		std::snprintf(buf, sizeof(buf), "[local]::%u", static_cast<unsigned>(m_address.port));
		return buf;
	}
	char host[64];
	if (enet_address_get_host_ip(&m_address, host, sizeof(host)) != 0)
		std::snprintf(buf, sizeof(buf), "[udp]:<unresolved>:%u", static_cast<unsigned>(m_address.port));
	else
		std::snprintf(buf, sizeof(buf), "[udp]:%s:%u", host, static_cast<unsigned>(m_address.port));
	return buf;
}

bool ZCom_Address::resolveHostname(bool /*async*/, zU32 /*timeout*/) {
	// setAddress() already resolves the hostname synchronously via ENet, so
	// there is no deferred work to start or wait for. Report success only when
	// the address is valid.
	return m_valid;
}

eZCom_HostnameResult ZCom_Address::checkHostname() {
	return m_valid ? eZCom_HostnameSuccess : eZCom_HostnameFailed;
}

zU32 ZCom_Address::computeHashKey(zU32 max) const {
	if (max == 0)
		return 0;
	zU32 h = getIP();
	h ^= static_cast<zU32>(m_address.port) << 16;
	h ^= static_cast<zU32>(m_controlID);
	return h % max;
}
