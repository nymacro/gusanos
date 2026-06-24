#ifndef NET_ADDRESS_H
#define NET_ADDRESS_H

#include <string>
#include <cstdint>
#include <enet/enet.h>

class ZCom_Address {
public:
	ZCom_Address();
	explicit ZCom_Address(const ENetAddress& addr);
	~ZCom_Address();

	void setAddress(int type, int port, const char* addr);
	uint32_t getIP() const { return m_address.host; }
	int getPort() const { return m_address.port; }
	
	const ENetAddress& getENetAddress() const { return m_address; }
	ENetAddress& getENetAddress() { return m_address; }
	
	std::string toString() const;

private:
	ENetAddress m_address;
	bool m_valid;
};

#endif // NET_ADDRESS_H