// test_ref_bitstream.cpp
// BitStream tests from ref_tests:

#include <boost/test/unit_test.hpp>
#include "network_compat.h"
#include <cstring>
#include <cwchar>

BOOST_AUTO_TEST_SUITE(ref_bitstream)

BOOST_AUTO_TEST_CASE(basic_int_ops)
{
	ZCom_BitStream bs;

	bs.addInt(42, 8);
	bs.resetReadState();
	BOOST_CHECK_EQUAL(bs.getInt(8), 42);

	bs.Clear();
	bs.addInt(0xABCD, 16);
	bs.resetReadState();
	BOOST_CHECK_EQUAL(bs.getInt(16), 0xABCD);

	bs.Clear();
	bs.addInt(0x12345678, 32);
	bs.resetReadState();
	BOOST_CHECK_EQUAL(bs.getInt(32), 0x12345678);
}

BOOST_AUTO_TEST_CASE(signed_int_ops)
{
	ZCom_BitStream bs;

	bs.addSignedInt(-1, 8);
	bs.resetReadState();
	BOOST_CHECK_EQUAL(bs.getSignedInt(8), -1);

	bs.Clear();
	bs.addSignedInt(-128, 8);
	bs.resetReadState();
	BOOST_CHECK_EQUAL(bs.getSignedInt(8), -128);

	bs.Clear();
	bs.addSignedInt(127, 8);
	bs.resetReadState();
	BOOST_CHECK_EQUAL(bs.getSignedInt(8), 127);

	bs.Clear();
	bs.addSignedInt(-30000, 16);
	bs.resetReadState();
	BOOST_CHECK_EQUAL(bs.getSignedInt(16), -30000);
}

BOOST_AUTO_TEST_CASE(bool_ops)
{
	ZCom_BitStream bs;

	bs.addBool(true);
	bs.addBool(false);
	bs.addBool(true);
	bs.resetReadState();

	BOOST_CHECK(bs.getBool() == true);
	BOOST_CHECK(bs.getBool() == false);
	BOOST_CHECK(bs.getBool() == true);
}

BOOST_AUTO_TEST_CASE(float_ops)
{
	ZCom_BitStream bs;

	// Use values in [-1.0, 1.0] range for quantized encoding
	float f = 0.75f;
	bs.addFloat(f, 10);
	bs.resetReadState();
	float f2 = bs.getFloat(10);
	float diff = (f > f2) ? (f - f2) : (f2 - f);
	BOOST_CHECK(diff < 0.01f);

	bs.Clear();
	f = 3.14159f;
	bs.addFloat(f, 32);
	bs.resetReadState();
	f2 = bs.getFloat(32);
	diff = (f > f2) ? (f - f2) : (f2 - f);
	BOOST_CHECK(diff < 0.001f);
}

BOOST_AUTO_TEST_CASE(string_ops)
{
	ZCom_BitStream bs;

	const char* test_str = "Hello Zoidcom!";
	bs.addString(test_str);
	bs.resetReadState();

	uint16_t strsize = bs.getStringSize();
	BOOST_CHECK_EQUAL(strsize, strlen(test_str));

	char buf[256];
	bs.getString(buf, sizeof(buf));
	BOOST_CHECK_EQUAL(std::string(buf), std::string(test_str));
}

BOOST_AUTO_TEST_CASE(string_static)
{
	ZCom_BitStream bs;
	bs.addString("Static buffer test");
	bs.resetReadState();
	const char* static_str = bs.getStringStatic();
	BOOST_REQUIRE(static_str);
	BOOST_CHECK_EQUAL(std::string(static_str), "Static buffer test");
}

BOOST_AUTO_TEST_CASE(widestring_ops)
{
	ZCom_BitStream bs;
	const wchar_t* wtest = L"Wide Zoidcom!";

	bs.addStringW(wtest);
	bs.resetReadState();

	uint16_t wlen = bs.getStringWLength();
	BOOST_CHECK_EQUAL(wlen, wcslen(wtest));

	wchar_t wbuf[256];
	bs.getStringW(wbuf, 256);
	BOOST_CHECK(wcscmp(wbuf, wtest) == 0);
}

BOOST_AUTO_TEST_CASE(buffer_ops)
{
	ZCom_BitStream bs;
	unsigned char sbuf[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99 };
	uint16_t slen = 10;

	bs.addBuffer((char*)sbuf, slen);
	bs.resetReadState();

	char rbuf[16];
	memset(rbuf, 0, sizeof(rbuf));
	uint16_t rlen = bs.getBuffer(rbuf, slen);

	BOOST_CHECK_EQUAL(rlen, slen);
	BOOST_CHECK(memcmp(sbuf, rbuf, slen) == 0);
}

BOOST_AUTO_TEST_CASE(nested_bitstream)
{
	ZCom_BitStream inner;
	inner.addInt(0xDE, 8);
	inner.addString("inner");

	ZCom_BitStream outer;
	outer.addInt(1, 8);
	outer.addBitStream(&inner);
	outer.addInt(2, 8);

	outer.resetReadState();
	uint32_t a = outer.getInt(8);
	BOOST_CHECK_EQUAL(a, 1);

	// addBitStream writes: 16-bit bit-count + raw data bytes
	int innerBits = outer.getInt(16);
	BOOST_CHECK(innerBits > 0);

	// Consume the raw inner data bytes
	int innerBytes = (innerBits + 7) / 8;
	for (int i = 0; i < innerBytes; i++)
		outer.getInt(8);

	uint32_t d = outer.getInt(8);
	BOOST_CHECK_EQUAL(d, 2);
}

BOOST_AUTO_TEST_SUITE_END()
