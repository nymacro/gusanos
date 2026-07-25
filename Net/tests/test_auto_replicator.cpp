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
BOOST_AUTO_TEST_CASE(int_unsigned_roundtrip) {
	zS32 v = 200;
	AutoReplicatorInt rep(&v, 8, /*sign=*/false);
	rep.initial = false; // value already equals snapshot => not dirty

	BOOST_CHECK(!rep.detect(/*force=*/false)); // unchanged -> no pack

	v = 201;
	BOOST_CHECK(rep.detect(/*force=*/false));  // changed -> pack
	BOOST_CHECK(!rep.detect(/*force=*/false)); // snapshot updated -> no pack

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
BOOST_AUTO_TEST_CASE(int_signed_roundtrip) {
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
BOOST_AUTO_TEST_CASE(int_32bit_roundtrip) {
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
BOOST_AUTO_TEST_CASE(float_roundtrip) {
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
BOOST_AUTO_TEST_CASE(bool_roundtrip_and_no_adjacent_clobber) {
	// Packed struct: [sentinel0][b][sentinel1]
	uint8_t buf[3] = {0xAB, 0, 0xCD};
	bool *b = reinterpret_cast<bool *>(&buf[1]);

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
	bool *b2 = reinterpret_cast<bool *>(&buf2[1]);
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
BOOST_AUTO_TEST_CASE(initial_forces_first_pack) {
	zS32 v = 5; // equals ctor snapshot (m_old = 5)
	AutoReplicatorInt rep(&v, 8, false);
	// initial defaults to true
	BOOST_CHECK(rep.detect(false));	 // initial => pack once
	BOOST_CHECK(!rep.detect(false)); // now clean
}

// ---- force overrides no change ----
BOOST_AUTO_TEST_CASE(force_overrides_no_change) {
	zS32 v = 9;
	AutoReplicatorInt rep(&v, 8, false);
	rep.initial = false;
	BOOST_CHECK(!rep.detect(false)); // unchanged
	BOOST_CHECK(rep.detect(true));	 // forced
	BOOST_CHECK(!rep.detect(false)); // snapshot updated
}

// ---- skip consumes exactly the value bits ----
BOOST_AUTO_TEST_CASE(skip_consumes_bits) {
	zS32 v = 123;
	AutoReplicatorInt rep(&v, 8, false);
	rep.initial = false;
	BOOST_CHECK(rep.detect(true)); // force pack

	ZCom_BitStream out;
	rep.emit(out); // 8 bits written

	ZCom_BitStream in;
	in.addBitStream(&out);
	in.resetReadState();
	rep.skip(in); // consume 8 bits
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
	bool inPreUpdateItem(ZCom_Node *, uint32_t, eZCom_NodeRole, ZCom_Replicator *rep, uint32_t) override {
		items++;
		// Confirm the interceptor can read the decoded value via peekData().
		int *p = static_cast<int *>(rep->peekData());
		BOOST_CHECK(p != nullptr);
		return true; // accept
	}
};

BOOST_AUTO_TEST_CASE(interceptor_accept_commits) {
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
	void *p = sink.decodeForPeek(in);
	tempRep.peekDataStore(p);
	bool accept = interceptor.inPreUpdateItem(nullptr, 0, eZCom_RoleAuthority, &tempRep, 0);
	tempRep.peekDataStore(nullptr);
	if (accept)
		sink.commitPeek();

	BOOST_CHECK_EQUAL(interceptor.items, 1);
	BOOST_CHECK_EQUAL(decoded, 7);
}

// ---- Interceptor reject skips commit ----
class RejectInterceptor : public ZCom_NodeReplicationInterceptor {
  public:
	bool inPreUpdateItem(ZCom_Node *, uint32_t, eZCom_NodeRole, ZCom_Replicator *, uint32_t) override {
		return false; // reject
	}
};

BOOST_AUTO_TEST_CASE(interceptor_reject_skips_commit) {
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
	void *p = sink.decodeForPeek(in); // consumes the value bits
	tempRep.peekDataStore(p);
	bool accept = interceptor.inPreUpdateItem(nullptr, 0, eZCom_RoleAuthority, &tempRep, 0);
	tempRep.peekDataStore(nullptr);
	if (accept)
		sink.commitPeek();

	BOOST_CHECK(!accept);
	BOOST_CHECK_EQUAL(decoded, -1); // unchanged
	// The value bits were consumed (decodeForPeek advances the stream).
	BOOST_CHECK(in.endOfStream());
}

// ---- End-to-end via ZCom_Node public API (bool adjacency) ----
BOOST_AUTO_TEST_CASE(node_api_bool_does_not_clobber_adjacent) {
	// Sender struct with a bool between two sentinels.
	struct S {
		uint8_t a;
		bool b;
		uint8_t c;
	};
	S s;
	s.a = 0x55;
	s.b = false;
	s.c = 0xAA;

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
	r.a = 0x55;
	r.b = false;
	r.c = 0xAA;

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
BOOST_AUTO_TEST_CASE(node_api_int_roundtrip) {
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

// ===========================================================================
// G2: float / bool interceptor accept & reject paths.
// The int interceptor path is covered by interceptor_accept_commits and
// interceptor_reject_skips_commit; float and bool are not. They share the
// decodeForPeek/commitPeek plumbing but return type-specific peek buffers, so
// pin each. (Production only uses INTERCEPT on int auto-reps -- m_playerID,
// m_wormID -- but the contract must hold for the other types too.)
// ===========================================================================

class AcceptInterceptor2 : public ZCom_NodeReplicationInterceptor {
  public:
	int items = 0;
	bool inPreUpdateItem(ZCom_Node *, uint32_t, eZCom_NodeRole, ZCom_Replicator *rep, uint32_t) override {
		items++;
		return true; // accept
	}
};

class RejectInterceptor2 : public ZCom_NodeReplicationInterceptor {
  public:
	bool inPreUpdateItem(ZCom_Node *, uint32_t, eZCom_NodeRole, ZCom_Replicator *, uint32_t) override {
		return false; // reject
	}
};

// Generic helper: simulate the unpackAllReplicators intercept branch for one
// AutoReplicator of type RepT, packed from `value` into `out`. `peekAs`
// dereferences the interceptor's peekData() pointer with the correct type.
template <typename RepT, typename T>
void simulateInterceptDecode(RepT &sink, ZCom_BitStream &out, bool accept, T &decoded) {
	ZCom_BitStream in;
	in.addBitStream(&out);
	in.resetReadState();

	ZCom_ReplicatorSetup tempSetup(sink.flags, sink.rule, /*interceptID=*/0);
	ZCom_ReplicatorBasic tempRep(&tempSetup);
	void *p = sink.decodeForPeek(in);
	tempRep.peekDataStore(p);
	AcceptInterceptor2 a;
	RejectInterceptor2 r;
	bool ok = accept ? a.inPreUpdateItem(nullptr, 0, eZCom_RoleAuthority, &tempRep, 0)
					 : r.inPreUpdateItem(nullptr, 0, eZCom_RoleAuthority, &tempRep, 0);
	tempRep.peekDataStore(nullptr);
	if (ok)
		sink.commitPeek();
}

BOOST_AUTO_TEST_CASE(float_interceptor_accept_and_reject) {
	float v = 0.5f; // must be within addFloat's [-1,1] quantization range
	AutoReplicatorFloat rep(&v, 10);
	rep.flags = ZCOM_REPFLAG_INTERCEPT;
	rep.initial = false;
	BOOST_REQUIRE(rep.detect(true)); // force pack

	ZCom_BitStream out;
	rep.emit(out);

	// Accept -> value committed.
	float decA = -1.0f;
	AutoReplicatorFloat sinkA(&decA, 10);
	sinkA.flags = ZCOM_REPFLAG_INTERCEPT;
	simulateInterceptDecode(sinkA, out, /*accept=*/true, decA);
	BOOST_CHECK_CLOSE(decA, 0.5f, 0.5f);

	// Reject -> value NOT committed (stays -1).
	float decR = -1.0f;
	AutoReplicatorFloat sinkR(&decR, 10);
	sinkR.flags = ZCOM_REPFLAG_INTERCEPT;
	simulateInterceptDecode(sinkR, out, /*accept=*/false, decR);
	BOOST_CHECK_EQUAL(decR, -1.0f);
}

BOOST_AUTO_TEST_CASE(bool_interceptor_accept_and_reject) {
	bool v = true;
	AutoReplicatorBool rep(&v);
	rep.flags = ZCOM_REPFLAG_INTERCEPT;
	rep.initial = false;
	BOOST_REQUIRE(rep.detect(true));

	ZCom_BitStream out;
	rep.emit(out);

	// Accept -> value committed.
	bool decA = false;
	AutoReplicatorBool sinkA(&decA);
	sinkA.flags = ZCOM_REPFLAG_INTERCEPT;
	simulateInterceptDecode(sinkA, out, /*accept=*/true, decA);
	BOOST_CHECK_EQUAL(decA, true);

	// Reject -> value NOT committed (stays false).
	bool decR = false;
	AutoReplicatorBool sinkR(&decR);
	sinkR.flags = ZCOM_REPFLAG_INTERCEPT;
	simulateInterceptDecode(sinkR, out, /*accept=*/false, decR);
	BOOST_CHECK_EQUAL(decR, false);
}

// ===========================================================================
// G3: node-level store=false (skip) path. unpackAllReplicators(store=false)
// must advance the read position by exactly the packed value bits WITHOUT
// touching the bound pointer. The branch is dead in production (both call
// sites pass true) but a contract pin is cheaper than deleting the path.
// ===========================================================================

BOOST_AUTO_TEST_CASE(node_api_store_false_skips_and_preserves_value) {
	// Sender struct: bool b between two sentinels (matches the bool-adjacency
	// layout) plus an int that follows, so we can detect stream desync.
	struct S {
		uint8_t a;
		bool b;
		uint8_t c;
		int32_t n;
	};
	S s;
	s.a = 0x55;
	s.b = true;
	s.c = 0xAA;
	s.n = 12345;
	S r;
	r.a = 0x55;
	r.b = false;
	r.c = 0xAA;
	r.n = 0;

	// Sender bound to s.
	ZCom_Node node;
	node.addReplicationBool(&s.b, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL);
	node.addReplicationInt((zS32 *)&s.n, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL);

	// Pack everything (b changes from initial; n changes from initial).
	ZCom_BitStream packed;
	node.packAllReplicators(&packed);

	// Receiver bound to r.
	ZCom_Node nodeRcv;
	nodeRcv.addReplicationBool(&r.b, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL);
	nodeRcv.addReplicationInt((zS32 *)&r.n, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL);

	ZCom_BitStream in;
	in.addBitStream(&packed);
	in.resetReadState();

	// store=false: must consume all bits WITHOUT modifying r.
	nodeRcv.unpackAllReplicators(&in, /*store=*/false, /*estimatedTimeSent=*/0);
	BOOST_CHECK(in.endOfStream());
	BOOST_CHECK_EQUAL(r.a, 0x55);
	BOOST_CHECK_EQUAL(r.b, false); // unchanged
	BOOST_CHECK_EQUAL(r.c, 0xAA);
	BOOST_CHECK_EQUAL(r.n, 0); // unchanged

	// A second pass (store=true) should now apply, proving the first pass only
	// skipped and did not corrupt state.
	in.resetReadState();
	nodeRcv.unpackAllReplicators(&in, /*store=*/true, /*estimatedTimeSent=*/0);
	BOOST_CHECK_EQUAL(r.b, true);
	BOOST_CHECK_EQUAL(r.n, 12345);
	BOOST_CHECK_EQUAL(r.a, 0x55); // still untouched
	BOOST_CHECK_EQUAL(r.c, 0xAA);
}

// ===========================================================================
// G4: null-bound AutoReplicator guards. detect/emit/unpackStore/commitPeek all
// tolerate a null bound pointer (inherited from the old ReplicationEntry null
// tolerance). Pin the contract so a future change can't silently start
// dereferencing nullptr.
// ===========================================================================

BOOST_AUTO_TEST_CASE(null_bound_replicator_is_noop) {
	// Int with null ptr.
	AutoReplicatorInt repI(nullptr, 8, false);
	BOOST_CHECK(!repI.detect(false)); // null -> never dirty
	BOOST_CHECK(!repI.detect(true));  // force still no-op (ptr null)
	{
		ZCom_BitStream out;
		repI.emit(out); // guarded, writes nothing
		BOOST_CHECK_EQUAL(out.getBitCount(), 0u);
		ZCom_BitStream in;
		in.addBitStream(&out);
		in.resetReadState();
		repI.unpackStore(in); // guarded, no write
		repI.skip(in);		  // guarded, no read
	}

	// Float with null ptr.
	AutoReplicatorFloat repF(nullptr, 10);
	BOOST_CHECK(!repF.detect(true));
	{
		ZCom_BitStream out;
		repF.emit(out);
		BOOST_CHECK_EQUAL(out.getBitCount(), 0u);
		ZCom_BitStream in;
		in.addBitStream(&out);
		in.resetReadState();
		repF.unpackStore(in);
		repF.skip(in);
	}

	// Bool with null ptr.
	AutoReplicatorBool repB(nullptr);
	BOOST_CHECK(!repB.detect(true));
	{
		ZCom_BitStream out;
		repB.emit(out);
		BOOST_CHECK_EQUAL(out.getBitCount(), 0u);
		ZCom_BitStream in;
		in.addBitStream(&out);
		in.resetReadState();
		repB.unpackStore(in);
		repB.skip(in);
	}
}

// ===========================================================================
// G5: sender-side interceptor reject branches in packReplicatorsForRouting.
// Pre-existing (not introduced by the AutoReplicator refactor): outPreUpdate()
// returning false makes the whole node emit hasUpdate=false for every entry;
// outPreUpdateItem() returning false on an explicit (m_replicators) entry
// skips that entry. Only explicit replicators consult outPreUpdateItem on the
// sender side -- auto-reps do not. Neither reject branch was previously
// exercised. These tests pin them.
// ===========================================================================

class RejectOutPreUpdate : public ZCom_NodeReplicationInterceptor {
  public:
	ZCom_Node *seen = nullptr;
	bool outPreUpdate(ZCom_Node *node, uint32_t, eZCom_NodeRole) override {
		seen = node;
		return false; // reject the whole node
	}
};

class RejectOutPreUpdateItem : public ZCom_NodeReplicationInterceptor {
  public:
	int itemCalls = 0;
	bool outPreUpdateItem(ZCom_Node *node, uint32_t, eZCom_NodeRole, ZCom_Replicator *rep) override {
		// Reject every explicit replicator.
		(void)node;
		(void)rep;
		itemCalls++;
		return false;
	}
};

BOOST_AUTO_TEST_CASE(sender_outPreUpdate_false_emits_no_updates) {
	ZCom_Node node;
	zS32 v = 123;
	node.addReplicationInt(&v, 32, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL);

	RejectOutPreUpdate interceptor;
	node.setReplicationInterceptor(&interceptor);

	std::vector<PackedReplicator> out;
	node.packReplicatorsForRouting(out);

	BOOST_REQUIRE_EQUAL(out.size(), 1u);
	BOOST_CHECK_EQUAL(out[0].hasUpdate, false); // whole node rejected
	BOOST_CHECK_EQUAL(interceptor.seen, &node);
}

BOOST_AUTO_TEST_CASE(sender_outPreUpdateItem_false_skips_explicit_rep) {
	ZCom_Node node;
	// Explicit replicator, INTERCEPT-flagged so the sender consults
	// outPreUpdateItem for it (net_node.cpp:353).
	ZCom_ReplicatorSetup setup(ZCOM_REPFLAG_INTERCEPT, ZCOM_REPRULE_AUTH_2_ALL);
	node.addReplicator(std::make_unique<ZCom_ReplicatorBasic>(&setup), true);

	// One auto-rep (always proceeds on the sender side, NOT consulted) so we
	// can distinguish routing slots: explicit rep first, then the auto-rep.
	int32_t av = 55;
	node.addReplicationInt(&av, 8, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL);

	RejectOutPreUpdateItem interceptor;
	node.setReplicationInterceptor(&interceptor);

	std::vector<PackedReplicator> out;
	node.packReplicatorsForRouting(out);

	// Order matches m_replicators then m_autoReplications.
	BOOST_REQUIRE_EQUAL(out.size(), 2u);
	BOOST_CHECK_EQUAL(out[0].hasUpdate, false); // explicit rep: item rejected
	BOOST_CHECK_EQUAL(out[1].hasUpdate, true);	// auto-rep: not consulted
	BOOST_CHECK_EQUAL(interceptor.itemCalls, 1);
	// Auto-rep slot must still contain the packed value (8 bits).
	BOOST_CHECK_EQUAL(out[1].data.getBitCount(), 8u);
}

BOOST_AUTO_TEST_SUITE_END()
