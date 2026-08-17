#include "net_bitstream.h"
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <new>
#include <memory>

ZCom_BitStream::ZCom_BitStream(zU16 _maxfill) : m_writeBit(0), m_readBit(0), m_fillPos{0, 0}, m_readPos{0, 0} {
	if (_maxfill)
		m_data.reserve(_maxfill);
}

ZCom_BitStream::ZCom_BitStream(const uint8_t *data, size_t bytes)
	: m_data(data, data + bytes), m_writeBit(bytes * 8), m_readBit(0),
	  // m_fillPos.bit is the sub-byte offset (invariant [0,7]), not total bits.
	  m_fillPos{0, static_cast<uint16_t>(bytes)}, m_readPos{0, 0} {}

ZCom_BitStream::ZCom_BitStream(const ZCom_BitStream &other)
	: m_data(other.m_data), m_writeBit(other.m_writeBit), m_readBit(other.m_readBit), m_fillPos(other.m_fillPos),
	  m_readPos(other.m_readPos), m_lastString(other.m_lastString), m_lastWString(other.m_lastWString),
	  m_readError(other.m_readError) {}

ZCom_BitStream &ZCom_BitStream::operator=(const ZCom_BitStream &other) {
	m_data = other.m_data;
	m_writeBit = other.m_writeBit;
	m_readBit = other.m_readBit;
	m_fillPos = other.m_fillPos;
	m_readPos = other.m_readPos;
	m_lastString = other.m_lastString;
	m_lastWString = other.m_lastWString;
	m_readError = other.m_readError;
	return *this;
}

ZCom_BitStream::~ZCom_BitStream() {}

void ZCom_BitStream::ensureCapacity(size_t neededBits) {
	size_t neededBytes = (neededBits + 7) / 8;
	if (neededBytes > m_data.size()) {
		// Geometric (doubling) growth: building a large stream via repeated
		// addInt previously did ~N/64 realloc-copies (fixed +64 headroom per
		// grow). Doubling amortises to O(log n) reallocs. m_writeBit — not
		// m_data.size() — remains the authoritative length (getDataLength()).
		size_t newCap = m_data.size();
		if (newCap < 64)
			newCap = 64;
		while (newCap < neededBytes)
			newCap *= 2;
		m_data.resize(newCap);
	}
}

bool ZCom_BitStream::addInt(zU32 val, zU8 bits) {
	if (bits == 0)
		return true;
	if (bits > 32)
		bits = 32; // addInt carries at most 32 bits
	ensureCapacity(m_writeBit + bits);

	// Masked word write (was a per-bit loop, ~8 ops/bit on the hottest path:
	// every replicator x every node x every tick). Little-endian bit packing
	// lets the value OR into a byte-aligned window with a sub-byte shift. OR
	// (not assign) preserves bits a restoreWriteState rewind may have left,
	// matching the original per-bit `|=` semantics.
	size_t byteOffset = m_writeBit / 8;
	unsigned bitOffset = static_cast<unsigned>(m_writeBit % 8);
	uint64_t mask = (bits < 32) ? ((1ULL << bits) - 1) : 0xFFFFFFFFULL;
	size_t span = (bitOffset + bits + 7) / 8; // bytes touched, <= 5
	uint64_t cur = 0;
	std::memcpy(&cur, &m_data[byteOffset], span);
	cur |= (static_cast<uint64_t>(val) & mask) << bitOffset;
	std::memcpy(&m_data[byteOffset], &cur, span);

	m_writeBit += bits;
	m_fillPos.bit = static_cast<uint16_t>(m_writeBit % 8);
	m_fillPos.pos = static_cast<uint16_t>(m_writeBit / 8);
	return true;
}

bool ZCom_BitStream::addSignedInt(zS32 val, zU8 bits) {
	// Write as two's complement. `(1u << bits)` for bits >= 32 is UB; the
	// ternary below already special-cases bits==32, but bits > 32 would still
	// fall through to addInt's `1u << i` UB. Clamp the legal range.
	if (bits > 32)
		bits = 32;
	zU32 mask = (bits < 32) ? ((1u << bits) - 1) : 0xFFFFFFFFu;
	addInt(static_cast<zU32>(val) & mask, bits);
	return true;
}

