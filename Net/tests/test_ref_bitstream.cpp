// test_ref_bitstream.cpp
// Reference-compatibility tests for BitStream new features

#include <boost/test/unit_test.hpp>
#include "network_compat.h"
#include <cstring>
#include <cwchar>
#include <string>

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
	BOOST_CHECK_EQUAL(strsize, strlen(test_str) + 1); // length includes null terminator per spec §6.7

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
	BOOST_CHECK_EQUAL(wlen, wcslen(wtest) + 1); // length includes null terminator per spec §6.7

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

	// addBitStream inlines the inner bits directly (no length prefix).
	// Consume the inner data: 1 byte (0xDE) then the string "inner".
	outer.getInt(8); // 0xDE
	BOOST_CHECK_EQUAL(std::string(outer.getStringStatic()), "inner");

	uint32_t d = outer.getInt(8);
	BOOST_CHECK_EQUAL(d, 2);
}

// ---- Copy constructor ----

BOOST_AUTO_TEST_CASE(copy_constructor)
{
	ZCom_BitStream bs;
	bs.addInt(42, 8);
	bs.addString("hello");
	bs.addFloat(3.14f, 32);

	ZCom_BitStream copy(bs);
	BOOST_CHECK_EQUAL(copy.getInt(8), 42);
	BOOST_CHECK_EQUAL(std::string(copy.getStringStatic()), "hello");
	BOOST_CHECK_CLOSE(copy.getFloat(32), 3.14f, 0.001f);
}

// ---- Assignment operator ----

BOOST_AUTO_TEST_CASE(assignment_operator)
{
	ZCom_BitStream bs;
	bs.addInt(100, 16);
	bs.addBool(true);

	ZCom_BitStream copy;
	copy = bs;
	BOOST_CHECK_EQUAL(copy.getInt(16), 100);
	BOOST_CHECK(copy.getBool());
}

// ---- addInt64 / getInt64 ----

BOOST_AUTO_TEST_CASE(add_get_int64_32bit)
{
	ZCom_BitStream bs;
	bs.addInt64(0x12345678, 32);
	BOOST_CHECK_EQUAL(bs.getInt64(32), 0x12345678);
}

BOOST_AUTO_TEST_CASE(add_get_int64_64bit)
{
	ZCom_BitStream bs;
	int64_t val = 0x123456789ABCDEF0LL;
	bs.addInt64(val, 64);
	BOOST_CHECK_EQUAL(bs.getInt64(64), val);
}

BOOST_AUTO_TEST_CASE(add_get_int64_negative)
{
	ZCom_BitStream bs;
	int64_t val = -1234567890123LL;
	bs.addInt64(val, 64);
	BOOST_CHECK_EQUAL(bs.getInt64(64), val);
}

// ---- addDouble / getDouble ----

BOOST_AUTO_TEST_CASE(add_get_double_64bit)
{
	ZCom_BitStream bs;
	double val = 3.14159265358979;
	bs.addDouble(val, 64);
	double result = bs.getDouble(64);
	BOOST_CHECK_CLOSE(result, val, 0.0001);
}

BOOST_AUTO_TEST_CASE(add_get_double_32bit)
{
	ZCom_BitStream bs;
	double val = 2.71828;
	bs.addDouble(val, 32);
	double result = bs.getDouble(32);
	BOOST_CHECK_CLOSE(result, val, 0.001);
}

BOOST_AUTO_TEST_CASE(add_get_double_quantized)
{
	ZCom_BitStream bs;
	double val = 0.5;
	bs.addDouble(val, 16);
	double result = bs.getDouble(16);
	BOOST_CHECK_CLOSE(result, val, 0.01);
}

BOOST_AUTO_TEST_CASE(add_get_double_negative)
{
	ZCom_BitStream bs;
	double val = -2.71828;
	bs.addDouble(val, 64);
	double result = bs.getDouble(64);
	BOOST_CHECK_CLOSE(result, val, 0.0001);
}

