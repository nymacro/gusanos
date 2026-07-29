#include "net_auto_replicator.h"

#include <cstring>

namespace {

// Aligned read/write helpers
template <typename T>
inline T readAligned(const void *p) {
	T v;
	std::memcpy(&v, p, sizeof(v));
	return v;
}
template <typename T>
inline void writeAligned(void *p, T v) {
	std::memcpy(p, &v, sizeof(v));
}

} // namespace

// ---------------------------------------------------------------------------
// AutoReplicatorInt
// ---------------------------------------------------------------------------

bool AutoReplicatorInt::detect(bool force) {
	if (!m_ptr)
		return false;
	int32_t val = readAligned<int32_t>(m_ptr);
	if (force || initial || val != m_old) {
		m_old = val;
		initial = false;
		return true;
	}
	return false;
}

void AutoReplicatorInt::emit(ZCom_BitStream &out) {
	if (!m_ptr)
		return;
	if (m_sign)
		out.addSignedInt(readAligned<int32_t>(m_ptr), m_bits);
	else
		out.addInt(static_cast<uint32_t>(readAligned<uint32_t>(m_ptr)), m_bits);
}

void AutoReplicatorInt::unpackStore(ZCom_BitStream &in) {
	int32_t val = m_sign ? in.getSignedInt(m_bits) : static_cast<int32_t>(in.getInt(m_bits));
	if (m_ptr)
		writeAligned<int32_t>(m_ptr, val);
}

void *AutoReplicatorInt::decodeForPeek(ZCom_BitStream &in) {
	m_peek = m_sign ? in.getSignedInt(m_bits) : static_cast<int32_t>(in.getInt(m_bits));
	return &m_peek;
}

void AutoReplicatorInt::commitPeek() {
	if (m_ptr)
		writeAligned<int32_t>(m_ptr, m_peek);
}

void AutoReplicatorInt::skip(ZCom_BitStream &in) {
	if (m_sign)
		in.skipSignedInt(m_bits);
	else
		in.skipInt(m_bits);
}

// ---------------------------------------------------------------------------
// AutoReplicatorFloat
// ---------------------------------------------------------------------------

bool AutoReplicatorFloat::detect(bool force) {
	if (!m_ptr)
		return false;
	float val = readAligned<float>(m_ptr);
	if (force || initial || val != m_old) {
		m_old = val;
		initial = false;
		return true;
	}
	return false;
}

void AutoReplicatorFloat::emit(ZCom_BitStream &out) {
	if (!m_ptr)
		return;
	out.addFloat(readAligned<float>(m_ptr), m_bits);
}

void AutoReplicatorFloat::unpackStore(ZCom_BitStream &in) {
	float val = in.getFloat(m_bits);
	if (m_ptr)
		writeAligned<float>(m_ptr, val);
}

void *AutoReplicatorFloat::decodeForPeek(ZCom_BitStream &in) {
	m_peek = in.getFloat(m_bits);
	return &m_peek;
}

void AutoReplicatorFloat::commitPeek() {
	if (m_ptr)
		writeAligned<float>(m_ptr, m_peek);
}

void AutoReplicatorFloat::skip(ZCom_BitStream &in) {
	in.skipFloat(m_bits);
}

// ---------------------------------------------------------------------------
// AutoReplicatorBool
// ---------------------------------------------------------------------------

bool AutoReplicatorBool::detect(bool force) {
	if (!m_ptr)
		return false;
	bool val = *m_ptr;
	bool old = initial ? false : m_old;
	if (force || initial || val != old) {
		m_old = val;
		initial = false;
		return true;
	}
	return false;
}

void AutoReplicatorBool::emit(ZCom_BitStream &out) {
	if (!m_ptr)
		return;
	out.addInt(*m_ptr ? 1 : 0, 1);
}

void AutoReplicatorBool::unpackStore(ZCom_BitStream &in) {
	int v = in.getInt(1);
	if (m_ptr)
		*m_ptr = (v != 0);
}

void *AutoReplicatorBool::decodeForPeek(ZCom_BitStream &in) {
	m_peek = in.getInt(1);
	return &m_peek;
}

void AutoReplicatorBool::commitPeek() {
	if (m_ptr)
		*m_ptr = (m_peek != 0);
}

void AutoReplicatorBool::skip(ZCom_BitStream &in) {
	in.skipInt(1);
}
