#ifndef NET_BITSTREAM_H
#define NET_BITSTREAM_H

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <cassert>

class ZCom_BitStream {
public:
	ZCom_BitStream();
	ZCom_BitStream(const uint8_t* data, size_t bytes);
	~ZCom_BitStream();

	// Writing
	void addInt(int val, int bits);
	void addSignedInt(int val, int bits);
	void addBool(bool val);
	void addFloat(float val, int bits);
	void addString(const char* str);
	void addBitStream(ZCom_BitStream* other);

	// Reading
	int getInt(int bits);
	int getSignedInt(int bits);
	bool getBool();
	float getFloat(int bits);
	const char* getStringStatic();
	char* getString();

	// Other
	ZCom_BitStream* Duplicate();

	// Internal helpers (used by replicators etc.)
	const uint8_t* getData() const { return m_data.data(); }
	size_t getDataLength() const { return (m_writeBit + 7) / 8; }
	size_t getBitLength() const { return m_writeBit; }
	void resetRead() { m_readBit = 0; }
	void reset() { m_data.clear(); m_writeBit = 0; m_readBit = 0; }

	// For reading from a prepared buffer
	void assign(const uint8_t* data, size_t bytes);

	// Depths
	static int getDepth() { return 32; }

private:
	void ensureCapacity(size_t neededBits);

	std::vector<uint8_t> m_data;
	size_t m_writeBit;  // current write position in bits
	size_t m_readBit;   // current read position in bits
	std::string m_lastString; // caches last read string for getStringStatic
};

#endif // NET_BITSTREAM_H