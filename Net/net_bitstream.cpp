#include "net_bitstream.h"
#include <cstring>
#include <algorithm>
#include <stdexcept>

ZCom_BitStream::ZCom_BitStream()
	: m_writeBit(0), m_readBit(0), m_fillPos{0, 0}, m_readPos{0, 0}
{
}

ZCom_BitStream::ZCom_BitStream(const uint8_t* data, size_t bytes)
	: m_data(data, data + bytes), m_writeBit(bytes * 8), m_readBit(0)
	, m_fillPos{static_cast<uint16_t>(bytes * 8), static_cast<uint16_t>(bytes)}
	, m_readPos{0, 0}
{
}

ZCom_BitStream::ZCom_BitStream(const ZCom_BitStream& other)
	: m_data(other.m_data)
	, m_writeBit(other.m_writeBit)
	, m_readBit(other.m_readBit)
	, m_fillPos(other.m_fillPos)
	, m_readPos(other.m_readPos)
	, m_lastString(other.m_lastString)
	, m_lastWString(other.m_lastWString)
{
}

ZCom_BitStream& ZCom_BitStream::operator=(const ZCom_BitStream& other)
{
	m_data = other.m_data;
	m_writeBit = other.m_writeBit;
	m_readBit = other.m_readBit;
	m_fillPos = other.m_fillPos;
	m_readPos = other.m_readPos;
	m_lastString = other.m_lastString;
	m_lastWString = other.m_lastWString;
	return *this;
}

ZCom_BitStream::~ZCom_BitStream()
{
}

void ZCom_BitStream::ensureCapacity(size_t neededBits)
{
	size_t neededBytes = (neededBits + 7) / 8;
	if (neededBytes > m_data.size()) {
		m_data.resize(neededBytes + 64); // grow with some headroom
	}
}

void ZCom_BitStream::addInt(int val, int bits)
{
	if (bits <= 0) return;
	ensureCapacity(m_writeBit + bits);

	for (int i = 0; i < bits; ++i) {
		size_t byteIdx = m_writeBit / 8;
		size_t bitIdx = m_writeBit % 8;
		if (bitIdx == 0 && byteIdx >= m_data.size()) m_data.push_back(0);

		if (val & (1 << i))
			m_data[byteIdx] |= (1 << bitIdx);

		++m_writeBit;
	}
	m_fillPos.bit = static_cast<uint16_t>(m_writeBit % 8);
	m_fillPos.pos = static_cast<uint16_t>(m_writeBit / 8);
}

void ZCom_BitStream::addSignedInt(int val, int bits)
{
	// Write as two's complement
	unsigned int mask = (bits < 32) ? ((1u << bits) - 1) : 0xFFFFFFFFu;
	addInt(static_cast<int>(static_cast<unsigned int>(val) & mask), bits);
}

void ZCom_BitStream::addInt64(int64_t val, int bits)
{
	if (bits <= 0) return;
	ensureCapacity(m_writeBit + bits);

	for (int i = 0; i < bits; ++i) {
		size_t byteIdx = m_writeBit / 8;
		size_t bitIdx = m_writeBit % 8;
		if (bitIdx == 0 && byteIdx >= m_data.size()) m_data.push_back(0);

		uint64_t uval = static_cast<uint64_t>(val);
		if (uval & (1ULL << i))
			m_data[byteIdx] |= (1 << static_cast<int>(bitIdx));

		++m_writeBit;
	}
	m_fillPos.bit = static_cast<uint16_t>(m_writeBit % 8);
	m_fillPos.pos = static_cast<uint16_t>(m_writeBit / 8);
}

int ZCom_BitStream::getInt(int bits)
{
	if (bits <= 0) return 0;
	if (m_readBit + static_cast<size_t>(bits) > m_writeBit) return 0;

	int val = 0;
	for (int i = 0; i < bits; ++i) {
		size_t byteIdx = m_readBit / 8;
		size_t bitIdx = m_readBit % 8;
		if (byteIdx < m_data.size() && (m_data[byteIdx] & (1 << bitIdx)))
			val |= (1 << i);
		++m_readBit;
	}
	m_readPos.bit = static_cast<uint16_t>(m_readBit % 8);
	m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
	return val;
}

int ZCom_BitStream::getSignedInt(int bits)
{
	int val = getInt(bits);
	// Sign extend
	if (bits > 0 && bits < 32 && (val & (1 << (bits - 1))))
		val |= ~((1 << bits) - 1);
	return val;
}