void ZCom_BitStream::addInt64(int64_t val, int bits) {
	if (bits <= 0)
		return;
	if (bits > 64)
		bits = 64;
	// Delegate to the 32-bit masked-word path in two contiguous halves. A
	// single 64-bit window could span up to 9 bytes (64 bits + sub-byte
	// offset), overflowing a uint64; splitting keeps the window <= 5 bytes.
	// The bit layout is identical (contiguous little-endian).
	uint64_t uval = static_cast<uint64_t>(val);
	addInt(static_cast<zU32>(uval & 0xFFFFFFFFu), static_cast<zU8>(std::min(bits, 32)));
	if (bits > 32)
		addInt(static_cast<zU32>((uval >> 32) & 0xFFFFFFFFu), static_cast<zU8>(bits - 32));
}

zU32 ZCom_BitStream::getInt(zU8 bits) {
	if (bits == 0)
		return 0;
	if (bits > 32)
		bits = 32;
	if (m_readBit + static_cast<size_t>(bits) > m_writeBit) {
		m_readError = true; // over-read: make the desync observable (T3.1)
		return 0;
	}

	// Masked word read (was a per-bit loop on the hottest replicator/event
	// path). Read a byte-aligned window, shift out the sub-byte offset, mask.
	size_t byteOffset = m_readBit / 8;
	unsigned bitOffset = static_cast<unsigned>(m_readBit % 8);
	uint64_t cur = 0;
	size_t avail = (byteOffset < m_data.size()) ? (m_data.size() - byteOffset) : 0;
	if (avail > sizeof(uint64_t))
		avail = sizeof(uint64_t);
	size_t span = (bitOffset + bits + 7) / 8;
	size_t rd = (span < avail) ? span : avail;
	if (rd)
		std::memcpy(&cur, &m_data[byteOffset], rd);
	uint64_t mask = (bits < 32) ? ((1ULL << bits) - 1) : 0xFFFFFFFFULL;
	zU32 val = static_cast<zU32>((cur >> bitOffset) & mask);

	m_readBit += bits;
	m_readPos.bit = static_cast<uint16_t>(m_readBit % 8);
	m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
	return val;
}

zS32 ZCom_BitStream::getSignedInt(zU8 bits) {
	zS32 val = static_cast<zS32>(getInt(bits));
	// Sign extend. `1 << bits` is UB for bits==31 (shift into the sign bit),
	// so derive the fill mask from `1 << (bits-1)` (shift <= 30). The guard
	// already guarantees bit (bits-1) is set, so filling from bit (bits-1)
	// is equivalent to filling from bit `bits`.
	if (bits > 0 && bits < 32) {
		zS32 signbit = 1 << (bits - 1);
		if (val & signbit)
			val |= ~(signbit - 1);
	}
	return val;
}

int64_t ZCom_BitStream::getInt64(int bits) {
	if (bits <= 0)
		return 0;
	if (bits > 64)
		bits = 64;
	if (m_readBit + static_cast<size_t>(bits) > m_writeBit) {
		m_readError = true; // over-read: make the desync observable (T3.1)
		return 0;
	}
	// Two contiguous 32-bit reads (see addInt64). The upfront over-read check
	// guarantees both sub-reads fit, so neither sets m_readError spuriously.
	uint64_t lo = getInt(static_cast<zU8>(std::min(bits, 32)));
	uint64_t hi = (bits > 32) ? (static_cast<uint64_t>(getInt(static_cast<zU8>(bits - 32))) << 32) : 0;
	return static_cast<int64_t>(lo | hi);
}

bool ZCom_BitStream::addBool(bool val) {
	addInt(val ? 1 : 0, 1);
	return true;
}

bool ZCom_BitStream::getBool() {
	return getInt(1) != 0;
}

