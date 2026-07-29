// test_bitstream_hardening.cpp
// Regression tests for BitStream defensive hardening (all unit-level):
//   - T3.1: getReadError() flag set on over-read; sticky until cleared;
//     copied across copy/assign; cleared by reset/Clear/assign.
//   - T3.3: addInt/addSignedInt/getInt at bits=32 and addInt64/getInt64 at
//     bits=64 round-trip (boundary of the new asserts; asserts themselves
//     can't be tested without aborting the process).
//   - T2.4: addBitStream copies exactly getBitCount() bits (no byte padding),
//     preserving bit alignment — the property the routing path now relies on.

#include <boost/test/unit_test.hpp>
#include "net_bitstream.h"

BOOST_AUTO_TEST_SUITE(bitstream_hardening)

// ---- T3.1: over-read flag ----

BOOST_AUTO_TEST_CASE(overread_sets_flag) {
	ZCom_BitStream bs;
	bs.addInt(0xAB, 8);
	BOOST_CHECK(!bs.getReadError());
	(void)bs.getInt(16); // over-reads by 8 bits
	BOOST_CHECK(bs.getReadError());
}

BOOST_AUTO_TEST_CASE(overread_returns_zero) {
	ZCom_BitStream bs;
	bs.addInt(0xAB, 8);
	BOOST_CHECK_EQUAL(bs.getInt(16), 0u);
	BOOST_CHECK(bs.getReadError());
}

BOOST_AUTO_TEST_CASE(overread_flag_sticky_until_clear) {
	ZCom_BitStream bs;
	bs.addInt(0xAB, 8);
	(void)bs.getInt(16);
	BOOST_CHECK(bs.getReadError());
	// A subsequent in-bounds read still works; the flag stays set.
	bs.resetRead();
	BOOST_CHECK_EQUAL(bs.getInt(8), 0xAB);
	BOOST_CHECK(bs.getReadError());
	// clearReadError() resets the flag without touching the data.
	bs.clearReadError();
	BOOST_CHECK(!bs.getReadError());
}

BOOST_AUTO_TEST_CASE(reset_clears_flag) {
	ZCom_BitStream bs;
	bs.addInt(0xAB, 8);
	(void)bs.getInt(16);
	BOOST_CHECK(bs.getReadError());
	bs.reset();
	BOOST_CHECK(!bs.getReadError());
}

BOOST_AUTO_TEST_CASE(clear_clears_flag) {
	ZCom_BitStream bs;
	bs.addInt(0xAB, 8);
	(void)bs.getInt(16);
	BOOST_CHECK(bs.getReadError());
	bs.Clear();
	BOOST_CHECK(!bs.getReadError());
}

BOOST_AUTO_TEST_CASE(assign_clears_flag) {
	ZCom_BitStream bs;
	bs.addInt(0xAB, 8);
	(void)bs.getInt(16);
	BOOST_CHECK(bs.getReadError());
	uint8_t data[] = {0xCD, 0xEF};
	bs.assign(data, 2);
	BOOST_CHECK(!bs.getReadError());
}

BOOST_AUTO_TEST_CASE(copy_preserves_flag) {
	ZCom_BitStream bs;
	bs.addInt(0xAB, 8);
	(void)bs.getInt(16);
	BOOST_CHECK(bs.getReadError());
	ZCom_BitStream copy(bs);
	BOOST_CHECK(copy.getReadError());
}

BOOST_AUTO_TEST_CASE(assign_op_preserves_flag) {
	ZCom_BitStream bs;
	bs.addInt(0xAB, 8);
	(void)bs.getInt(16);
	BOOST_CHECK(bs.getReadError());
	ZCom_BitStream other;
	other.addInt(0, 4);
	other = bs;
	BOOST_CHECK(other.getReadError());
}

// ---- T3.3: bits boundary round-trips ----

BOOST_AUTO_TEST_CASE(add_get_int_32bits_boundary) {
	ZCom_BitStream bs;
	bs.addInt(0xFFFFFFFFu, 32);
	BOOST_CHECK_EQUAL(bs.getBitCount(), 32u);
	BOOST_CHECK_EQUAL(bs.getInt(32), 0xFFFFFFFFu);
	BOOST_CHECK(!bs.getReadError());
}

BOOST_AUTO_TEST_CASE(add_get_signed_int_32bits_boundary) {
	ZCom_BitStream bs;
	bs.addSignedInt(-1, 32);
	BOOST_CHECK_EQUAL(bs.getSignedInt(32), -1);
	BOOST_CHECK(!bs.getReadError());
}

BOOST_AUTO_TEST_CASE(add_get_int64_64bits_boundary) {
	ZCom_BitStream bs;
	bs.addInt64(static_cast<int64_t>(-1), 64);
	BOOST_CHECK_EQUAL(bs.getInt64(64), static_cast<int64_t>(-1));
	BOOST_CHECK(!bs.getReadError());
}

// ---- T2.4: addBitStream preserves bit count and alignment ----

BOOST_AUTO_TEST_CASE(addbitstream_preserves_bitcount) {
	ZCom_BitStream src;
	src.addInt(0x1FFF, 13);
	BOOST_CHECK_EQUAL(src.getBitCount(), 13u);
	ZCom_BitStream dst;
	dst.addBitStream(&src);
	// No byte padding: dst has exactly 13 bits, not 16. The old per-byte
	// addInt(getData()[i], 8) loop would have produced 16 bits here.
	BOOST_CHECK_EQUAL(dst.getBitCount(), 13u);
	BOOST_CHECK_EQUAL(dst.getInt(13), 0x1FFFu);
}

BOOST_AUTO_TEST_CASE(addbitstream_preserves_alignment) {
	ZCom_BitStream src;
	src.addInt(0x1FFF, 13);
	ZCom_BitStream dst;
	dst.addInt(0, 5);		// 5 bits -> non-byte-aligned write position
	dst.addBitStream(&src); // appends 13 bits at bit position 5
	BOOST_CHECK_EQUAL(dst.getBitCount(), 18u);
	BOOST_CHECK_EQUAL(dst.getInt(5), 0u);
	BOOST_CHECK_EQUAL(dst.getInt(13), 0x1FFFu);
}

BOOST_AUTO_TEST_CASE(addbitstream_does_not_touch_flag) {
	// addBitStream resets the source's read position but must not touch its
	// over-read flag (callers may reuse the source afterwards).
	ZCom_BitStream src;
	src.addInt(0xAB, 8);
	src.clearReadError();
	ZCom_BitStream dst;
	dst.addBitStream(&src);
	BOOST_CHECK(!src.getReadError());
	BOOST_CHECK(!dst.getReadError());
}

BOOST_AUTO_TEST_SUITE_END()
