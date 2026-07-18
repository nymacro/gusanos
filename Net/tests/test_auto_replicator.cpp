// test_auto_replicator.cpp
// Unit + integration tests for the AutoReplicator abstraction that replaces
// the former POD ReplicationEntry tagged union in net_node.cpp.
//
// Covers the three value types (int/unsigned, float, bool), the initial/force
// dirty detection, the skip path, and the interceptor accept/reject paths.
// The bool tests double as a regression test for the former 4-byte
// over-write-into-a-1-byte-bool bug (now fixed by AutoReplicatorBool).

#include <boost/test/unit_test.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "net_auto_replicator.h"
#include "net_node.h"
#include "net_replicator.h"

BOOST_AUTO_TEST_SUITE(auto_replicator)

// ---- Int: unsigned, round-trip ----
BOOST_AUTO_TEST_CASE(int_unsigned_roundtrip)
{
	zS32 v = 200;
	AutoReplicatorInt rep(&v, 8, /*sign=*/false);
	rep.initial = false; // value already equals snapshot => not dirty

	BOOST_CHECK(!rep.detect(/*force=*/false)); // unchanged -> no pack

	v = 201;
	BOOST_CHECK(rep.detect(/*force=*/false));   // changed -> pack
	BOOST_CHECK(!rep.detect(/*force=*/false));  // snapshot updated -> no pack

	ZCom_BitStream out;
	rep.emit(out);
	BOOST_REQUIRE_EQUAL(out.getBitCount(), 8u);

	ZCom_BitStream in;
	in.addBitStream(&out);
	in.resetReadState();

	zS32 decoded = 0;
	{
		// Decode via unpackStore on a fresh pointer.
		AutoReplicatorInt sink(&decoded, 8, false);
		sink.unpackStore(in); // consume 8 bits
	}
	BOOST_CHECK_EQUAL(decoded, 201);
	BOOST_CHECK(in.endOfStream());
}

// ---- Int: signed, negative value ----
BOOST_AUTO_TEST_CASE(int_signed_roundtrip)
{
	zS32 v = -7;
	AutoReplicatorInt rep(&v, 8, /*sign=*/true);
	rep.initial = false;

	BOOST_CHECK(!rep.detect(false));
	v = -42;
	BOOST_CHECK(rep.detect(false));

	ZCom_BitStream out;
	rep.emit(out);
	BOOST_REQUIRE_EQUAL(out.getBitCount(), 8u);

	ZCom_BitStream in;
	in.addBitStream(&out);
	in.resetReadState();
	zS32 decoded = 0;
	AutoReplicatorInt sink(&decoded, 8, true);
	sink.unpackStore(in);
	BOOST_CHECK_EQUAL(decoded, -42);
}

// ---- Int: 32-bit full width ----
BOOST_AUTO_TEST_CASE(int_32bit_roundtrip)
{
	zS32 v = 1234567;
	AutoReplicatorInt rep(&v, 32, /*sign=*/false);
	rep.initial = false;
	BOOST_CHECK(!rep.detect(false));
	v = 7654321;
	BOOST_CHECK(rep.detect(false));

	ZCom_BitStream out;
	rep.emit(out);
	BOOST_REQUIRE_EQUAL(out.getBitCount(), 32u);

	ZCom_BitStream in;
	in.addBitStream(&out);
	in.resetReadState();
	zS32 decoded = 0;
	AutoReplicatorInt sink(&decoded, 32, false);
	sink.unpackStore(in);
	BOOST_CHECK_EQUAL(decoded, 7654321);
}

// ---- Float round-trip ----
// addFloat/getFloat quantize to [-1,1] over (2^bits - 1) steps when bits < 32,
// so use a value inside the range and allow the quantization step as tolerance.
BOOST_AUTO_TEST_CASE(float_roundtrip)
{
	zFloat v = 0.5f;
	AutoReplicatorFloat rep(&v, 10);
	rep.initial = false;
	BOOST_CHECK(!rep.detect(false));
	v = 0.25f;
	BOOST_CHECK(rep.detect(false));

	ZCom_BitStream out;
	rep.emit(out);

	ZCom_BitStream in;
	in.addBitStream(&out);
	in.resetReadState();
	zFloat decoded = 0.0f;
	AutoReplicatorFloat sink(&decoded, 10);
	sink.unpackStore(in);
	// 10 mantissa bits => step ~ 2/1023 ~ 0.002.
	BOOST_CHECK(std::abs(decoded - 0.25f) < 0.01f);
}