int64_t ZCom_BitStream::getInt64(int bits)
{
	if (bits <= 0) return 0;
	if (m_readBit + static_cast<size_t>(bits) > m_writeBit) return 0;

	uint64_t val = 0;
	for (int i = 0; i < bits; ++i) {
		size_t byteIdx = m_readBit / 8;
		size_t bitIdx = m_readBit % 8;
		if (byteIdx < m_data.size() && (m_data[byteIdx] & (1 << bitIdx)))
			val |= (1ULL << i);
		++m_readBit;
	}
	m_readPos.bit = static_cast<uint16_t>(m_readBit % 8);
	m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
	return static_cast<int64_t>(val);
}

void ZCom_BitStream::addBool(bool val)
{
	addInt(val ? 1 : 0, 1);
}

bool ZCom_BitStream::getBool()
{
	return getInt(1) != 0;
}

void ZCom_BitStream::addFloat(float val, int bits)
{
	// Write float as raw bits (32-bit IEEE 754)
	if (bits >= 32) {
		uint32_t raw;
		memcpy(&raw, &val, sizeof(raw));
		addInt(static_cast<int>(raw), 32);
	} else {
		// Quantize float to integer range for fewer bits
		// Maps [-1.0, 1.0] to [0, 2^bits - 1] for unsigned
		// For other ranges the caller must scale
		int maxVal = (1 << (bits - 1)) - 1;
		int intVal = static_cast<int>(val * maxVal);
		addSignedInt(intVal, bits);
	}
}

float ZCom_BitStream::getFloat(int bits)
{
	if (bits >= 32) {
		uint32_t raw = static_cast<uint32_t>(getInt(32));
		float val;
		memcpy(&val, &raw, sizeof(val));
		return val;
	} else {
		int maxVal = (1 << (bits - 1)) - 1;
		int intVal = getSignedInt(bits);
		return static_cast<float>(intVal) / maxVal;
	}
}

void ZCom_BitStream::addDouble(double val, int bits)
{
	if (bits >= 64) {
		uint64_t raw;
		memcpy(&raw, &val, sizeof(raw));
		addInt64(static_cast<int64_t>(raw), 64);
	} else if (bits >= 32) {
		// Store as float and use 32 bits
		float fval = static_cast<float>(val);
		addFloat(fval, 32);
	} else {
		// Quantize
		int maxVal = (1 << (bits - 1)) - 1;
		int intVal = static_cast<int>(val * maxVal);
		addSignedInt(intVal, bits);
	}
}

double ZCom_BitStream::getDouble(int bits)
{
	if (bits >= 64) {
		uint64_t raw = static_cast<uint64_t>(getInt64(64));
		double val;
		memcpy(&val, &raw, sizeof(val));
		return val;
	} else if (bits >= 32) {
		return static_cast<double>(getFloat(32));
	} else {
		int maxVal = (1 << (bits - 1)) - 1;
		int intVal = getSignedInt(bits);
		return static_cast<double>(intVal) / maxVal;
	}
}

void ZCom_BitStream::addString(const char* str)
{
	if (!str) {
		addInt(0, 16); // empty string
		return;
	}
	size_t len = strlen(str);
	addInt(static_cast<int>(len), 16); // length prefix
	for (size_t i = 0; i < len; ++i)
		addInt(static_cast<unsigned char>(str[i]), 8);
}

const char* ZCom_BitStream::getStringStatic()
{
	int len = getInt(16);
	if (len <= 0) return "";

	m_lastString.clear();
	m_lastString.reserve(len);
	for (int i = 0; i < len; ++i)
		m_lastString += static_cast<char>(getInt(8));

	return m_lastString.c_str();
}

char* ZCom_BitStream::getString()
{
	// Allocates a new string buffer - caller must free
	int len = getInt(16);
	if (len <= 0) {
		char* empty = new char[1];
		empty[0] = '\0';
		return empty;
	}
	char* buf = new char[len + 1];
	for (int i = 0; i < len; ++i)
		buf[i] = static_cast<char>(getInt(8));
	buf[len] = '\0';
	return buf;
}

uint16_t ZCom_BitStream::getStringSize()
{
	size_t saved = m_readBit;
	int len = getInt(16);
	m_readBit = saved;
	m_readPos.bit = static_cast<uint16_t>(m_readBit % 8);
	m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
	return static_cast<uint16_t>(len);
}

uint16_t ZCom_BitStream::getStringLength()
{
	return getStringSize();
}