// ---- Skip methods ----

BOOST_AUTO_TEST_CASE(skip_int)
{
	ZCom_BitStream bs;
	bs.addInt(10, 8);
	bs.addInt(20, 8);
	bs.addInt(30, 8);

	BOOST_CHECK_EQUAL(bs.getInt(8), 10);
	bs.skipInt(8);
	BOOST_CHECK_EQUAL(bs.getInt(8), 30);
}

BOOST_AUTO_TEST_CASE(skip_bool)
{
	ZCom_BitStream bs;
	bs.addBool(true);
	bs.addBool(false);
	bs.addBool(true);

	BOOST_CHECK(bs.getBool());
	bs.skipBool();
	BOOST_CHECK(bs.getBool());
}

BOOST_AUTO_TEST_CASE(skip_float)
{
	ZCom_BitStream bs;
	bs.addFloat(1.0f, 32);
	bs.addFloat(2.0f, 32);
	bs.addFloat(3.0f, 32);

	bs.skipFloat(32);
	BOOST_CHECK_CLOSE(bs.getFloat(32), 2.0f, 0.001f);
	bs.skipFloat(32);
	BOOST_CHECK(bs.endOfStream());
}

BOOST_AUTO_TEST_CASE(skip_string)
{
	ZCom_BitStream bs;
	bs.addString("skip_me");
	bs.addInt(99, 8);

	bs.skipString();
	BOOST_CHECK_EQUAL(bs.getInt(8), 99);
}

BOOST_AUTO_TEST_CASE(skip_buffer)
{
	ZCom_BitStream bs;
	char buf[] = "bufferdata";
	bs.addBuffer(buf, 10);
	bs.addInt(77, 8);

	bs.skipBuffer(10);
	BOOST_CHECK_EQUAL(bs.getInt(8), 77);
}

BOOST_AUTO_TEST_CASE(skip_bits)
{
	ZCom_BitStream bs;
	bs.addInt(0xFF, 8);
	bs.addInt(0xAA, 8);
	bs.addInt(0x55, 8);

	bs.skipBits(8); // skip 0xFF
	BOOST_CHECK_EQUAL(bs.getInt(8), 0xAA);
}

BOOST_AUTO_TEST_CASE(skip_signed_int)
{
	ZCom_BitStream bs;
	bs.addSignedInt(-100, 16);
	bs.addSignedInt(200, 16);
	bs.addSignedInt(300, 16);

	BOOST_CHECK_EQUAL(bs.getSignedInt(16), -100);
	bs.skipSignedInt(16);
	BOOST_CHECK_EQUAL(bs.getSignedInt(16), 300);
}

// ---- Save/Restore state ----

BOOST_AUTO_TEST_CASE(save_restore_read_state)
{
	ZCom_BitStream bs;
	bs.addInt(1, 8);
	bs.addInt(2, 8);
	bs.addInt(3, 8);

	BOOST_CHECK_EQUAL(bs.getInt(8), 1);

	ZCom_BitStream::BitPos saved;
	bs.saveReadState(saved);
	BOOST_CHECK_EQUAL(bs.getInt(8), 2);

	bs.restoreReadState(saved);
	BOOST_CHECK_EQUAL(bs.getInt(8), 2); // should read 2 again
}

BOOST_AUTO_TEST_CASE(save_restore_write_state)
{
	ZCom_BitStream bs;
	bs.addInt(10, 8);

	ZCom_BitStream::BitPos saved;
	bs.saveWriteState(saved);

	bs.addInt(20, 8); // this write will be undone
	bs.restoreWriteState(saved);

	// Now write something else
	bs.addInt(30, 8);

	BOOST_CHECK_EQUAL(bs.getInt(8), 10);
	BOOST_CHECK_EQUAL(bs.getInt(8), 30);
}

// ---- Stream checks ----

