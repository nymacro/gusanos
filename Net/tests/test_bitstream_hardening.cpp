// test_bitstream_hardening.cpp
// Regression tests for BitStream defensive hardening (all unit-level):
//   - T3.1: getReadError() flag set on over-read; sticky until cleared;
//     copied across copy/assign; cleared by reset/Clear/assign.
//   - T3.3: addInt/addSignedInt/getInt at bits=32 and addInt64/getInt64 at
//     bits=64 round-trip (boundary of the new asserts; asserts themselves
//     can't be tested without aborting the process).
//   - T2.4: addBitStream copies exactly getBitCount() bits (no byte padding),
//     preserving bit alignment — the property the routing path now relies on.
//   - Fast paths: byte-aligned memcpy for addString/getString/addBuffer/
//     getBuffer/getBitStream (aligned fast path vs unaligned loop fallthrough),
//     addBitStream self-copy guard, and masked-word addInt/getInt cross-word
//     and bit-width (1/31/32/33) boundaries.

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

// getSignedInt at bits==31 exercises the sign-extension mask: `1 << bits`
// used to be UB here (shift into the sign bit). 31-bit signed range is
// [-2^30, 2^30-1].
BOOST_AUTO_TEST_CASE(add_get_signed_int_31bits_boundary) {
	ZCom_BitStream bs;
	bs.addSignedInt(-1073741824, 31); // -2^30 (sign bit set)
	bs.addSignedInt(1073741823, 31);  // 2^30 - 1 (sign bit clear)
	bs.addSignedInt(-1, 31);		  // all 31 bits set
	BOOST_CHECK_EQUAL(bs.getSignedInt(31), -1073741824);
	BOOST_CHECK_EQUAL(bs.getSignedInt(31), 1073741823);
	BOOST_CHECK_EQUAL(bs.getSignedInt(31), -1);
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

// ---- Byte-aligned memcpy fast paths (strings/buffers/bitstreams) ----

BOOST_AUTO_TEST_CASE(string_fastpath_aligned_roundtrip) {
	ZCom_BitStream bs;
	BOOST_CHECK(bs.addString("hello world"));
	bs.resetReadState();
	BOOST_CHECK_EQUAL(bs.getString(), "hello world");
	BOOST_CHECK(!bs.getReadError());
	BOOST_CHECK(bs.endOfStream());
}

BOOST_AUTO_TEST_CASE(string_fastpath_unaligned_roundtrip) {
	// A leading bool puts the write head at bit 1, forcing the per-byte loop
	// for both write and read (the fast path requires byte alignment).
	ZCom_BitStream bs;
	BOOST_CHECK(bs.addBool(true));
	BOOST_CHECK(bs.addString("offset"));
	bs.resetReadState();
	BOOST_CHECK(bs.getBool());
	BOOST_CHECK_EQUAL(bs.getString(), "offset");
	BOOST_CHECK(!bs.getReadError());
	BOOST_CHECK(bs.endOfStream());
}

BOOST_AUTO_TEST_CASE(string_fastpath_getstringstatic_aligned) {
	ZCom_BitStream bs;
	BOOST_CHECK(bs.addString("static-str"));
	bs.resetReadState();
	BOOST_CHECK_EQUAL(std::string(bs.getStringStatic()), "static-str");
	BOOST_CHECK(!bs.getReadError());
	BOOST_CHECK(bs.endOfStream());
}

BOOST_AUTO_TEST_CASE(string_fastpath_long) {
	// Exercises the multi-byte memcpy path (>64 bytes, past the initial cap).
	std::string s(1000, 'q');
	ZCom_BitStream bs;
	BOOST_CHECK(bs.addString(s.c_str()));
	bs.resetReadState();
	BOOST_CHECK_EQUAL(bs.getString(), s);
	BOOST_CHECK(!bs.getReadError());
	BOOST_CHECK(bs.endOfStream());
}

BOOST_AUTO_TEST_CASE(string_fastpath_empty) {
	ZCom_BitStream bs;
	BOOST_CHECK(bs.addString(""));
	bs.resetReadState();
	BOOST_CHECK_EQUAL(bs.getString(), std::string());
	BOOST_CHECK(!bs.getReadError());
	BOOST_CHECK(bs.endOfStream());
}

BOOST_AUTO_TEST_CASE(buffer_fastpath_aligned_roundtrip) {
	char in[8] = {0, 1, 2, 3, 4, 5, 6, 7};
	ZCom_BitStream bs;
	BOOST_CHECK(bs.addBuffer(in, 8));
	bs.resetReadState();
	char out[8] = {};
	BOOST_CHECK_EQUAL(bs.getBuffer(out, 8), 8u);
	BOOST_CHECK_EQUAL(std::memcmp(in, out, 8), 0);
	BOOST_CHECK(!bs.getReadError());
	BOOST_CHECK(bs.endOfStream());
}

BOOST_AUTO_TEST_CASE(buffer_fastpath_partial_read) {
	// Caller buffer smaller than the stored buffer: copy fits, the rest is
	// skipped, and the return value is the stored length.
	char in[16];
	for (int i = 0; i < 16; ++i)
		in[i] = static_cast<char>(i);
	ZCom_BitStream bs;
	BOOST_CHECK(bs.addBuffer(in, 16));
	BOOST_CHECK(bs.addInt(0xAB, 8));
	bs.resetReadState();
	char out[4] = {};
	BOOST_CHECK_EQUAL(bs.getBuffer(out, 4), 16u);
	BOOST_CHECK_EQUAL(std::memcmp(in, out, 4), 0);
	BOOST_CHECK_EQUAL(bs.getInt(8), 0xABu); // skip landed us after the whole buffer
	BOOST_CHECK(!bs.getReadError());
}

BOOST_AUTO_TEST_CASE(buffer_fastpath_unaligned_roundtrip) {
	ZCom_BitStream bs;
	BOOST_CHECK(bs.addBool(true));
	char in[5] = {1, 2, 3, 4, 5};
	BOOST_CHECK(bs.addBuffer(in, 5));
	bs.resetReadState();
	BOOST_CHECK(bs.getBool());
	char out[5] = {};
	BOOST_CHECK_EQUAL(bs.getBuffer(out, 5), 5u);
	BOOST_CHECK_EQUAL(std::memcmp(in, out, 5), 0);
	BOOST_CHECK(!bs.getReadError());
	BOOST_CHECK(bs.endOfStream());
}

BOOST_AUTO_TEST_CASE(getbitstream_fastpath_whole_bytes) {
	ZCom_BitStream bs;
	bs.addInt(0x0B, 8);
	bs.addInt(0x16, 8);
	bs.resetReadState();
	auto ex = bs.getBitStream(16);
	BOOST_REQUIRE(ex);
	BOOST_CHECK(!bs.getReadError());
	ex->resetReadState();
	BOOST_CHECK_EQUAL(ex->getInt(8), 0x0Bu);
	BOOST_CHECK_EQUAL(ex->getInt(8), 0x16u);
}

BOOST_AUTO_TEST_CASE(getbitstream_fastpath_trailing_bits) {
	// 13 bits, byte-aligned read: 1 byte via memcpy + 5 bits via the loop.
	ZCom_BitStream bs;
	bs.addInt(0x1FFF, 13);
	bs.addInt(0, 3); // pad to a byte boundary
	bs.resetReadState();
	auto ex = bs.getBitStream(13);
	BOOST_REQUIRE(ex);
	BOOST_CHECK(!bs.getReadError());
	ex->resetReadState();
	BOOST_CHECK_EQUAL(ex->getInt(13), 0x1FFFu);
}

BOOST_AUTO_TEST_CASE(getbitstream_overread_sets_flag) {
	ZCom_BitStream bs;
	bs.addInt(0xAB, 8);
	bs.resetReadState();
	auto ex = bs.getBitStream(16); // only 8 bits available
	BOOST_REQUIRE(ex);
	BOOST_CHECK(bs.getReadError());
}

BOOST_AUTO_TEST_CASE(addbitstream_selfcopy_preserves_content) {
	ZCom_BitStream bs;
	bs.addInt(0xAB, 8);
	bs.addInt(0xCD, 8);
	bs.addBitStream(&bs); // self-embed (byte-aligned -> snapshot guard)
	BOOST_CHECK_EQUAL(bs.getBitCount(), 32u);
	bs.resetReadState();
	BOOST_CHECK_EQUAL(bs.getInt(8), 0xABu);
	BOOST_CHECK_EQUAL(bs.getInt(8), 0xCDu);
	BOOST_CHECK_EQUAL(bs.getInt(8), 0xABu); // the embedded copy
	BOOST_CHECK_EQUAL(bs.getInt(8), 0xCDu);
	BOOST_CHECK(!bs.getReadError());
}

// ---- Masked-word int ops: cross-word & bit-width boundaries ----

BOOST_AUTO_TEST_CASE(addint_cross_word_boundary) {
	// 32 bits starting at bit 3 span 5 bytes; the masked-word path must OR
	// (not assign) into the window so prior bits survive.
	ZCom_BitStream bs;
	bs.addInt(0, 3);
	bs.addInt(0xDEADBEEFu, 32);
	bs.addInt(0, 5); // pad to a byte boundary
	bs.resetReadState();
	BOOST_CHECK_EQUAL(bs.getInt(3), 0u);
	BOOST_CHECK_EQUAL(bs.getInt(32), 0xDEADBEEFu);
	BOOST_CHECK(!bs.getReadError());
}

BOOST_AUTO_TEST_CASE(addint_bit_widths_boundary) {
	ZCom_BitStream bs;
	BOOST_CHECK(bs.addInt(1, 1));
	BOOST_CHECK(bs.addInt(0x7FFFFFFFu, 31));
	BOOST_CHECK(bs.addInt(0xFFFFFFFFu, 32));
	bs.addInt64(static_cast<int64_t>(0x1FFFFFFFFULL), 33);
	bs.resetReadState();
	BOOST_CHECK_EQUAL(bs.getInt(1), 1u);
	BOOST_CHECK_EQUAL(bs.getInt(31), 0x7FFFFFFFu);
	BOOST_CHECK_EQUAL(bs.getInt(32), 0xFFFFFFFFu);
	BOOST_CHECK_EQUAL(bs.getInt64(33), static_cast<int64_t>(0x1FFFFFFFFULL));
	BOOST_CHECK(!bs.getReadError());
}

// ---- Item 38 regression tests ----

// addFloat/getFloat with bits<=1: the quantization path used to hit
// shift-by-negative UB (bits==0) and maxVal==0 NaN (bits==1). Both now clamp
// to 2 bits, so add/get stay in sync. With maxVal==1 only -1/0/+1 survive.
BOOST_AUTO_TEST_CASE(addfloat_getfloat_bits_le_one) {
	ZCom_BitStream bs;
	BOOST_CHECK(bs.addFloat(1.0f, 0));	// bits==0 -> clamped to 2
	BOOST_CHECK(bs.addFloat(-1.0f, 1)); // bits==1 -> clamped to 2
	BOOST_CHECK(bs.addFloat(0.0f, 2));
	bs.resetReadState();
	BOOST_CHECK_CLOSE(bs.getFloat(0), 1.0f, 0.001f);
	BOOST_CHECK_CLOSE(bs.getFloat(1), -1.0f, 0.001f);
	BOOST_CHECK_CLOSE(bs.getFloat(2), 0.0f, 0.001f);
	BOOST_CHECK(!bs.getReadError());
}

// Duplicate() with a non-byte-aligned read head: the old raw-byte copy
// retained the consumed partial byte and desynced the duplicate (item 3).
BOOST_AUTO_TEST_CASE(duplicate_unaligned_read_head) {
	ZCom_BitStream bs;
	bs.addInt(0, 3); // 3 leading bits consumed below
	bs.addInt(0x1FFF, 13);
	bs.addInt(0, 2); // pad to 18 bits
	bs.resetReadState();
	(void)bs.getInt(3); // advance read head to bit 3 (non-byte-aligned)
	auto dup = bs.Duplicate();
	BOOST_REQUIRE(dup);
	BOOST_CHECK_EQUAL(dup->getBitCount(), 15u); // 18 - 3 consumed
	BOOST_CHECK_EQUAL(dup->getInt(13), 0x1FFFu);
	BOOST_CHECK_EQUAL(dup->getInt(2), 0u);
	BOOST_CHECK(!dup->getReadError());
}

// Strings at the 64 KB boundary: the 16-bit length prefix counts the null
// terminator, capping payload at 65534 chars; 65535 must be refused, not
// wrapped to a zero-length prefix (item 6).
BOOST_AUTO_TEST_CASE(string_at_64kb_boundary) {
	std::string maxStr(65534, 'z'); // len+1 == 0xFFFF: largest legal
	ZCom_BitStream bs;
	BOOST_CHECK(bs.addString(maxStr.c_str()));
	bs.resetReadState();
	BOOST_CHECK_EQUAL(bs.getString(), maxStr);
	BOOST_CHECK(!bs.getReadError());

	std::string overStr(65535, 'z'); // len+1 == 0x10000: overflows prefix
	ZCom_BitStream bs2;
	BOOST_CHECK(!bs2.addString(overStr.c_str()));
}

// Deserialize replaces content and must clear a prior over-read flag (item 4).
BOOST_AUTO_TEST_CASE(deserialize_clears_readerror) {
	ZCom_BitStream bs;
	bs.addInt(0xAB, 8);
	(void)bs.getInt(16); // over-read: flag set
	BOOST_CHECK(bs.getReadError());
	uint8_t data[] = {0x01, 0x02};
	BOOST_CHECK(bs.Deserialize(reinterpret_cast<char *>(data), 2));
	BOOST_CHECK(!bs.getReadError());
	BOOST_CHECK_EQUAL(bs.getInt(8), 0x01);
	BOOST_CHECK_EQUAL(bs.getInt(8), 0x02);
}

// Skip-path over-read must set the flag (item 5): skipBits/skipString past the
// end are desyncs and must be observable, not silent.
BOOST_AUTO_TEST_CASE(skippath_overread_sets_flag) {
	ZCom_BitStream bs;
	bs.addString("hi"); // 16-bit len(3) + 3 bytes = 40 bits
	bs.resetReadState();
	bs.skipString(); // consume in bounds
	BOOST_CHECK(!bs.getReadError());
	bs.resetReadState();
	bs.skipBits(64); // only 40 bits available -> over-read
	BOOST_CHECK(bs.getReadError());
}

BOOST_AUTO_TEST_SUITE_END()
