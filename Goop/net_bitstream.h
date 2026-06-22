#ifndef GUSANOS_NET_BITSTREAM_H
#define GUSANOS_NET_BITSTREAM_H

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>

/**
 * NetBitStream: bit-level serialization buffer.
 *
 * Wraps a dynamically-growing byte buffer with bit-precise read/write
 * cursors. Designed to replace ZoidCom's ZCom_BitStream with a
 * self-contained implementation that buffers internally.
 *
 * The buffer can then be sent via ENet (or other transport) and
 * reconstructed on the receiving end by passing the raw data.
 */
class NetBitStream
{
public:
	NetBitStream();
	
	/// Construct from received data (read-only initially, can write after reset)
	NetBitStream(const void* data, size_t byteLen);
	
	/// Construct as copy
	NetBitStream(const NetBitStream& other);
	NetBitStream& operator=(const NetBitStream& other);
	
	/** Reset both read and write cursors to beginning */
	void reset();
	
	/** Reset for writing from scratch (clears buffer too) */
	void clear();
	
	// ---- Writers ----
	void addInt(int32_t val, int bits);
	void addSignedInt(int32_t val, int bits);
	void addFloat(float val, int bits);
	void addString(const char* str);
	void addBitStream(const NetBitStream* other);
	
	// ---- Readers ----
	int32_t getInt(int bits);
	int32_t getSignedInt(int bits);
	float getFloat(int bits);
	/// Returns pointer to internally-buffered string data (valid until next read)
	const char* getStringStatic();
	/// Allocates a new copy (caller must free with delete[])
	char* getString();
	
	/// Create a heap copy
	NetBitStream* Duplicate() const;
	
	// ---- Raw access for transport ----
	const uint8_t* getData() const { return m_buffer.data(); }
	size_t getLength() const { return m_buffer.size(); }
	size_t getByteLength() const { return m_buffer.size(); }
	
	/** Total bits written */
	size_t getBitLength() const { return m_writeCursor; }
	
	/** Bytes consumed by reading so far */
	size_t getReadBytePos() const { return (m_readCursor + 7) / 8; }
	
	/** Number of readable bits remaining */
	size_t getRemainingBits() const
	{
		return (m_writeCursor > m_readCursor) ? (m_writeCursor - m_readCursor) : 0;
	}
	
	bool hasMoreBits() const { return m_readCursor < m_writeCursor; }

private:
	void ensureCapacity(size_t neededBits);
	void writeBits(uint32_t val, int bits);
	uint32_t readBits(int bits);
	
	std::vector<uint8_t> m_buffer;
	size_t m_writeCursor;  ///< Current write position in bits
	size_t m_readCursor;   ///< Current read position in bits
	
	/// Temp storage for getStringStatic()
	mutable std::string m_readString;
};

#endif // GUSANOS_NET_BITSTREAM_H