bool ZCom_BitStream::addFloat(zFloat val, zU8 bits) {
	// Write float as raw bits (32-bit IEEE 754)
	if (bits >= 32) {
		uint32_t raw;
		memcpy(&raw, &val, sizeof(raw));
		addInt(raw, 32);
	} else {
		// Fixed-point quantization using `bits` bits.
		// The float is assumed to lie in [-1.0, 1.0]; it is scaled by
		// maxVal = 2^(bits-1) - 1 and stored as a SIGNED integer in the
		// range [-maxVal, +maxVal]. getFloat applies the inverse scaling.
		// Values outside [-1.0, 1.0] OVERFLOW this signed range (the integer
		// wraps/aliases through addSignedInt), so callers replicating
		// arbitrary-magnitude floats with bits < 32 must pre-scale the value
		// into [-1, 1] first. For full IEEE-754 fidelity use bits >= 32.
		if (bits < 2)
			bits = 2; // <2: shift-by-negative UB (bits==0) and maxVal==0 → NaN (bits==1); keep add/get in sync
		int maxVal = (1 << (bits - 1)) - 1;
		int intVal = static_cast<int>(val * maxVal);
		addSignedInt(intVal, bits);
	}
	return true;
}

zFloat ZCom_BitStream::getFloat(zU8 bits) {
	if (bits >= 32) {
		uint32_t raw = static_cast<uint32_t>(getInt(32));
		float val;
		memcpy(&val, &raw, sizeof(val));
		return val;
	} else {
		// Inverse of the addFloat quantization above: recover the signed
		// integer and divide by maxVal = 2^(bits-1) - 1 to map back to [-1,1].
		if (bits < 2)
			bits = 2; // <2: shift-by-negative UB / maxVal==0 → NaN; matches addFloat
		int maxVal = (1 << (bits - 1)) - 1;
		int intVal = getSignedInt(bits);
		return static_cast<float>(intVal) / maxVal;
	}
}

void ZCom_BitStream::addDouble(double val, int bits) {
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
		if (bits < 2)
			bits = 2; // <2: shift-by-negative UB / maxVal==0 → NaN; matches getDouble
		int maxVal = (1 << (bits - 1)) - 1;
		int intVal = static_cast<int>(val * maxVal);
		addSignedInt(intVal, bits);
	}
}

double ZCom_BitStream::getDouble(int bits) {
	if (bits >= 64) {
		uint64_t raw = static_cast<uint64_t>(getInt64(64));
		double val;
		memcpy(&val, &raw, sizeof(val));
		return val;
	} else if (bits >= 32) {
		return static_cast<double>(getFloat(32));
	} else {
		if (bits < 2)
			bits = 2; // <2: shift-by-negative UB / maxVal==0 → NaN; matches addDouble
		int maxVal = (1 << (bits - 1)) - 1;
		int intVal = getSignedInt(bits);
		return static_cast<double>(intVal) / maxVal;
	}
}

bool ZCom_BitStream::addString(const char *str) {
	if (!str) {
		addInt(0, 16); // empty string (no terminator written for empty)
		return true;
	}
	size_t len = strlen(str);
	// Length prefix is 16 bits and counts the null terminator; >= 65535 chars
	// would wrap the prefix to 0 while the payload writes every char,
	// desyncing the reader. Refuse instead.
	if (len + 1 > 0xFFFF)
		return false;
	addInt(static_cast<int>(len + 1), 16); // length includes null terminator (per spec §6.7)
	// Byte-aligned fast path: memcpy the body + null terminator in one shot
	// (was a per-char addInt(c,8) loop on the announce/event string path).
	// Bit-identical to the loop when the write head is byte-aligned.
	if ((m_writeBit & 7) == 0) {
		size_t bytes = len + 1; // body + null terminator
		ensureCapacity(m_writeBit + bytes * 8);
		std::memcpy(&m_data[m_writeBit / 8], str, len);
		m_data[m_writeBit / 8 + len] = 0; // null terminator
		m_writeBit += bytes * 8;
		m_fillPos.bit = 0;
		m_fillPos.pos = static_cast<uint16_t>(m_writeBit / 8);
		return true;
	}
	for (size_t i = 0; i < len; ++i)
		addInt(static_cast<unsigned char>(str[i]), 8);
	addInt(0, 8); // null terminator
	return true;
}