// ---- Bool round-trip + adjacency regression ----
// A bool sandwiched between sentinel bytes must NOT clobber its neighbours
// when unpacked (the bug fixed by AutoReplicatorBool).
BOOST_AUTO_TEST_CASE(bool_roundtrip_and_no_adjacent_clobber)
{
	// Packed struct: [sentinel0][b][sentinel1]
	uint8_t buf[3] = {0xAB, 0, 0xCD};
	bool* b = reinterpret_cast<bool*>(&buf[1]);

	AutoReplicatorBool rep(b);
	rep.initial = false;
	BOOST_CHECK(!rep.detect(false));

	*b = true;
	BOOST_CHECK(rep.detect(false));

	ZCom_BitStream out;
	rep.emit(out);
	BOOST_REQUIRE_EQUAL(out.getBitCount(), 1u);

	// Now unpack into a DIFFERENT buffer to check adjacency safety.
	uint8_t buf2[3] = {0x11, 0, 0x22};
	bool* b2 = reinterpret_cast<bool*>(&buf2[1]);
	BOOST_REQUIRE_EQUAL(buf2[0], 0x11);
	BOOST_REQUIRE_EQUAL(buf2[2], 0x22);

	ZCom_BitStream in;
	in.addBitStream(&out);
	in.resetReadState();
	AutoReplicatorBool sink(b2);
	sink.unpackStore(in);

	BOOST_CHECK(*b2 == true);
	BOOST_CHECK_EQUAL(buf2[0], 0x11); // untouched
	BOOST_CHECK_EQUAL(buf2[2], 0x22); // untouched
}

// ---- initial forces first pack ----
BOOST_AUTO_TEST_CASE(initial_forces_first_pack)
{
	zS32 v = 5; // equals ctor snapshot (m_old = 5)
	AutoReplicatorInt rep(&v, 8, false);
	// initial defaults to true
	BOOST_CHECK(rep.detect(false));   // initial => pack once
	BOOST_CHECK(!rep.detect(false));  // now clean
}

// ---- force overrides no change ----
BOOST_AUTO_TEST_CASE(force_overrides_no_change)
{
	zS32 v = 9;
	AutoReplicatorInt rep(&v, 8, false);
	rep.initial = false;
	BOOST_CHECK(!rep.detect(false));   // unchanged
	BOOST_CHECK(rep.detect(true));     // forced
	BOOST_CHECK(!rep.detect(false));   // snapshot updated
}

// ---- skip consumes exactly the value bits ----
BOOST_AUTO_TEST_CASE(skip_consumes_bits)
{
	zS32 v = 123;
	AutoReplicatorInt rep(&v, 8, false);
	rep.initial = false;
	BOOST_CHECK(rep.detect(true)); // force pack

	ZCom_BitStream out;
	rep.emit(out);                 // 8 bits written

	ZCom_BitStream in;
	in.addBitStream(&out);
	in.resetReadState();
	rep.skip(in);                  // consume 8 bits
	BOOST_CHECK(in.endOfStream());

	// skip on an unforced type must advance the stream without touching ptr
	zS32 w = 55;
	AutoReplicatorInt rep2(&w, 8, false);
	rep2.initial = false;
	rep2.skip(in); // no-op (in is empty), must not read/crash
	BOOST_CHECK_EQUAL(w, 55);
}

// ---- Interceptor accept commits the decoded value ----
class AcceptInterceptor : public ZCom_NodeReplicationInterceptor {
public:
	int items = 0;
	bool inPreUpdateItem(ZCom_Node*, uint32_t, eZCom_NodeRole,
	                     ZCom_Replicator* rep, uint32_t) override {
		items++;
		// Confirm the interceptor can read the decoded value via peekData().
		int* p = static_cast<int*>(rep->peekData());
		BOOST_CHECK(p != nullptr);
		return true; // accept
	}
};

BOOST_AUTO_TEST_CASE(interceptor_accept_commits)
{
	zS32 v = 7;
	AutoReplicatorInt rep(&v, 8, false);
	rep.flags = ZCOM_REPFLAG_INTERCEPT;
	rep.initial = false;
	BOOST_CHECK(rep.detect(true)); // force pack

	ZCom_BitStream out;
	rep.emit(out);

	// Receiver side with interceptor.
	zS32 decoded = -1;
	AutoReplicatorInt sink(&decoded, 8, false);
	sink.flags = ZCOM_REPFLAG_INTERCEPT;

	ZCom_BitStream in;
	in.addBitStream(&out);
	in.resetReadState();

	AcceptInterceptor interceptor;
	// Simulate the unpackAllReplicators intercept branch.
	ZCom_ReplicatorSetup tempSetup(sink.flags, sink.rule, /*interceptID=*/0);
	ZCom_ReplicatorBasic tempRep(&tempSetup);
	void* p = sink.decodeForPeek(in);
	tempRep.peekDataStore(p);
	bool accept = interceptor.inPreUpdateItem(nullptr, 0, eZCom_RoleAuthority, &tempRep, 0);
	tempRep.peekDataStore(nullptr);
	if (accept) sink.commitPeek();

	BOOST_CHECK_EQUAL(interceptor.items, 1);
	BOOST_CHECK_EQUAL(decoded, 7);
}

