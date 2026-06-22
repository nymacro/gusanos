#include "net_bitstream.h"
#include <stdexcept>
#include <cstring>
#include <cmath>

NetBitStream::NetBitStream()
	: m_writeCursor(0)
	, m_readCursor(0)
{
}

NetBitStream::NetBitStream(const void* data, size_t byteLen)
	: m_buffer(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + byteLen)
	, m_writeCursor(byteLen * 8)
	, m_readCursor(0)
{
}

NetBitStream::NetBitStream(const NetBitStream& other)
	: m_buffer(other.m_buffer)
	, m_writeCursor(other.m_writeCursor)
	, m_readCursor(other.m_readCursor)
	, m_readString(other.m_readString)
{
}

NetBitStream& NetBitStream::operator=(const NetBitStream& other)
{
	if (this != &other)
	{
		m_buffer = other.m_buffer;
		m_writeCursor = other.m_writeCursor;
		m_readCursor = other.m_readCursor;
		m_readString = other.m_readString;
	}
	return *this;
}

void NetBitStream::reset()
{
	m_readCursor = 0;
	m_readString.clear();
}

void NetBitStream::clear()
{
	m_buffer.clear();
	m_writeCursor = 0;
	m_readCursor = 0;
	m_readString.clear();
}

void NetBitStream::ensureCapacity(size_t neededBits)
{
	size_t neededBytes = (neededBits + 7) / 8;
	if (neededBytes > m_buffer.size())
	{
		// Grow to at least 1.5x, but at least to neededBytes
		size_t newSize = m_buffer.size();
		if (newSize < 64)
			newSize = 64;
		while (newSize < neededBytes)
			newSize = newSize + (newSize / 2) + 8;
		m_buffer.resize(newSize);
	}
}

void NetBitStream::writeBits(uint32_t val, int bits)
{
	if (bits <= 0) return;
	if (bits > 32) bits = 32; // clamp
	
	// Mask to the requested bit width
	if (bits < 32)
		val &= (1u << bits) - 1;
	
	ensureCapacity(m_writeCursor + bits);
	
	for (int i = 0; i < bits; ++i)
	{
		size_t byteIdx = (m_writeCursor) / 8;
		int bitIdx = (m_writeCursor) % 8;
		
		if (val & (1u << (bits - 1 - i)))
			m_buffer[byteIdx] |= (1 << (7 - bitIdx));
		else
			m_buffer[byteIdx] &= ~(1 << (7 - bitIdx));
		
		++m_writeCursor;
	}
}

uint32_t NetBitStream::readBits(int bits)
{
	if (bits <= 0) return 0;
	if (bits > 32) bits = 32;
	
	if (m_readCursor + bits > m_writeCursor)
	{
		// Not enough data — return 0 as the ZoidCom stubs do
		m_readCursor = m_writeCursor;
		return 0;
	}
	
	uint32_t val = 0;
	for (int i = 0; i < bits; ++i)
	{
		size_t byteIdx = m_readCursor / 8;
		int bitIdx = m_readCursor % 8;
		
		val <<= 1;
		if (m_buffer[byteIdx] & (1 << (7 - bitIdx)))
			val |= 1;
		
		++m_readCursor;
	}
	
	return val;
}

void NetBitStream::addInt(int32_t val, int bits)
{
	if (bits <= 0) bits = 1;
	if (bits > 32) bits = 32;
	writeBits(static_cast<uint32_t>(val), bits);
}

int32_t NetBitStream::getInt(int bits)
{
	if (bits <= 0) bits = 1;
	if (bits > 32) bits = 32;
	return static_cast<int32_t>(readBits(bits));
}

void NetBitStream::addSignedInt(int32_t val, int bits)
{
	// Zigzag encoding: map signed to unsigned
	uint32_t encoded;
	if (val < 0)
		encoded = static_cast<uint32_t>((-val) << 1) | 1u;
	else
		encoded = static_cast<uint32_t>(val) << 1;
	addInt(static_cast<int32_t>(encoded), bits + 1);
}

int32_t NetBitStream::getSignedInt(int bits)
{
	uint32_t encoded = static_cast<uint32_t>(getInt(bits + 1));
	if (encoded & 1u)
		return -static_cast<int32_t>(encoded >> 1);
	else
		return static_cast<int32_t>(encoded >> 1);
}

void NetBitStream::addFloat(float val, int bits)
{
	// Encode as fixed-point: map [-1, 1) into a signed int range
	if (bits <= 0) bits = 16;
	if (bits > 32) bits = 32;
	
	int32_t fixed;
	if (val >= 0.0f)
		fixed = static_cast<int32_t>(val * (1 << (bits - 1)));
	else
		fixed = static_cast<int32_t>(val * (1 << (bits - 1))) - 1;
	
	addSignedInt(fixed, bits);
}

float NetBitStream::getFloat(int bits)
{
	if (bits <= 0) bits = 16;
	if (bits > 32) bits = 32;
	
	int32_t fixed = getSignedInt(bits);
	return static_cast<float>(fixed) / (1 << (bits - 1));
}

void NetBitStream::addString(const char* str)
{
	size_t len = str ? std::strlen(str) : 0;
	if (len > 65535) len = 65535; // cap
	
	addInt(static_cast<int32_t>(len), 16);
	for (size_t i = 0; i < len; ++i)
		addInt(static_cast<unsigned char>(str[i]), 8);
}

const char* NetBitStream::getStringStatic()
{
	int32_t len = getInt(16);
	if (len < 0) { len = 0; }
	
	m_readString.clear();
	m_readString.reserve(static_cast<size_t>(len) + 1);
	for (int32_t i = 0; i < len; ++i)
	{
		unsigned char c = static_cast<unsigned char>(getInt(8));
		m_readString.push_back(static_cast<char>(c));
	}
	m_readString.push_back('\0');
	
	return m_readString.c_str();
}

char* NetBitStream::getString()
{
	const char* s = getStringStatic();
	if (!s || *s == '\0')
	{
		char* empty = new char[1];
		empty[0] = '\0';
		return empty;
	}
	
	size_t len = m_readString.size();
	char* copy = new char[len];
	std::memcpy(copy, s, len);
	return copy;
}

void NetBitStream::addBitStream(const NetBitStream* other)
{
	if (!other) return;
	
	size_t otherBits = other->getBitLength();
	size_t otherBytes = (otherBits + 7) / 8;
	
	ensureCapacity(m_writeCursor + otherBits);
	for (size_t i = 0; i < otherBytes; ++i)
	{
		auto byte = (i < other->m_buffer.size()) ? other->m_buffer[i] : 0;
		addInt(byte, 8);
	}
	// Handle partial last byte's padding bits
	size_t fullBits = otherBytes * 8;
	if (fullBits > otherBits)
		m_writeCursor -= (fullBits - otherBits);
}

NetBitStream* NetBitStream::Duplicate() const
{
	return new NetBitStream(*this);
}