BOOST_AUTO_TEST_CASE(end_of_stream)
{
	ZCom_BitStream bs;
	BOOST_CHECK(bs.endOfStream());

	bs.addInt(42, 8);
	BOOST_CHECK(!bs.endOfStream());

	bs.getInt(8);
	BOOST_CHECK(bs.endOfStream());
}

BOOST_AUTO_TEST_CASE(check_max)
{
	ZCom_BitStream bs;
	bs.addInt(1, 8);
	// After adding 8 bits, checkMax for 1000 may fail (small default vector)
	// This should not crash and return correct result
	bs.checkMax(8); // should succeed
}

BOOST_AUTO_TEST_CASE(get_size_hint)
{
	ZCom_BitStream bs;
	BOOST_CHECK_EQUAL(bs.getSizeHint(), 0);

	bs.addInt(0xFF, 8);
	BOOST_CHECK_EQUAL(bs.getSizeHint(), 1);

	bs.addInt(0xFF, 16);
	BOOST_CHECK_EQUAL(bs.getSizeHint(), 3);
}

// ---- getStringLength ----

BOOST_AUTO_TEST_CASE(get_string_length)
{
	ZCom_BitStream bs;
	bs.addString("test123");

	BOOST_CHECK_EQUAL(bs.getStringLength(), 8);
	BOOST_CHECK_EQUAL(std::string(bs.getStringStatic()), "test123");
}

BOOST_AUTO_TEST_CASE(get_string_length_empty)
{
	ZCom_BitStream bs;
	bs.addString("");

	BOOST_CHECK_EQUAL(bs.getStringLength(), 1);
}

// ---- isEqual ----

BOOST_AUTO_TEST_CASE(is_equal_same)
{
	ZCom_BitStream a;
	a.addInt(1, 8);
	a.addInt(2, 8);

	ZCom_BitStream b;
	b.addInt(1, 8);
	b.addInt(2, 8);

	BOOST_CHECK(a.isEqual(b));
}

BOOST_AUTO_TEST_CASE(is_equal_different)
{
	ZCom_BitStream a;
	a.addInt(1, 8);

	ZCom_BitStream b;
	b.addInt(2, 8);

	BOOST_CHECK(!a.isEqual(b));
}

BOOST_AUTO_TEST_CASE(is_equal_different_length)
{
	ZCom_BitStream a;
	a.addInt(1, 8);
	a.addInt(2, 8);

	ZCom_BitStream b;
	b.addInt(1, 8);

	BOOST_CHECK(!a.isEqual(b));
}

// ---- Serialize / Deserialize ----

BOOST_AUTO_TEST_CASE(serialize_deserialize)
{
	ZCom_BitStream bs;
	bs.addInt(42, 8);
	bs.addString("serialize_test");
	bs.addFloat(3.14f, 32);

	char buf[256];
	uint16_t size = 0;
	BOOST_CHECK(bs.Serialize(buf, &size, sizeof(buf)));
	BOOST_CHECK(size > 0);

	ZCom_BitStream restored;
	BOOST_CHECK(restored.Deserialize(buf, size));

	BOOST_CHECK_EQUAL(restored.getInt(8), 42);
	BOOST_CHECK_EQUAL(std::string(restored.getStringStatic()), "serialize_test");
	BOOST_CHECK_CLOSE(restored.getFloat(32), 3.14f, 0.001f);
}

BOOST_AUTO_TEST_CASE(serialize_buffer_too_small)
{
	ZCom_BitStream bs;
	bs.addInt(42, 8);

	char buf[1];
	uint16_t size = 0;
	BOOST_CHECK(!bs.Serialize(buf, &size, 0)); // max_size=0
}

BOOST_AUTO_TEST_CASE(deserialize_null)
{
	ZCom_BitStream bs;
	BOOST_CHECK(!bs.Deserialize(nullptr, 0));
}

// ---- getBufferMax ----

