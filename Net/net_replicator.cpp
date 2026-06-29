#include "net_replicator.h"
#include "net_bitstream.h"

// ---- ZCom_Replicate_Bool ----
void ZCom_Replicate_Bool::packData(ZCom_BitStream* stream)
{
	stream->addBool(m_value);
	m_oldValue = m_value;
}

void ZCom_Replicate_Bool::unpackData(ZCom_BitStream* stream, bool store, uint32_t estimatedTimeSent)
{
	(void)estimatedTimeSent;
	bool val = stream->getBool();
	if (store) {
		m_value = val;
		m_oldValue = val;
	}
}

// ---- ZCom_Replicate_Boolp ----
bool ZCom_Replicate_Boolp::checkState()
{
	if (!m_ptr) return false;
	bool changed = (*m_ptr != m_oldValue);
	return changed;
}

void ZCom_Replicate_Boolp::packData(ZCom_BitStream* stream)
{
	if (m_ptr) {
		stream->addBool(*m_ptr);
		m_oldValue = *m_ptr;
	} else {
		stream->addBool(false);
	}
}

void ZCom_Replicate_Boolp::unpackData(ZCom_BitStream* stream, bool store, uint32_t estimatedTimeSent)
{
	(void)estimatedTimeSent;
	bool val = stream->getBool();
	if (store && m_ptr) {
		*m_ptr = val;
		m_oldValue = val;
	}
}