// ---- Interceptor reject skips commit ----
class RejectInterceptor : public ZCom_NodeReplicationInterceptor {
public:
	bool inPreUpdateItem(ZCom_Node*, uint32_t, eZCom_NodeRole,
	                     ZCom_Replicator*, uint32_t) override {
		return false; // reject
	}
};

BOOST_AUTO_TEST_CASE(interceptor_reject_skips_commit)
{
	zS32 v = 99;
	AutoReplicatorInt rep(&v, 8, false);
	rep.flags = ZCOM_REPFLAG_INTERCEPT;
	rep.initial = false;
	BOOST_CHECK(rep.detect(true));

	ZCom_BitStream out;
	rep.emit(out);

	zS32 decoded = -1;
	AutoReplicatorInt sink(&decoded, 8, false);
	sink.flags = ZCOM_REPFLAG_INTERCEPT;

	ZCom_BitStream in;
	in.addBitStream(&out);
	in.resetReadState();

	RejectInterceptor interceptor;
	ZCom_ReplicatorSetup tempSetup(sink.flags, sink.rule, 0);
	ZCom_ReplicatorBasic tempRep(&tempSetup);
	void* p = sink.decodeForPeek(in); // consumes the value bits
	tempRep.peekDataStore(p);
	bool accept = interceptor.inPreUpdateItem(nullptr, 0, eZCom_RoleAuthority, &tempRep, 0);
	tempRep.peekDataStore(nullptr);
	if (accept) sink.commitPeek();

	BOOST_CHECK(!accept);
	BOOST_CHECK_EQUAL(decoded, -1); // unchanged
	// The value bits were consumed (decodeForPeek advances the stream).
	BOOST_CHECK(in.endOfStream());
}

// ---- End-to-end via ZCom_Node public API (bool adjacency) ----
BOOST_AUTO_TEST_CASE(node_api_bool_does_not_clobber_adjacent)
{
	// Sender struct with a bool between two sentinels.
	struct S { uint8_t a; bool b; uint8_t c; };
	S s;
	s.a = 0x55; s.b = false; s.c = 0xAA;

	ZCom_Node node;
	node.addReplicationBool(&s.b, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL);

	// First pack: initial => emits bool=0.
	ZCom_BitStream packed;
	node.packAllReplicators(&packed);
	BOOST_REQUIRE(packed.getBitCount() >= 1u);

	// Flip on sender, repack (now emits bool=1).
	s.b = true;
	ZCom_BitStream packed2;
	node.packAllReplicators(&packed2);
	BOOST_REQUIRE(packed2.getBitCount() >= 1u);

	// Receiver struct with its own sentinels; a separate node binds to it.
	S r;
	r.a = 0x55; r.b = false; r.c = 0xAA;

	ZCom_Node nodeRcv;
	nodeRcv.addReplicationBool(&r.b, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL);

	ZCom_BitStream in;
	in.addBitStream(&packed2);
	in.resetReadState();
	nodeRcv.unpackAllReplicators(&in, /*store=*/true, /*estimatedTimeSent=*/0);

	BOOST_CHECK(r.b == true);
	BOOST_CHECK_EQUAL(r.a, 0x55); // neighbour untouched
	BOOST_CHECK_EQUAL(r.c, 0xAA); // neighbour untouched
}

// ---- End-to-end via ZCom_Node public API (int) ----
BOOST_AUTO_TEST_CASE(node_api_int_roundtrip)
{
	ZCom_Node node;
	zS32 iv = 0;
	node.addReplicationInt(&iv, 16, true, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL);

	iv = -1234;
	ZCom_BitStream packed;
	node.packAllReplicators(&packed);

	zS32 rv = 0;
	ZCom_Node node2;
	node2.addReplicationInt(&rv, 16, true, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL);

	ZCom_BitStream in;
	in.addBitStream(&packed);
	in.resetReadState();
	node2.unpackAllReplicators(&in, true, 0);

	BOOST_CHECK_EQUAL(rv, -1234);
}

BOOST_AUTO_TEST_SUITE_END()
