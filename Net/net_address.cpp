#include "net_address.h"
#include <cstring>
#include <cstdlib>
#include <sstream>

ZCom_Address::ZCom_Address()
	: m_valid(false)
{
	memset(&m_address, 0, sizeof(m_address));
}

ZCom_Address::ZCom_Address(const ENetAddress& addr)
	: m_address(addr), m_valid(true)
{
}

ZCom_Address::~ZCom_Address()
{
}

void ZCom_Address::setAddress(int type, int port, const char* addr)
{
	(void)type; // always UDP
	
	if (addr) {
		// Zoidcom allows setAddress(0, 0, "host:port") where the port
		// is embedded in the string. ENet's enet_address_set_host expects
		// only the host part, so parse the port from the string when port==0.
		const char* colon = strrchr(addr, ':');
		if (colon && port == 0) {
			port = std::atoi(colon + 1);
			if (port <= 0 || port > 65535) {
				m_valid = false;
				return;
			}
			// Use only the host part for enet_address_set_host
			std::string hostPart(addr, colon - addr);
			if (enet_address_set_host(&m_address, hostPart.c_str()) != 0) {
				// Try resolving the full address as-is (rare case)
				if (enet_address_set_host(&m_address, addr) != 0) {
					m_valid = false;
					return;
				}
			}
		} else {
			if (enet_address_set_host(&m_address, addr) != 0) {
				// Host resolution failed
				m_valid = false;
				return;
			}
		}
	}
	
	m_address.port = static_cast<uint16_t>(port);
	m_valid = true;
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