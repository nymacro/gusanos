#include "net_replicator.h"
#include "net_bitstream.h"
#include <cstring>

// ---- ZCom_Replicate_Bool ----
void ZCom_Replicate_Bool::packData(ZCom_BitStream *stream) {
	stream->addBool(m_value);
	m_oldValue = m_value;
}

void ZCom_Replicate_Bool::unpackData(ZCom_BitStream *stream, bool store, uint32_t estimatedTimeSent) {
	(void)estimatedTimeSent;
	bool val = stream->getBool();
	if (store) {
		m_value = val;
		m_oldValue = val;
	}
}

// ---- ZCom_Replicate_Boolp ----
bool ZCom_Replicate_Boolp::checkState() {
	if (!m_ptr)
		return false;
	bool changed = (*m_ptr != m_oldValue);
	return changed;
}

void ZCom_Replicate_Boolp::packData(ZCom_BitStream *stream) {
	if (m_ptr) {
		stream->addBool(*m_ptr);
		m_oldValue = *m_ptr;
	} else {
		stream->addBool(false);
	}
}

void ZCom_Replicate_Boolp::unpackData(ZCom_BitStream *stream, bool store, uint32_t estimatedTimeSent) {
	(void)estimatedTimeSent;
	bool val = stream->getBool();
	if (store && m_ptr) {
		*m_ptr = val;
		m_oldValue = val;
	}
}

// ---- ZCom_Replicate_Stringp ----
bool ZCom_Replicate_Stringp::checkState() {
	if (!m_ptr || !*m_ptr)
		return false;
	return *m_ptr != m_oldValue;
}

void ZCom_Replicate_Stringp::packData(ZCom_BitStream *stream) {
	if (m_ptr && *m_ptr) {
		stream->addString(*m_ptr);
		m_oldValue = *m_ptr;
	} else {
		stream->addString("");
	}
}

void ZCom_Replicate_Stringp::unpackData(ZCom_BitStream *stream, bool store, uint32_t estimatedTimeSent) {
	(void)estimatedTimeSent;
	// Always consume the encoded string so the read head stays in sync even when
	// the interceptor rejected the update (store == false); commit to m_oldValue
	// only when actually storing.
	const char *val = stream->getStringStatic();
	if (store && m_ptr) {
		m_lastRead = val;
		m_oldValue = m_lastRead;
	}
}

// ---- ZCom_Replicate_StringWp ----
bool ZCom_Replicate_StringWp::checkState() {
	if (!m_ptr || !*m_ptr)
		return false;
	return *m_ptr != m_oldValue;
}

void ZCom_Replicate_StringWp::packData(ZCom_BitStream *stream) {
	if (m_ptr && *m_ptr) {
		stream->addStringW(*m_ptr);
		m_oldValue = *m_ptr;
	} else {
		stream->addStringW(L"");
	}
}

void ZCom_Replicate_StringWp::unpackData(ZCom_BitStream *stream, bool store, uint32_t estimatedTimeSent) {
	(void)estimatedTimeSent;
	const wchar_t *val = stream->getStringWStatic();
	if (store && m_ptr) {
		m_lastWRead = val;
		m_oldValue = m_lastWRead;
	}
}

// ---- ZCom_Replicate_Memblock ----
bool ZCom_Replicate_Memblock::checkState() {
	if (!m_ptr)
		return false;
	return memcmp(m_ptr, m_oldData.data(), m_blockSize) != 0;
}

void ZCom_Replicate_Memblock::packData(ZCom_BitStream *stream) {
	if (!m_ptr)
		return;
	stream->addBuffer(static_cast<const char *>(m_ptr), static_cast<uint16_t>(m_blockSize));
	memcpy(m_oldData.data(), m_ptr, m_blockSize);
}

void ZCom_Replicate_Memblock::unpackData(ZCom_BitStream *stream, bool store, uint32_t estimatedTimeSent) {
	(void)estimatedTimeSent;
	if (store && m_ptr) {
		uint16_t readLen = stream->getBuffer(static_cast<char *>(m_ptr), static_cast<uint16_t>(m_blockSize));
		if (readLen > m_blockSize)
			readLen = static_cast<uint16_t>(m_blockSize);
		memcpy(m_oldData.data(), m_ptr, readLen);
	} else {
		// Skip the length-prefixed buffer (prefix + payload). Done
		// unconditionally: a 0-length buffer still has a 16-bit prefix that
		// must be consumed to keep the stream aligned.
		stream->skipBuffer();
	}
}