const char *ZCom_BitStream::getStringStatic() {
	int len = getInt(16);
	if (len <= 0)
		return "";

	// Byte-aligned fast path: bulk-copy the len bytes (incl. null terminator)
	// when the read head is aligned and enough bits remain. Bit-identical to
	// the per-byte getInt(8) loop.
	if ((m_readBit & 7) == 0 && m_readBit + static_cast<size_t>(len) * 8 <= m_writeBit) {
		m_lastString.assign(reinterpret_cast<const char *>(&m_data[m_readBit / 8]), static_cast<size_t>(len));
		m_readBit += static_cast<size_t>(len) * 8;
		m_readPos.bit = 0;
		m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
		return m_lastString.c_str();
	}

	m_lastString.clear();
	m_lastString.reserve(len);
	for (int i = 0; i < len; ++i)
		m_lastString += static_cast<char>(getInt(8));

	return m_lastString.c_str();
}

std::string ZCom_BitStream::getString() {
	// Returns the string by value (RAII; no caller-managed buffer).
	int len = getInt(16);
	if (len <= 0) {
		return std::string();
	}
	// Byte-aligned fast path: bulk-copy the len bytes (incl. null terminator)
	// when the read head is aligned and enough bits remain. Bit-identical to
	// the per-byte getInt(8) loop; the trailing-null strip is unchanged.
	if ((m_readBit & 7) == 0 && m_readBit + static_cast<size_t>(len) * 8 <= m_writeBit) {
		std::string buf(reinterpret_cast<const char *>(&m_data[m_readBit / 8]), static_cast<size_t>(len));
		m_readBit += static_cast<size_t>(len) * 8;
		m_readPos.bit = 0;
		m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
		if (!buf.empty() && buf.back() == '\0')
			buf.pop_back();
		return buf;
	}
	std::string buf;
	buf.reserve(len);
	for (int i = 0; i < len; ++i)
		buf += static_cast<char>(getInt(8));
	// The stored length includes a null terminator (see addString); strip it
	// so the returned std::string holds just the text.
	if (!buf.empty() && buf.back() == '\0')
		buf.pop_back();
	return buf;
}

uint16_t ZCom_BitStream::getStringSize() {
	size_t saved = m_readBit;
	int len = getInt(16);
	m_readBit = saved;
	m_readPos.bit = static_cast<uint16_t>(m_readBit % 8);
	m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
	return static_cast<uint16_t>(len);
}

uint16_t ZCom_BitStream::getStringLength() {
	return getStringSize();
}

void ZCom_BitStream::getString(char *buf, zU16 bufsize) {
	int len = getInt(16);
	if (len <= 0) {
		if (bufsize > 0 && buf)
			buf[0] = '\0';
		return;
	}
	// Byte-aligned fast path: when aligned and the whole payload fits, copy
	// the caller's portion via memcpy and advance past the rest in one step.
	// Bit-identical to the per-byte getInt(8) loops (including the skip).
	if ((m_readBit & 7) == 0 && m_readBit + static_cast<size_t>(len) * 8 <= m_writeBit) {
		size_t avail = (bufsize > 0) ? static_cast<size_t>(bufsize) - 1 : 0;
		size_t copyLen = static_cast<size_t>(len) < avail ? static_cast<size_t>(len) : avail;
		if (copyLen > 0 && buf)
			std::memcpy(buf, &m_data[m_readBit / 8], copyLen);
		if (bufsize > 0 && buf)
			buf[copyLen] = '\0';
		m_readBit += static_cast<size_t>(len) * 8;
		m_readPos.bit = 0;
		m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
		return;
	}
	if (bufsize == 0) {
		for (int i = 0; i < len; ++i)
			getInt(8);
		return;
	}
	size_t avail = static_cast<size_t>(bufsize) - 1;
	size_t copyLen = static_cast<size_t>(len) < avail ? static_cast<size_t>(len) : avail;
	for (size_t i = 0; i < copyLen; ++i)
		buf[i] = static_cast<char>(getInt(8));
	buf[copyLen] = '\0';
	// Skip remaining if buffer was too small
	for (size_t i = copyLen; i < static_cast<size_t>(len); ++i)
		getInt(8);
}

