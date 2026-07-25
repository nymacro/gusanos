// test_bitstream.cpp
// BitStream serialization tests

#include <boost/test/unit_test.hpp>
#include "net_bitstream.h"

BOOST_AUTO_TEST_SUITE(bitstream)

BOOST_AUTO_TEST_CASE(add_get_int) {
	ZCom_BitStream bs;
	bs.addInt(42, 8);
	BOOST_CHECK_EQUAL(bs.getInt(8), 42);
}

BOOST_AUTO_TEST_CASE(add_get_24bit_int) {
	ZCom_BitStream bs;
	bs.addInt(1234567, 24);
	BOOST_CHECK_EQUAL(bs.getInt(24), 1234567);
}

BOOST_AUTO_TEST_CASE(add_get_signed_positive) {
	ZCom_BitStream bs;
	bs.addSignedInt(127, 8);
	BOOST_CHECK_EQUAL(bs.getSignedInt(8), 127);
}

BOOST_AUTO_TEST_CASE(add_get_signed_negative) {
	ZCom_BitStream bs;
	bs.addSignedInt(-128, 8);
	BOOST_CHECK_EQUAL(bs.getSignedInt(8), -128);
}

BOOST_AUTO_TEST_CASE(add_get_signed_negative_16bit) {
	ZCom_BitStream bs;
	bs.addSignedInt(-30000, 16);
	BOOST_CHECK_EQUAL(bs.getSignedInt(16), -30000);
}

BOOST_AUTO_TEST_CASE(add_get_bool_true) {
	ZCom_BitStream bs;
	bs.addBool(true);
	BOOST_CHECK(bs.getBool());
}

BOOST_AUTO_TEST_CASE(add_get_bool_false) {
	ZCom_BitStream bs;
	bs.addBool(false);
	BOOST_CHECK(!bs.getBool());
}

BOOST_AUTO_TEST_CASE(add_get_float_32bit) {
	ZCom_BitStream bs;
	float val = 3.14159f;
	bs.addFloat(val, 32);
	float result = bs.getFloat(32);
	BOOST_CHECK_CLOSE(result, val, 0.001f);
}

BOOST_AUTO_TEST_CASE(add_get_float_quantized) {
	ZCom_BitStream bs;
	float val = 0.5f;
	bs.addFloat(val, 16);
	float result = bs.getFloat(16);
	BOOST_CHECK_CLOSE(result, val, 1.0f);
}

BOOST_AUTO_TEST_CASE(add_get_string) {
	ZCom_BitStream bs;
	bs.addString("Hello Zoidcom!");
	const char *result = bs.getStringStatic();
	BOOST_REQUIRE(result != nullptr);
	BOOST_CHECK_EQUAL(std::string(result), "Hello Zoidcom!");
}

BOOST_AUTO_TEST_CASE(add_get_empty_string) {
	ZCom_BitStream bs;
	bs.addString("");
	const char *result = bs.getStringStatic();
	BOOST_REQUIRE(result != nullptr);
	BOOST_CHECK_EQUAL(std::string(result), "");
}

BOOST_AUTO_TEST_CASE(add_get_null_string) {
	ZCom_BitStream bs;
	bs.addString(nullptr);
	const char *result = bs.getStringStatic();
	BOOST_REQUIRE(result != nullptr);
	BOOST_CHECK_EQUAL(std::string(result), "");
}

BOOST_AUTO_TEST_CASE(add_get_string_allocating) {
	ZCom_BitStream bs;
	bs.addString("Allocated String");
	std::string result = bs.getString();
	BOOST_REQUIRE(!result.empty());
	BOOST_CHECK_EQUAL(result, "Allocated String");
}