BOOST_AUTO_TEST_CASE(get_buffer_max)
{
	ZCom_BitStream bs;
	char buf[] = "1234567890";
	bs.addBuffer(buf, 10);

	BOOST_CHECK_EQUAL(bs.getBufferMax(), 10);

	char out[20];
	BOOST_CHECK_EQUAL(bs.getBuffer(out, 20), 10);
	BOOST_CHECK_EQUAL(bs.getBufferMax(), 0);
}

// ---- addStringW / getStringW (already existed but additional tests) ----

BOOST_AUTO_TEST_CASE(add_get_wide_string)
{
	ZCom_BitStream bs;
	bs.addStringW(L"wide_test");

	BOOST_CHECK_EQUAL(bs.getStringWLength(), 10);
	BOOST_CHECK(std::wcscmp(bs.getStringWStatic(), L"wide_test") == 0);
}

BOOST_AUTO_TEST_CASE(add_get_empty_wide_string)
{
	ZCom_BitStream bs;
	bs.addStringW(L"");

	BOOST_CHECK_EQUAL(bs.getStringWLength(), 1);
}

// ---- AddBitStream / GetBitStream ----

BOOST_AUTO_TEST_CASE(add_get_bitstream_roundtrip)
{
	ZCom_BitStream inner;
	inner.addInt(1, 8);
	inner.addInt(2, 8);

	ZCom_BitStream outer;
	outer.addBitStream(&inner);

	// addBitStream inlines the inner bits directly (no length prefix).
	// getBitStream reads the requested number of raw data bits.
	ZCom_BitStream* extracted = outer.getBitStream(16, false);
	BOOST_REQUIRE(extracted != nullptr);
	BOOST_CHECK_EQUAL(extracted->getInt(8), 1);
	BOOST_CHECK_EQUAL(extracted->getInt(8), 2);
	delete extracted;
}

BOOST_AUTO_TEST_CASE(add_bitstream_bits)
{
	// addBitStream inlines the inner bits directly (no length prefix)
	ZCom_BitStream inner;
	inner.addInt(99, 8); // 8 bits

	ZCom_BitStream outer;
	outer.addBitStream(&inner);

	BOOST_CHECK_EQUAL(outer.getInt(8), 99);
}

// ---- Misc: clear and reset ----

BOOST_AUTO_TEST_CASE(clear_preserves_state)
{
	ZCom_BitStream bs;
	bs.addInt(1, 8);
	bs.addInt(2, 8);
	bs.Clear();

	BOOST_CHECK(bs.endOfStream());
	BOOST_CHECK_EQUAL(bs.getSizeHint(), 0);

	bs.addInt(3, 8);
	BOOST_CHECK_EQUAL(bs.getInt(8), 3);
}

BOOST_AUTO_TEST_CASE(multiple_reset_and_reuse)
{
	ZCom_BitStream bs;
	for (int round = 0; round < 3; ++round) {
		bs.reset();
		bs.addInt(round, 8);
		BOOST_CHECK_EQUAL(bs.getInt(8), round);
	}
}

BOOST_AUTO_TEST_CASE(is_equal_empty)
{
	ZCom_BitStream a;
	ZCom_BitStream b;
	BOOST_CHECK(a.isEqual(b));
}

BOOST_AUTO_TEST_CASE(duplicate_from_mid_stream)
{
	ZCom_BitStream bs;
	bs.addInt(1, 8);
	bs.addInt(2, 8);
	bs.addInt(3, 8);

	bs.getInt(8); // read position moves past 1

	ZCom_BitStream* dup = bs.Duplicate();
	BOOST_REQUIRE(dup != nullptr);
	BOOST_CHECK_EQUAL(dup->getInt(8), 2);
	BOOST_CHECK_EQUAL(dup->getInt(8), 3);
	BOOST_CHECK(dup->endOfStream());
	delete dup;
}

// ---- ZCom_TypeHelper tests ----

BOOST_AUTO_TEST_CASE(type_helper_value_type)
{
	ZCom_TypeHelper<int>::value_type v = 42;
	ZCom_TypeHelper<int>::pointer p = &v;
	BOOST_CHECK_EQUAL(*p, 42);
}