bool ZCom_BitStream::addStringW(const wchar_t *str) {
	if (!str) {
		addInt(0, 16);
		return true;
	}
	size_t len = wcslen(str);
	if (len + 1 > 0xFFFF) // 16-bit length prefix would wrap → reader desync
		return false;
	addInt(static_cast<int>(len + 1), 16); // length includes null terminator
	for (size_t i = 0; i < len; ++i)
		addInt(static_cast<int>(str[i]), 16);
	addInt(0, 16); // null terminator
	return true;
}

uint16_t ZCom_BitStream::getStringWLength() {
	size_t saved = m_readBit;
	int len = getInt(16);
	m_readBit = saved;
	m_readPos.bit = static_cast<uint16_t>(m_readBit % 8);
	m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
	return static_cast<uint16_t>(len);
}

void ZCom_BitStream::getStringW(wchar_t *buf, zU16 bufsize) {
	int len = getInt(16);
	if (len <= 0) {
		if (bufsize > 0 && buf)
			buf[0] = L'\0';
		return;
	}
	if (bufsize == 0) {
		for (int i = 0; i < len; ++i)
			getInt(16);
		return;
	}
	size_t avail = static_cast<size_t>(bufsize) - 1;
	size_t copyLen = static_cast<size_t>(len) < avail ? static_cast<size_t>(len) : avail;
	for (size_t i = 0; i < copyLen; ++i)
		buf[i] = static_cast<wchar_t>(getInt(16));
	buf[copyLen] = L'\0';
	for (size_t i = copyLen; i < static_cast<size_t>(len); ++i)
		getInt(16);
}

const wchar_t *ZCom_BitStream::getStringWStatic() {
	int len = getInt(16);
	if (len <= 0)
		return L"";

	m_lastWString.clear();
	for (int i = 0; i < len; ++i)
		m_lastWString += static_cast<wchar_t>(getInt(16));

	return m_lastWString.c_str();
}

bool ZCom_BitStream::addBuffer(const char *buf, zU16 len) {
	addInt(len, 16);
	// Byte-aligned fast path: memcpy the payload in one shot (was a
	// per-byte addInt(c,8) loop). Bit-identical when the write head is aligned.
	if ((m_writeBit & 7) == 0 && len > 0) {
		ensureCapacity(m_writeBit + static_cast<size_t>(len) * 8);
		std::memcpy(&m_data[m_writeBit / 8], buf, len);
		m_writeBit += static_cast<size_t>(len) * 8;
		m_fillPos.bit = 0;
		m_fillPos.pos = static_cast<uint16_t>(m_writeBit / 8);
		return true;
	}
	for (zU16 i = 0; i < len; ++i)
		addInt(static_cast<unsigned char>(buf[i]), 8);
	return true;
}

uint16_t ZCom_BitStream::getBuffer(char *buf, uint16_t len) {
	uint16_t actualLen = static_cast<uint16_t>(getInt(16));
	uint16_t copyLen = actualLen < len ? actualLen : len;
	// Byte-aligned fast path: when aligned and the whole buffer fits, memcpy
	// the caller's portion and advance past the rest in one step. Bit-identical
	// to the per-byte getInt(8) loops (including the skip).
	if ((m_readBit & 7) == 0 && m_readBit + static_cast<size_t>(actualLen) * 8 <= m_writeBit) {
		if (copyLen > 0)
			std::memcpy(buf, &m_data[m_readBit / 8], copyLen);
		m_readBit += static_cast<size_t>(actualLen) * 8;
		m_readPos.bit = 0;
		m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
		return actualLen;
	}
	for (uint16_t i = 0; i < copyLen; ++i)
		buf[i] = static_cast<char>(getInt(8));
	// Skip remaining bytes
	for (uint16_t i = copyLen; i < actualLen; ++i)
		getInt(8);
	return actualLen;
}

