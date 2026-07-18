#ifndef NET_AUTO_REPLICATOR_H
#define NET_AUTO_REPLICATOR_H

#include "net_types.h"
#include "net_bitstream.h"
#include <cstdint>

class ZCom_BitStream;

// ---------------------------------------------------------------------------
// AutoReplicator
//
// Replaces the former POD `ReplicationEntry` struct (a hand-rolled tagged
// union with a TypeInt/TypeFloat/TypeBool enum) and the ~10 duplicated
// type-dispatch chains that lived in net_node.cpp. Each concrete subclass
// owns one replicated value type: the bound pointer, its "old" snapshot, the
// `initial` flag, and the type-specific read/compare/serialize logic.
//
// Adding a new replicated type now means: derive one subclass and add a thin
// addReplicationX() shim on ZCom_Node -- no type-dispatch edits anywhere else.
//
// The pack side is intentionally split into detect() then emit() so callers
// can write the has-update bit *between* them, exactly preserving the wire
// ordering (has-update bit precedes the value bits) of the original code.
// ---------------------------------------------------------------------------
class AutoReplicator {
public:
	virtual ~AutoReplicator() = default;

	// Common metadata (mirrors the former ReplicationEntry fields).
	uint32_t flags = 0;     // ZCOM_REPFLAG_* bitmask
	uint32_t rule = 0;      // ZCOM_REPRULE_* routing bitmask
	zS16 minDelay = -1;
	zS16 maxDelay = -1;
	bool initial = true;     // forces the first pack

	// Detect whether the bound value changed since the last pack (or force/
	// initial). On change: update the internal snapshot, clear `initial`, and
	// return true. On no change: leave the snapshot untouched and return false.
	virtual bool detect(bool force) = 0;

	// Serialize the current bound value. Called only after detect() returned
	// true. Writes value bits only (no has-update prefix).
	virtual void emit(ZCom_BitStream& out) = 0;

	// Unpack side, non-interceptor path: decode the next value from `in` and
	// store it to the bound pointer.
	virtual void unpackStore(ZCom_BitStream& in) = 0;

	// Unpack side, interceptor path step 1: decode the next value from `in`
	// into an internal temp buffer and return a pointer to it (for
	// ZCom_ReplicatorBasic::peekDataStore). Does not touch the bound pointer.
	virtual void* decodeForPeek(ZCom_BitStream& in) = 0;

	// Unpack side, interceptor path step 2 (only if the interceptor accepted):
	// write the previously-decoded temp to the bound pointer.
	virtual void commitPeek() = 0;

	// Unpack side, skip path: discard the next value from `in` without
	// decoding or storing.
	virtual void skip(ZCom_BitStream& in) = 0;
};

// zS32, configurable bit width and signedness. Wire: addSignedInt/addInt.
class AutoReplicatorInt : public AutoReplicator {
public:
	AutoReplicatorInt(zS32* ptr, zU8 bits, bool sign)
		: m_ptr(ptr), m_bits(bits), m_sign(sign), m_old(ptr ? *ptr : 0) {}

	bool detect(bool force) override;
	void emit(ZCom_BitStream& out) override;
	void unpackStore(ZCom_BitStream& in) override;
	void* decodeForPeek(ZCom_BitStream& in) override;
	void commitPeek() override;
	void skip(ZCom_BitStream& in) override;

private:
	zS32* m_ptr;
	zU8 m_bits;
	bool m_sign;
	int32_t m_old;      // snapshot of last packed value
	int32_t m_peek;     // temp for interceptor decode
};

// zFloat, mantissa_bits precision. Wire: addFloat/getFloat.
//
// IMPORTANT: for mantissa_bits < 32, ZCom_BitStream::addFloat/getFloat use
// fixed-point quantization that only faithfully represents values in [-1, 1]
// (see net_bitstream.cpp). Values outside that range OVERFLOW the signed
// integer storage and get aliased, so a replicated float with a small bit
// width must be pre-scaled into [-1, 1] by the caller (or use >= 32 bits for
// full IEEE-754 fidelity). This class forwards `m_bits` straight through to
// addFloat/getFloat, so it inherits that constraint unchanged.
class AutoReplicatorFloat : public AutoReplicator {
public:
	AutoReplicatorFloat(zFloat* ptr, zU8 mantissaBits)
		: m_ptr(ptr), m_bits(mantissaBits), m_old(ptr ? *ptr : 0.0f) {}

	bool detect(bool force) override;
	void emit(ZCom_BitStream& out) override;
	void unpackStore(ZCom_BitStream& in) override;
	void* decodeForPeek(ZCom_BitStream& in) override;
	void commitPeek() override;
	void skip(ZCom_BitStream& in) override;

private:
	zFloat* m_ptr;
	// Wire bit width for the float. >= 32 => raw IEEE-754 (full range/fidelity);
	// < 32 => fixed-point quantization valid only for values in [-1, 1].
	zU8 m_bits;
	float m_old;
	float m_peek;
};

// bool, 1 bit on the wire, reads/writes exactly 1 byte.
//
// This supplants the former addReplicationBool() behavior, which incorrectly
// stored type=TypeInt (NOT TypeBool) and routed bools through the int path -- causing a 4-byte
// over-read on pack (benign: only the low bit was serialized) and a 4-byte
// over-WRITE into a 1-byte bool* on unpack (clobbering adjacent memory). The
// correct 1-byte logic below matches the (formerly dead) TypeBool branches in
// the original net_node.cpp.
class AutoReplicatorBool : public AutoReplicator {
public:
	AutoReplicatorBool(bool* ptr)
		: m_ptr(ptr), m_old(ptr ? *ptr : false) {}

	bool detect(bool force) override;
	void emit(ZCom_BitStream& out) override;
	void unpackStore(ZCom_BitStream& in) override;
	void* decodeForPeek(ZCom_BitStream& in) override;
	void commitPeek() override;
	void skip(ZCom_BitStream& in) override;

private:
	bool* m_ptr;
	bool m_old;
	int32_t m_peek;   // peekDataStore takes int* (ZCom_ReplicatorBasic contract)
};

#endif // NET_AUTO_REPLICATOR_H
