#ifndef NET_BITSTREAM_H
#define NET_BITSTREAM_H

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cwchar>
#include <climits>

class ZCom_BitStream {
public:
	// Position tracking for save/restore state
	struct BitPos {
		uint16_t bit;  // bit in current byte
		uint16_t pos;  // byte position in array
	};

	ZCom_BitStream();
	ZCom_BitStream(const uint8_t* data, size_t bytes);
	ZCom_BitStream(const ZCom_BitStream& other);  // copy constructor
	~ZCom_BitStream();

	ZCom_BitStream& operator=(const ZCom_BitStream& other);

	// Writing
	void addInt(int val, int bits);
	void addSignedInt(int val, int bits);
	void addInt64(int64_t val, int bits);
	void addBool(bool val);
	void addFloat(float val, int bits);
	void addDouble(double val, int bits);
	void addString(const char* str);
	void addStringW(const wchar_t* str);
	void addBuffer(const char* buf, uint16_t len);
	void addBitStream(ZCom_BitStream* other);

	// Reading
	int getInt(int bits);
	int getSignedInt(int bits);
	int64_t getInt64(int bits);
	bool getBool();
	float getFloat(int bits);
	double getDouble(int bits);
	const char* getStringStatic();
	char* getString();
	uint16_t getStringSize();
	uint16_t getStringLength();
	void getString(char* buf, size_t bufsize);

	// Wide string reading
	uint16_t getStringWLength();
	void getStringW(wchar_t* buf, size_t bufsize);
	const wchar_t* getStringWStatic();

	// Buffer reading
	uint16_t getBuffer(char* buf, uint16_t len);
	uint16_t getBufferMax();

	// BitStream extraction
	ZCom_BitStream* getBitStream(uint32_t bits, bool copyData);

	// Skip methods
	void skipInt(int bits);
	void skipSignedInt(int bits);
	void skipBool();
	void skipFloat(int bits);
	void skipString();
	void skipBuffer(uint16_t len);
	void skipBits(uint32_t amount);

	// State save/restore
	void saveWriteState(BitPos& pos) const;
	void restoreWriteState(const BitPos& pos);
	void saveReadState(BitPos& pos) const;
	void restoreReadState(const BitPos& pos);

	// Stream checks
	bool checkMax(uint32_t bits) const;
	bool checkFull() const;
	bool endOfStream() const;
	uint16_t getSizeHint() const;

	// Serialization
	bool Serialize(char* ptr, uint16_t* size, uint16_t max_size);
	bool Deserialize(char* ptr, uint16_t size);

	// Comparison
	bool isEqual(const ZCom_BitStream& other) const;

	// Other
	ZCom_BitStream* Duplicate();

	// Internal helpers (used by replicators etc.)
	const uint8_t* getData() const { return m_data.data(); }
	size_t getDataLength() const { return (m_writeBit + 7) / 8; }
	size_t getBitCount() const { return m_writeBit; }
	size_t getBitLength() const { return m_writeBit; }
	void resetReadState() { m_readBit = 0; m_readPos = {0, 0}; }
	void resetRead() { m_readBit = 0; m_readPos = {0, 0}; }
	void reset() { m_data.clear(); m_writeBit = 0; m_readBit = 0; m_readPos = {0, 0}; m_fillPos = {0, 0}; }
	void Clear() { m_data.clear(); m_writeBit = 0; m_readBit = 0; m_readPos = {0, 0}; m_fillPos = {0, 0}; }

	// For reading from a prepared buffer
	void assign(const uint8_t* data, size_t bytes);

	// Depths
	static int getDepth() { return 32; }

private:
	void ensureCapacity(size_t neededBits);

	std::vector<uint8_t> m_data;
	size_t m_writeBit;  // current write position in bits
	size_t m_readBit;   // current read position in bits
	BitPos m_fillPos;   // write state for save/restore
	BitPos m_readPos;   // read state for save/restore
	std::string m_lastString; // caches last read string for getStringStatic
	std::wstring m_lastWString; // caches last read wide string
};

#endif // NET_BITSTREAM_H