uint16_t ZCom_BitStream::getBufferMax() {
	// Peek the stored buffer length without consuming it
	if (m_readBit + 16 > m_writeBit)
		return 0;
	size_t saved = m_readBit;
	uint16_t len = static_cast<uint16_t>(getInt(16));
	m_readBit = saved;
	m_readPos.bit = static_cast<uint16_t>(m_readBit % 8);
	m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
	return len;
}

bool ZCom_BitStream::addBitStream(ZCom_BitStream *other, bool _allow_align) {
	(void)_allow_align;
	if (!other)
		return false;
	// Self-embedding reads and writes the same buffer. Route through a
	// snapshot so source and destination never alias — the byte-aligned
	// fast path's memcpy and the per-bit loop otherwise mutate the very
	// stream they append to. Content is unchanged (the direct path also
	// resets the read head first, so the snapshot spans the whole stream).
	if (other == this) {
		this->resetReadState();
		auto snap = this->Duplicate();
		return addBitStream(snap.get(), _allow_align);
	}
	// Inline the source stream's written bits directly (no length prefix),
	// matching original Zoidcom semantics: addBitStream embeds bits that the
	// receiver reads back with getBitStream(bits) or direct getInt/getBool
	// calls — the application tracks how many bits to read. A length prefix
	// would desynchronize readers that consume the embedded bits directly
	// (e.g. NetWorm::sendWeaponMessage -> recieveMessage, LuaEvent payloads).
	other->resetReadState();
	zU32 n = other->getBitCount();
	if (n == 0)
		return true;

	// Byte-aligned fast path (A8): after resetReadState the source read head is
	// at bit 0, so when the destination write head is also byte-aligned we can
	// memcpy whole bytes and finish the <8 trailing bits with the bit loop.
	// Bit-identical to the per-bit loop below; avoids the per-bit
	// addBool/getBool overhead for large embedded streams (file transfer /
	// announce payloads). The invariant m_data.size() >= (m_writeBit+7)/8
	// guarantees the source has >= n/8 bytes and ensureCapacity guarantees the
	// destination has room.
	if ((m_writeBit & 7) == 0) {
		size_t fullBytes = n / 8;
		if (fullBytes > 0) {
			ensureCapacity(m_writeBit + fullBytes * 8);
			std::memcpy(&m_data[m_writeBit / 8], other->m_data.data(), fullBytes);
			m_writeBit += fullBytes * 8;
			other->m_readBit += fullBytes * 8;
			n -= static_cast<zU32>(fullBytes * 8);
			m_fillPos.bit = static_cast<uint16_t>(m_writeBit % 8);
			m_fillPos.pos = static_cast<uint16_t>(m_writeBit / 8);
			other->m_readPos.bit = static_cast<uint16_t>(other->m_readBit % 8);
			other->m_readPos.pos = static_cast<uint16_t>(other->m_readBit / 8);
		}
		for (zU32 i = 0; i < n; ++i)
			addBool(other->getBool());
		return true;
	}

	// Unaligned destination: per-bit copy.
	for (zU32 i = 0; i < n; ++i)
		addBool(other->getBool());
	return true;
}