BOOST_AUTO_TEST_CASE(type_helper_pointer_type)
{
	int val = 99;
	ZCom_TypeHelper<int*>::value_type v = val;
	ZCom_TypeHelper<int*>::pointer p = &v;
	BOOST_CHECK_EQUAL(*p, 99);
}

// ---- ZCom_ReplicatorValue tests ----

BOOST_AUTO_TEST_CASE(replicator_value_value_type)
{
	ZCom_ReplicatorValue<int, 3> rv;
	int* data = rv.getData();
	BOOST_REQUIRE(data != nullptr);

	BOOST_CHECK(rv.hasChanged()); // starts changed
	BOOST_CHECK(!rv.hasChanged()); // now cleared

	data[0] = 10;
	data[1] = 20;
	data[2] = 30;
	rv.setData(data);
	BOOST_CHECK(rv.hasChanged());
}

BOOST_AUTO_TEST_CASE(replicator_value_set_data_idx)
{
	ZCom_ReplicatorValue<int, 4> rv;
	rv.setData(42, 2);
	BOOST_CHECK(rv.hasChanged());
	BOOST_CHECK_EQUAL(rv.getData()[2], 42);
}

BOOST_AUTO_TEST_CASE(replicator_value_pointer_type)
{
	int vals[3] = {1, 2, 3};
	ZCom_ReplicatorValue<int*, 3> rv(vals);

	// First check - comparator initialized to 0, so 1 != 0 means changed
	BOOST_CHECK(rv.hasChanged());
	BOOST_CHECK_EQUAL(rv.getData()[0], 1);
	BOOST_CHECK_EQUAL(rv.getData()[1], 2);
	BOOST_CHECK_EQUAL(rv.getData()[2], 3);

	// After setChanged(), comparator matches
	rv.setChanged();
	BOOST_CHECK(!rv.hasChanged());

	// Modify data
	vals[1] = 99;
	BOOST_CHECK(rv.hasChanged());
}

BOOST_AUTO_TEST_CASE(replicator_value_pointer_update)
{
	int vals[2] = {5, 10};
	int new_vals[2] = {15, 20};
	ZCom_ReplicatorValue<int*, 2> rv(vals);

	rv.updateData(new_vals);
	BOOST_CHECK_EQUAL(vals[0], 15);
	BOOST_CHECK_EQUAL(vals[1], 20);
}

// ---- ZCom_NodeEventInterceptor derived test ----

class TestEventInterceptor : public ZCom_NodeEventInterceptor {
public:
	bool userEventCalled = false;
	bool recUserEvent(ZCom_Node* node, uint32_t from, eZCom_NodeRole remoterole, ZCom_BitStream& data, uint32_t estimated_time_sent) override {
		userEventCalled = true;
		return true;
	}
};

BOOST_AUTO_TEST_CASE(event_interceptor_derived)
{
	TestEventInterceptor interceptor;
	ZCom_BitStream bs;
	BOOST_CHECK(interceptor.recUserEvent(nullptr, 0, eZCom_RoleUndefined, bs, 0));
	BOOST_CHECK(interceptor.userEventCalled);
}

// ---- ZCom_NodeReplicationInterceptor derived test ----

class TestRepInterceptor : public ZCom_NodeReplicationInterceptor {
public:
	bool preUpdateCalled = false;
	bool outPreUpdate(ZCom_Node* node, uint32_t to, eZCom_NodeRole remote_role) override {
		preUpdateCalled = true;
		return true;
	}
};

BOOST_AUTO_TEST_CASE(rep_interceptor_derived)
{
	TestRepInterceptor interceptor;
	BOOST_CHECK(interceptor.outPreUpdate(nullptr, 0, eZCom_RoleUndefined));
	BOOST_CHECK(interceptor.preUpdateCalled);
}

BOOST_AUTO_TEST_SUITE_END()