void ZCom_BitStream::getString(char* buf, size_t bufsize)
{
	int len = getInt(16);
	if (len <= 0) {
		if (bufsize > 0) buf[0] = '\0';
		return;
	}
	size_t copyLen = static_cast<size_t>(len) < bufsize - 1 ? static_cast<size_t>(len) : bufsize - 1;
	for (size_t i = 0; i < copyLen; ++i)
		buf[i] = static_cast<char>(getInt(8));
	buf[copyLen] = '\0';
	// Skip remaining if buffer was too small
	for (size_t i = copyLen; i < static_cast<size_t>(len); ++i)
		getInt(8);
}

void ZCom_BitStream::addStringW(const wchar_t* str)
{
	if (!str) {
		addInt(0, 16);
		return;
	}
	size_t len = wcslen(str);
	addInt(static_cast<int>(len), 16);
	for (size_t i = 0; i < len; ++i)
		addInt(static_cast<int>(str[i]), 16);
}

uint16_t ZCom_BitStream::getStringWLength()
{
	size_t saved = m_readBit;
	int len = getInt(16);
	m_readBit = saved;
	m_readPos.bit = static_cast<uint16_t>(m_readBit % 8);
	m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
	return static_cast<uint16_t>(len);
}

void ZCom_BitStream::getStringW(wchar_t* buf, size_t bufsize)
{
	int len = getInt(16);
	if (len <= 0) {
		if (bufsize > 0) buf[0] = L'\0';
		return;
	}
	size_t copyLen = static_cast<size_t>(len) < bufsize ? static_cast<size_t>(len) : bufsize - 1;
	for (size_t i = 0; i < copyLen; ++i)
		buf[i] = static_cast<wchar_t>(getInt(16));
	buf[copyLen] = L'\0';
	for (size_t i = copyLen; i < static_cast<size_t>(len); ++i)
		getInt(16);
}

const wchar_t* ZCom_BitStream::getStringWStatic()
{
	int len = getInt(16);
	if (len <= 0) return L"";

	m_lastWString.clear();
	for (int i = 0; i < len; ++i)
		m_lastWString += static_cast<wchar_t>(getInt(16));

	return m_lastWString.c_str();
}

void ZCom_BitStream::addBuffer(const char* buf, uint16_t len)
{
	addInt(len, 16);
	for (uint16_t i = 0; i < len; ++i)
		addInt(static_cast<unsigned char>(buf[i]), 8);
}

uint16_t ZCom_BitStream::getBuffer(char* buf, uint16_t len)
{
	uint16_t actualLen = static_cast<uint16_t>(getInt(16));
	uint16_t copyLen = actualLen < len ? actualLen : len;
	for (uint16_t i = 0; i < copyLen; ++i)
		buf[i] = static_cast<char>(getInt(8));
	// Skip remaining bytes
	for (uint16_t i = copyLen; i < actualLen; ++i)
		getInt(8);
	return actualLen;
}

uint16_t ZCom_BitStream::getBufferMax()
{
	// Peek the stored buffer length without consuming it
	if (m_readBit + 16 > m_writeBit) return 0;
	size_t saved = m_readBit;
	uint16_t len = static_cast<uint16_t>(getInt(16));
	m_readBit = saved;
	m_readPos.bit = static_cast<uint16_t>(m_readBit % 8);
	m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
	return len;
}

void ZCom_BitStream::addBitStream(ZCom_BitStream* other)
{
	if (!other) return;
	size_t bits = other->getBitLength();
	addInt(static_cast<int>(bits), 16);
	const uint8_t* data = other->getData();
	size_t bytes = (bits + 7) / 8;
	for (size_t i = 0; i < bytes; ++i)
		addInt(data[i], 8);
}

ZCom_BitStream* ZCom_BitStream::getBitStream(uint32_t bits, bool copyData)
{
	(void)copyData;
	ZCom_BitStream* extracted = new ZCom_BitStream();
	for (uint32_t i = 0; i < bits; ++i) {
		if (getBool())
			extracted->addBool(true);
		else
			extracted->addBool(false);
	}
	extracted->resetReadState();
	return extracted;
}

// ---- Skip methods ----

void ZCom_BitStream::skipInt(int bits)
{
	if (bits <= 0) return;
	m_readBit += static_cast<size_t>(bits);
	if (m_readBit > m_writeBit) m_readBit = m_writeBit;
	m_readPos.bit = static_cast<uint16_t>(m_readBit % 8);
	m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
}

void ZCom_BitStream::skipSignedInt(int bits)
{
	skipInt(bits);
}

void ZCom_BitStream::skipBool()
{
	skipInt(1);
}

void ZCom_BitStream::skipFloat(int bits)
{
	skipInt(bits >= 32 ? 32 : bits);
}