std::unique_ptr<ZCom_BitStream> ZCom_BitStream::getBitStream(uint32_t bits, bool copyData) {
	(void)copyData;
	auto extracted = std::make_unique<ZCom_BitStream>();
	if (bits == 0) {
		extracted->resetReadState();
		return extracted;
	}
	// Byte-aligned fast path: bulk-copy whole bytes, finish the <8 trailing
	// bits with the bit loop. Bit-identical to the per-bit copy when the read
	// head is aligned; only copies bytes that actually fit (an over-read falls
	// through to the per-bit loop, which sets m_readError like getBool).
	if ((m_readBit & 7) == 0) {
		size_t fullBytes = bits / 8;
		if (fullBytes > 0 && m_readBit + fullBytes * 8 <= m_writeBit) {
			extracted->ensureCapacity(fullBytes * 8);
			std::memcpy(&extracted->m_data[0], &m_data[m_readBit / 8], fullBytes);
			extracted->m_writeBit = fullBytes * 8;
			extracted->m_fillPos.pos = static_cast<uint16_t>(fullBytes);
			extracted->m_fillPos.bit = 0;
			m_readBit += fullBytes * 8;
			m_readPos.bit = 0;
			m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
			bits -= static_cast<uint32_t>(fullBytes * 8);
		}
	}
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

void ZCom_BitStream::skipInt(zU8 bits) {
	if (bits <= 0)
		return;
	if (m_readBit + static_cast<size_t>(bits) > m_writeBit) {
		m_readError = true; // over-read: make the desync observable (T3.1)
		m_readBit = m_writeBit;
	} else {
		m_readBit += static_cast<size_t>(bits);
	}
	m_readPos.bit = static_cast<uint16_t>(m_readBit % 8);
	m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
}

void ZCom_BitStream::skipSignedInt(zU8 bits) {
	skipInt(bits);
}

void ZCom_BitStream::skipBool() {
	skipInt(1);
}

void ZCom_BitStream::skipFloat(zU8 bits) {
	skipInt(bits >= 32 ? 32 : bits);
}

void ZCom_BitStream::skipString() {
	int len = getInt(16);
	if (len > 0) {
		skipBits(len * 8);
	}
}

void ZCom_BitStream::skipBuffer() {
	// Length-prefixed model: read the stored 16-bit byte count, then skip
	// that many bytes. (Reference skipBuffer(zU16) skips a caller-supplied
	// count with no stored prefix — see the addBuffer deviation note.)
	uint16_t actualLen = static_cast<uint16_t>(getInt(16));
	skipBits(actualLen * 8);
}

void ZCom_BitStream::skipBits(uint32_t amount) {
	if (m_readBit + amount > m_writeBit) {
		m_readError = true; // over-read: make the desync observable (T3.1)
		m_readBit = m_writeBit;
	} else {
		m_readBit += amount;
	}
	m_readPos.bit = static_cast<uint16_t>(m_readBit % 8);
	m_readPos.pos = static_cast<uint16_t>(m_readBit / 8);
}

// ---- State save/restore ----

void ZCom_BitStream::saveWriteState(BitPos &pos) const {
	pos.bit = static_cast<uint16_t>(m_writeBit % 8);
	pos.pos = static_cast<uint16_t>(m_writeBit / 8);
}

void ZCom_BitStream::restoreWriteState(const BitPos &pos) {
	m_fillPos = pos;
	m_writeBit = static_cast<size_t>(pos.pos) * 8 + pos.bit;
	// Truncate data beyond the restored position
	size_t neededBytes = (m_writeBit + 7) / 8;
	if (m_data.size() > neededBytes)
		m_data.resize(neededBytes);
}

void ZCom_BitStream::saveReadState(BitPos &pos) const {
	pos.bit = static_cast<uint16_t>(m_readBit % 8);
	pos.pos = static_cast<uint16_t>(m_readBit / 8);
}

void ZCom_BitStream::restoreReadState(const BitPos &pos) {
	m_readPos = pos;
	m_readBit = static_cast<size_t>(pos.pos) * 8 + pos.bit;
}

// ---- Stream checks ----

bool ZCom_BitStream::checkMax(uint32_t bits) const {
	size_t neededBits = m_writeBit + bits;
	size_t maxBits = m_data.size() * 8;
	return neededBits <= maxBits;
}

bool ZCom_BitStream::checkFull() const {
	return m_writeBit / 8 > m_data.size();
}

bool ZCom_BitStream::endOfStream() const {
	return m_readBit >= m_writeBit;
}

uint16_t ZCom_BitStream::getSizeHint() const {
	return static_cast<uint16_t>((m_writeBit + 7) / 8);
}

// ---- Serialization ----

bool ZCom_BitStream::Serialize(char *ptr, uint16_t *size, uint16_t max_size) {
	uint16_t needed = getSizeHint();
	if (needed > max_size)
		return false;
	if (ptr && size) {
		memcpy(ptr, m_data.data(), needed);
		*size = needed;
	}
	return true;
}

bool ZCom_BitStream::Deserialize(char *ptr, uint16_t size) {
	if (!ptr || size == 0)
		return false;
	m_data.assign(reinterpret_cast<uint8_t *>(ptr), reinterpret_cast<uint8_t *>(ptr) + size);
	m_writeBit = size * 8;
	m_readBit = 0;
	m_fillPos.bit = 0;
	m_fillPos.pos = static_cast<uint16_t>(size);
	m_readPos = {0, 0};
	m_readError = false; // fresh content: clear any prior over-read flag (T3.1)
	return true;
}

// ---- Comparison ----

bool ZCom_BitStream::isEqual(const ZCom_BitStream &other) const {
	size_t ourBytes = (m_writeBit + 7) / 8;
	size_t otherBytes = (other.m_writeBit + 7) / 8;
	if (ourBytes != otherBytes)
		return false;
	return m_data.size() >= ourBytes && other.m_data.size() >= otherBytes
			   ? memcmp(m_data.data(), other.m_data.data(), ourBytes) == 0
			   : false;
}

// ---- Duplicate ----

std::unique_ptr<ZCom_BitStream> ZCom_BitStream::Duplicate() const {
	auto dup = std::make_unique<ZCom_BitStream>();
	if (m_readBit >= m_writeBit)
		return dup;
	size_t bitsToCopy = m_writeBit - m_readBit;
	size_t byteStart = m_readBit / 8;
	size_t bitOffset = m_readBit % 8;
	if (bitOffset == 0) {
		// Byte-aligned read head: copy whole bytes directly.
		size_t bytes = (bitsToCopy + 7) / 8;
		if (byteStart < m_data.size()) {
			size_t avail = m_data.size() - byteStart;
			if (bytes > avail)
				bytes = avail;
			dup->m_data.assign(m_data.begin() + byteStart, m_data.begin() + byteStart + bytes);
		}
	} else {
		// Unaligned read head: bit-shift so the first unconsumed bit lands at
		// dup bit 0. Copying raw bytes here (the old code) would retain the
		// already-consumed partial byte and desync the duplicate (live on the
		// Lua-event path: Goop/network.cpp Duplicate() of inbound streams).
		size_t outBytes = (bitsToCopy + 7) / 8;
		dup->m_data.assign(outBytes, 0);
		for (size_t i = 0; i < bitsToCopy; ++i) {
			size_t src = m_readBit + i;
			size_t bIdx = src / 8;
			if (bIdx < m_data.size() && (m_data[bIdx] & (1u << (src % 8))))
				dup->m_data[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
		}
	}
	dup->m_writeBit = bitsToCopy;
	dup->m_readBit = 0;
	dup->m_fillPos.bit = static_cast<uint16_t>(dup->m_writeBit % 8);
	dup->m_fillPos.pos = static_cast<uint16_t>(dup->m_writeBit / 8);
	dup->m_readPos = {0, 0};
	return dup;
}

void ZCom_BitStream::assign(const uint8_t *data, size_t bytes) {
	m_data.assign(data, data + bytes);
	m_writeBit = bytes * 8;
	m_readBit = 0;
	m_fillPos.bit = 0;
	m_fillPos.pos = static_cast<uint16_t>(bytes);
	m_readPos = {0, 0};
	m_readError = false; // fresh content: clear any prior over-read flag (T3.1)
}

// ---- Logging stubs (ref API, game-unused) ----

void ZCom_BitStream::logReadState() {}
void ZCom_BitStream::logWriteState() {}

// ---- Custom allocation (delegate to global; matches ref API) ----

void *ZCom_BitStream::operator new(size_t _size) {
	return ::operator new(_size);
}

void ZCom_BitStream::operator delete(void *_p) {
	::operator delete(_p);
}
