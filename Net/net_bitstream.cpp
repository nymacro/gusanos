#include "net_bitstream.h"
#include <cstring>
#include <algorithm>
#include <stdexcept>

ZCom_BitStream::ZCom_BitStream()
	: m_writeBit(0), m_readBit(0)
{
}

ZCom_BitStream::ZCom_BitStream(const uint8_t* data, size_t bytes)
	: m_data(data, data + bytes), m_writeBit(bytes * 8), m_readBit(0)
{
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
		if (bitIdx == 0) m_data[byteIdx] = 0; // clear new byte

		if (val & (1 << i))
			m_data[byteIdx] |= (1 << bitIdx);

		++m_writeBit;
	}
}

void ZCom_BitStream::addSignedInt(int val, int bits)
{
	// Write as two's complement
	unsigned int mask = (bits < 32) ? ((1u << bits) - 1) : 0xFFFFFFFFu;
	addInt(static_cast<int>(static_cast<unsigned int>(val) & mask), bits);
}

int ZCom_BitStream::getInt(int bits)
{
	if (bits <= 0) return 0;
	if (m_readBit + bits > m_writeBit) return 0;

	int val = 0;
	for (int i = 0; i < bits; ++i) {
		size_t byteIdx = m_readBit / 8;
		size_t bitIdx = m_readBit % 8;
		if (m_data[byteIdx] & (1 << bitIdx))
			val |= (1 << i);
		++m_readBit;
	}
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

ZCom_BitStream* ZCom_BitStream::Duplicate()
{
	ZCom_BitStream* dup = new ZCom_BitStream();
	size_t byteStart = m_readBit / 8;
	size_t bitOffset = m_readBit % 8;
	if (byteStart < m_data.size()) {
		dup->m_data.assign(m_data.begin() + byteStart, m_data.end());
	}
	dup->m_writeBit = m_writeBit - m_readBit;
	dup->m_readBit = bitOffset;
	return dup;
}

void ZCom_BitStream::assign(const uint8_t* data, size_t bytes)
{
	m_data.assign(data, data + bytes);
	m_writeBit = bytes * 8;
	m_readBit = 0;
}