void ZCom_BitStream::skipString()
{
	int len = getInt(16);
	if (len > 0) {
		skipInt(len * 8);
	}
}

void ZCom_BitStream::skipBuffer(uint16_t len)
{
	(void)len;
	// In ZoidCom reference, skipBuffer skips a *stored* buffer by given byte count
	// We need to read the length prefix and skip
	uint16_t actualLen = static_cast<uint16_t>(getInt(16));
	skipInt(actualLen * 8);
}

void ZCom_BitStream::skipBits(uint32_t amount)
{
	m_readBit += amount;
	if (m_readBit > m_writeBit) m_readBit = m_writeBit;
	m_readPos.bit = static_cast<uint16_t>(m_readBit % 8);
	m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
}

// ---- State save/restore ----

void ZCom_BitStream::saveWriteState(BitPos& pos) const
{
	pos.bit = static_cast<uint16_t>(m_writeBit % 8);
	pos.pos = static_cast<uint16_t>(m_writeBit / 8);
}

void ZCom_BitStream::restoreWriteState(const BitPos& pos)
{
	m_fillPos = pos;
	m_writeBit = static_cast<size_t>(pos.pos) * 8 + pos.bit;
}

void ZCom_BitStream::saveReadState(BitPos& pos) const
{
	pos.bit = static_cast<uint16_t>(m_readBit % 8);
	pos.pos = static_cast<uint16_t>(m_readBit / 8);
}

void ZCom_BitStream::restoreReadState(const BitPos& pos)
{
	m_readPos = pos;
	m_readBit = static_cast<size_t>(pos.pos) * 8 + pos.bit;
}

// ---- Stream checks ----

bool ZCom_BitStream::checkMax(uint32_t bits) const
{
	size_t neededBits = m_writeBit + bits;
	size_t maxBits = m_data.size() * 8;
	return neededBits <= maxBits;
}

bool ZCom_BitStream::checkFull() const
{
	return m_writeBit / 8 > m_data.size();
}

bool ZCom_BitStream::endOfStream() const
{
	return m_readBit >= m_writeBit;
}

uint16_t ZCom_BitStream::getSizeHint() const
{
	return static_cast<uint16_t>((m_writeBit + 7) / 8);
}

// ---- Serialization ----

bool ZCom_BitStream::Serialize(char* ptr, uint16_t* size, uint16_t max_size)
{
	uint16_t needed = getSizeHint();
	if (needed > max_size) return false;
	if (ptr && size) {
		memcpy(ptr, m_data.data(), needed);
		*size = needed;
	}
	return true;
}

bool ZCom_BitStream::Deserialize(char* ptr, uint16_t size)
{
	if (!ptr || size == 0) return false;
	m_data.assign(reinterpret_cast<uint8_t*>(ptr), reinterpret_cast<uint8_t*>(ptr) + size);
	m_writeBit = size * 8;
	m_readBit = 0;
	m_fillPos.bit = 0;
	m_fillPos.pos = static_cast<uint16_t>(size);
	m_readPos = {0, 0};
	return true;
}

// ---- Comparison ----

bool ZCom_BitStream::isEqual(const ZCom_BitStream& other) const
{
	size_t ourBytes = (m_writeBit + 7) / 8;
	size_t otherBytes = (other.m_writeBit + 7) / 8;
	if (ourBytes != otherBytes) return false;
	return m_data.size() >= ourBytes && other.m_data.size() >= otherBytes
		? memcmp(m_data.data(), other.m_data.data(), ourBytes) == 0
		: false;
}

// ---- Duplicate ----

ZCom_BitStream* ZCom_BitStream::Duplicate()
{
	ZCom_BitStream* dup = new ZCom_BitStream();
	size_t byteStart = m_readBit / 8;
	if (byteStart < m_data.size()) {
		dup->m_data.assign(m_data.begin() + byteStart, m_data.end());
	}
	dup->m_writeBit = m_writeBit - m_readBit;
	dup->m_readBit = 0;
	dup->m_fillPos.bit = static_cast<uint16_t>(dup->m_writeBit % 8);
	dup->m_fillPos.pos = static_cast<uint16_t>(dup->m_writeBit / 8);
	dup->m_readPos = {0, 0};
	return dup;
}

void ZCom_BitStream::assign(const uint8_t* data, size_t bytes)
{
	m_data.assign(data, data + bytes);
	m_writeBit = bytes * 8;
	m_readBit = 0;
	m_fillPos.bit = 0;
	m_fillPos.pos = static_cast<uint16_t>(bytes);
	m_readPos = {0, 0};
}
