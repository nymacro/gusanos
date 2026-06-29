#include "net_address.h"
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <arpa/inet.h>

ZCom_Address::ZCom_Address()
	: m_valid(false), m_type(0), m_controlID(0)
{
	memset(&m_address, 0, sizeof(m_address));
}

ZCom_Address::ZCom_Address(const ENetAddress& addr)
	: m_address(addr), m_valid(true), m_type(0), m_controlID(0)
{
}

ZCom_Address::ZCom_Address(const ZCom_Address& other)
	: m_address(other.m_address), m_valid(other.m_valid)
	, m_type(other.m_type), m_controlID(other.m_controlID)
{
}

ZCom_Address& ZCom_Address::operator=(const ZCom_Address& other)
{
	m_address = other.m_address;
	m_valid = other.m_valid;
	m_type = other.m_type;
	m_controlID = other.m_controlID;
	return *this;
}

ZCom_Address::~ZCom_Address()
{
}

bool ZCom_Address::setAddress(int type, int controlID, const char* addr)
{
	m_type = type;
	m_controlID = static_cast<uint32_t>(controlID);

	if (!addr) {
		m_valid = true; // local address, can leave host=0
		return true;
	}

	// Zoidcom allows setAddress(type, controlID, "host:port") or just "host"
	const char* colon = strrchr(addr, ':');
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
	return true;
}

void ZCom_Address::setIP(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
	uint32_t h = (static_cast<uint32_t>(a) << 24)
	           | (static_cast<uint32_t>(b) << 16)
	           | (static_cast<uint32_t>(c) << 8)
	           | static_cast<uint32_t>(d);
	m_address.host = htonl(h);
	m_valid = true;
}

uint8_t ZCom_Address::getIP(int byteIndex) const
{
	// ENet stores host in network byte order; convert for consistent extraction
	uint32_t h = ntohl(m_address.host);
	switch (byteIndex) {
		case 0: return static_cast<uint8_t>((h >> 24) & 0xFF);
		case 1: return static_cast<uint8_t>((h >> 16) & 0xFF);
		case 2: return static_cast<uint8_t>((h >> 8) & 0xFF);
		case 3: return static_cast<uint8_t>(h & 0xFF);
		default: return 0;
	}
}

uint32_t ZCom_Address::getIP() const { return ntohl(m_address.host); }

bool ZCom_Address::operator==(const ZCom_Address& other) const
{
	return m_address.host == other.m_address.host
	    && m_address.port == other.m_address.port
	    && m_type == other.m_type
	    && m_controlID == other.m_controlID;
}

std::string ZCom_Address::toString() const
{
	if (!m_valid) return "(invalid)";
	char host[256];
	enet_address_get_host_ip(&m_address, host, sizeof(host));
	std::ostringstream oss;
	oss << host << ":" << m_address.port;
	return oss.str();
}