BOOST_AUTO_TEST_CASE(nested_bitstream) {
	ZCom_BitStream inner;
	inner.addInt(99, 8);
	inner.addString("nested");

	ZCom_BitStream outer;
	outer.addInt(0, 8);
	outer.addInt(42, 16);
	outer.addBitStream(&inner);

	BOOST_CHECK_EQUAL(outer.getInt(8), 0);
	BOOST_CHECK_EQUAL(outer.getInt(16), 42);

	// addBitStream inlines the inner bits directly (no length prefix)
	BOOST_CHECK_EQUAL(outer.getInt(8), 99);
	BOOST_CHECK_EQUAL(std::string(outer.getStringStatic()), "nested");
}

BOOST_AUTO_TEST_CASE(duplicate) {
	ZCom_BitStream bs;
	bs.addInt(1, 8);
	bs.addInt(2, 8);
	bs.addInt(3, 8);

	auto dup = bs.Duplicate();
	BOOST_REQUIRE(dup != nullptr);
	BOOST_CHECK_EQUAL(dup->getInt(8), 1);
	BOOST_CHECK_EQUAL(dup->getInt(8), 2);
	BOOST_CHECK_EQUAL(dup->getInt(8), 3);
}

BOOST_AUTO_TEST_CASE(reset_and_reuse) {
	ZCom_BitStream bs;
	bs.addInt(100, 16);
	bs.addInt(200, 16);
	BOOST_CHECK_EQUAL(bs.getInt(16), 100);
	BOOST_CHECK_EQUAL(bs.getInt(16), 200);

	bs.reset();
	bs.addInt(42, 8);
	BOOST_CHECK_EQUAL(bs.getInt(8), 42);
}

BOOST_AUTO_TEST_CASE(mixed_types) {
	ZCom_BitStream bs;
	bs.addInt(255, 8);
	bs.addSignedInt(-1, 8);
	bs.addBool(true);
	bs.addFloat(2.71828f, 32);
	bs.addString("mixed");

	BOOST_CHECK_EQUAL(bs.getInt(8), 255);
	BOOST_CHECK_EQUAL(bs.getSignedInt(8), -1);
	BOOST_CHECK(bs.getBool());
	BOOST_CHECK_CLOSE(bs.getFloat(32), 2.71828f, 0.001f);
	BOOST_CHECK_EQUAL(std::string(bs.getStringStatic()), "mixed");
}

BOOST_AUTO_TEST_CASE(getData_raw_roundtrip) {
	ZCom_BitStream bs;
	bs.addInt(0xDE, 8);
	bs.addInt(0xAD, 8);

	ZCom_BitStream bs2(bs.getData(), (bs.getBitLength() + 7) / 8);
	BOOST_CHECK_EQUAL(bs2.getInt(8), 0xDE);
	BOOST_CHECK_EQUAL(bs2.getInt(8), 0xAD);
}

BOOST_AUTO_TEST_CASE(large_int_values) {
	ZCom_BitStream bs;
	bs.addInt(0xFFFFFFFF, 32);
	BOOST_CHECK_EQUAL(bs.getInt(32), 0xFFFFFFFF);
}

BOOST_AUTO_TEST_CASE(add_get_16bit_ints) {
	ZCom_BitStream bs;
	bs.addInt(12345, 16);
	bs.addInt(0xFFFF, 16);
	BOOST_CHECK_EQUAL(bs.getInt(16), 12345);
	BOOST_CHECK_EQUAL(bs.getInt(16), 0xFFFF);
}

BOOST_AUTO_TEST_CASE(stream_roundtrip_with_node_event_pattern) {
	ZCom_BitStream payload;
	payload.addString("event data");

	ZCom_BitStream pkt;
	pkt.addInt(0, 8);
	pkt.addInt(1, 16);
	pkt.addBitStream(&payload);

	BOOST_CHECK_EQUAL(pkt.getInt(8), 0);
	BOOST_CHECK_EQUAL(pkt.getInt(16), 1);

	// addBitStream inlines the payload bits directly (no length prefix)
	BOOST_CHECK_EQUAL(std::string(pkt.getStringStatic()), "event data");
}

BOOST_AUTO_TEST_SUITE_END()
