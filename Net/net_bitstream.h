#ifndef NET_BITSTREAM_H
#define NET_BITSTREAM_H

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cwchar>
#include <climits>
#include <memory>
#include "net_types.h"

class ZCom_BitStream {
public:
	// Position tracking for save/restore state
	struct BitPos {
		zU16 bit;  // bit in current byte
		zU16 pos;  // byte position in array
	};

	ZCom_BitStream(zU16 _maxfill = 64);
	ZCom_BitStream(const uint8_t* data, size_t bytes);
	ZCom_BitStream(const ZCom_BitStream& other);  // copy constructor
	~ZCom_BitStream();

	ZCom_BitStream& operator=(const ZCom_BitStream& other);

	// Writing
	bool addInt(zU32 val, zU8 bits);
	bool addSignedInt(zS32 val, zU8 bits);
	void addInt64(int64_t val, int bits);
	bool addBool(bool val);
	bool addFloat(zFloat val, zU8 bits);
	void addDouble(double val, int bits);
	bool addString(const char* str);
	bool addStringW(const wchar_t* str);
	bool addBuffer(const char* buf, zU16 len);
	bool addBitStream(ZCom_BitStream* other, bool _allow_align = false);

	// Reading
	zU32 getInt(zU8 bits);
	zS32 getSignedInt(zU8 bits);
	int64_t getInt64(int bits);
	bool getBool();
	zFloat getFloat(zU8 bits);
	double getDouble(int bits);
	const char* getStringStatic();
	std::string getString();
	zU16 getStringSize();
	zU16 getStringLength();
	void getString(char* buf, zU16 maxsize);

	// Wide string reading
	zU16 getStringWLength();
	void getStringW(wchar_t* buf, zU16 maxsize);
	const wchar_t* getStringWStatic();

	// Buffer reading
	zU16 getBuffer(char* buf, zU16 len);
	zU16 getBufferMax();

	// BitStream extraction
	std::unique_ptr<ZCom_BitStream> getBitStream(zU32 bits, bool _allow_align = false);

	// Skip methods
	void skipInt(zU8 bits);
	void skipSignedInt(zU8 bits);
	void skipBool();
	void skipFloat(zU8 bits);
	void skipString();
	void skipBuffer(zU16 len);
	void skipBits(zU32 amount);

	// State save/restore
	void saveWriteState(BitPos& pos) const;
	void restoreWriteState(const BitPos& pos);
	void saveReadState(BitPos& pos) const;
	void restoreReadState(const BitPos& pos);
	void resetReadState() { m_readBit = 0; m_readPos = {0, 0}; }
	void logReadState();
	void logWriteState();

	// Stream checks
	bool checkMax(zU32 bits) const;
	bool checkFull() const;
	bool endOfStream() const;
	zU16 getSizeHint() const;

	// Over-read error tracking (T3.1). getInt/getInt64 return 0 on a read
	// past end for back-compat; this flag makes such a read observable so
	// processInput can detect protocol desync and close the connection
	// instead of cascading garbage through every following read. The flag
	// is sticky until reset()/Clear()/assign()/clearReadError().
	bool getReadError() const { return m_readError; }
	void clearReadError() { m_readError = false; }

	// Serialization
	bool Serialize(char* ptr, zU16* size, zU16 max_size);
	bool Deserialize(char* ptr, zU16 size);

	// Comparison
	bool isEqual(const ZCom_BitStream& other) const;

	// Other
	std::unique_ptr<ZCom_BitStream> Duplicate() const;
	void* operator new(size_t _size);
	void operator delete(void* _p);

	// Internal helpers (used by replicators etc.)
	const uint8_t* getData() const { return m_data.data(); }
	size_t getDataLength() const { return (m_writeBit + 7) / 8; }
	zU32 getBitCount() const { return static_cast<zU32>(m_writeBit); }
	size_t getBitLength() const { return m_writeBit; }
	void resetRead() { m_readBit = 0; m_readPos = {0, 0}; }
	void reset() { m_data.clear(); m_writeBit = 0; m_readBit = 0; m_readPos = {0, 0}; m_fillPos = {0, 0}; m_readError = false; }
	void Clear() { m_data.clear(); m_writeBit = 0; m_readBit = 0; m_readPos = {0, 0}; m_fillPos = {0, 0}; m_readError = false; }

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
	bool m_readError = false; // set on read past end (T3.1); sticky until reset
};

#endif // NET_BITSTREAM_H
