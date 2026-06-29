#ifndef NET_ADDRESS_H
#define NET_ADDRESS_H

#include <string>
#include <cstdint>
#include <enet/enet.h>

// Address type constants
const int eZCom_AddressUDP = 0;
const int eZCom_AddressLocal = 1;

class ZCom_Address {
public:
	ZCom_Address();
	explicit ZCom_Address(const ENetAddress& addr);
	ZCom_Address(const ZCom_Address& other);
	ZCom_Address& operator=(const ZCom_Address& other);
	~ZCom_Address();

	// ZoidCom API: setAddress(type, controlID, "host:port") — port parsed from string
	bool setAddress(int type, int controlID, const char* addr);

	// IP byte access
	void setIP(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
	uint8_t getIP(int byteIndex) const;
	uint32_t getIP() const;

	void setPort(int port) { m_address.port = static_cast<uint16_t>(port); m_valid = true; }
	int getPort() const { return m_address.port; }

	void setType(int type) { m_type = type; }
	int getType() const { return m_type; }

	void setControlID(uint32_t id) { m_controlID = id; }
	uint32_t getControlID() const { return m_controlID; }

	bool operator==(const ZCom_Address& other) const;

	const ENetAddress& getENetAddress() const { return m_address; }
	ENetAddress& getENetAddress() { return m_address; }

	std::string toString() const;

private:
	ENetAddress m_address;
	bool m_valid;
	int m_type;
	uint32_t m_controlID;
};

#endif // NET_ADDRESS_H
