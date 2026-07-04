// Phase B3 — ZCom_BitStream reference-signature alignment tests.
// Verifies exact method signatures (via member-pointer static_asserts) and
// the behavioral fixes that the type-widening necessitated (1u<<i for 32-bit
// values, skipBits for large skipString/skipBuffer payloads).

#include "net_bitstream.h"
#include "net_types.h"
#include <boost/test/unit_test.hpp>
#include <string>
#include <type_traits>

// ---------------------------------------------------------------------------
// Compile-time signature checks (exact match against the Zoidcom reference).
// A failure here is a compile error, failing the whole build — intended.
// ---------------------------------------------------------------------------
#define ZBS_CHECK_SIG(MEMBER, EXPECTED) \
	static_assert(std::is_same< decltype(&ZCom_BitStream::MEMBER), EXPECTED >::value, \
	              #MEMBER " signature mismatch")

ZBS_CHECK_SIG(addInt,         bool (ZCom_BitStream::*)(zU32, zU8));
ZBS_CHECK_SIG(addSignedInt,    bool (ZCom_BitStream::*)(zS32, zU8));
ZBS_CHECK_SIG(addBool,         bool (ZCom_BitStream::*)(bool));
ZBS_CHECK_SIG(addFloat,        bool (ZCom_BitStream::*)(zFloat, zU8));
ZBS_CHECK_SIG(addString,       bool (ZCom_BitStream::*)(const char*));
ZBS_CHECK_SIG(addStringW,      bool (ZCom_BitStream::*)(const wchar_t*));
ZBS_CHECK_SIG(addBuffer,       bool (ZCom_BitStream::*)(const char*, zU16));
ZBS_CHECK_SIG(addBitStream,    bool (ZCom_BitStream::*)(ZCom_BitStream*, bool));

ZBS_CHECK_SIG(getInt,          zU32 (ZCom_BitStream::*)(zU8));
ZBS_CHECK_SIG(getSignedInt,    zS32 (ZCom_BitStream::*)(zU8));
ZBS_CHECK_SIG(getBool,         bool (ZCom_BitStream::*)());
ZBS_CHECK_SIG(getFloat,        zFloat (ZCom_BitStream::*)(zU8));
ZBS_CHECK_SIG(getStringStatic, const char* (ZCom_BitStream::*)());
static_assert(std::is_same< decltype(static_cast<void (ZCom_BitStream::*)(char*, zU16)>(&ZCom_BitStream::getString)), void (ZCom_BitStream::*)(char*, zU16) >::value, "getString(char*,zU16) overload missing");
ZBS_CHECK_SIG(getStringW,      void (ZCom_BitStream::*)(wchar_t*, zU16));
ZBS_CHECK_SIG(getStringSize,   zU16 (ZCom_BitStream::*)());
ZBS_CHECK_SIG(getStringLength, zU16 (ZCom_BitStream::*)());
ZBS_CHECK_SIG(getStringWLength,zU16 (ZCom_BitStream::*)());
ZBS_CHECK_SIG(getBuffer,       zU16 (ZCom_BitStream::*)(char*, zU16));
ZBS_CHECK_SIG(getBufferMax,    zU16 (ZCom_BitStream::*)());
ZBS_CHECK_SIG(getBitStream,    ZCom_BitStream* (ZCom_BitStream::*)(zU32, bool));
ZBS_CHECK_SIG(getBitCount,     zU32 (ZCom_BitStream::*)() const);

ZBS_CHECK_SIG(skipInt,         void (ZCom_BitStream::*)(zU8));
ZBS_CHECK_SIG(skipSignedInt,   void (ZCom_BitStream::*)(zU8));
ZBS_CHECK_SIG(skipFloat,       void (ZCom_BitStream::*)(zU8));
ZBS_CHECK_SIG(skipString,      void (ZCom_BitStream::*)());
ZBS_CHECK_SIG(skipBuffer,      void (ZCom_BitStream::*)(zU16));
ZBS_CHECK_SIG(skipBits,        void (ZCom_BitStream::*)(zU32));

ZBS_CHECK_SIG(saveWriteState,  void (ZCom_BitStream::*)(ZCom_BitStream::BitPos&) const);
ZBS_CHECK_SIG(restoreWriteState, void (ZCom_BitStream::*)(const ZCom_BitStream::BitPos&));
ZBS_CHECK_SIG(saveReadState,   void (ZCom_BitStream::*)(ZCom_BitStream::BitPos&) const);
ZBS_CHECK_SIG(restoreReadState,void (ZCom_BitStream::*)(const ZCom_BitStream::BitPos&));
ZBS_CHECK_SIG(resetReadState,  void (ZCom_BitStream::*)());
ZBS_CHECK_SIG(logReadState,    void (ZCom_BitStream::*)());
ZBS_CHECK_SIG(logWriteState,   void (ZCom_BitStream::*)());

ZBS_CHECK_SIG(checkMax,        bool (ZCom_BitStream::*)(zU32) const);
ZBS_CHECK_SIG(checkFull,       bool (ZCom_BitStream::*)() const);
ZBS_CHECK_SIG(endOfStream,     bool (ZCom_BitStream::*)() const);
ZBS_CHECK_SIG(getSizeHint,     zU16 (ZCom_BitStream::*)() const);

ZBS_CHECK_SIG(Serialize,       bool (ZCom_BitStream::*)(char*, zU16*, zU16));
ZBS_CHECK_SIG(Deserialize,    bool (ZCom_BitStream::*)(char*, zU16));
ZBS_CHECK_SIG(isEqual,         bool (ZCom_BitStream::*)(const ZCom_BitStream&) const);
ZBS_CHECK_SIG(Duplicate,       ZCom_BitStream* (ZCom_BitStream::*)() const);

#undef ZBS_CHECK_SIG

// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_SUITE(bitstream_signatures_b3)

// add* now return bool and the value is usable (not just ignorable).
BOOST_AUTO_TEST_CASE(add_methods_return_bool)
{
	ZCom_BitStream bs;
	bool r = false;
	r = bs.addInt(1, 8);          BOOST_CHECK(r);
	r = bs.addSignedInt(-1, 8);   BOOST_CHECK(r);
	r = bs.addBool(true);         BOOST_CHECK(r);
	r = bs.addFloat(1.0f, 32);    BOOST_CHECK(r);
	r = bs.addString("hi");       BOOST_CHECK(r);
	r = bs.addStringW(L"hi");     BOOST_CHECK(r);
	r = bs.addBuffer("ab", 2);     BOOST_CHECK(r);
	BOOST_CHECK(bs.addBitStream(&bs) == true);   // default _allow_align=false
	BOOST_CHECK(bs.addBitStream(&bs, false) == true);
	BOOST_CHECK(bs.addBitStream(nullptr, false) == false);
}

// getInt returns zU32 — bit 31 must survive (validates the 1u<<i fix).
BOOST_AUTO_TEST_CASE(get_int_32bit_high_bit)
{
	ZCom_BitStream bs;
	BOOST_CHECK(bs.addInt(0x80000000u, 32));
	BOOST_CHECK(bs.addInt(0xFFFFFFFFu, 32));
	bs.resetReadState();
	BOOST_CHECK_EQUAL(bs.getInt(32), 0x80000000u);
	BOOST_CHECK_EQUAL(bs.getInt(32), 0xFFFFFFFFu);

	// zU32 return type is observable via assignment.
	zU32 v = bs.getInt(0);
	(void)v;
}

// getSignedInt returns zS32; sign extension unchanged for the common cases.
BOOST_AUTO_TEST_CASE(get_signed_int_behaviour)
{
	ZCom_BitStream bs;
	bs.addSignedInt(-1, 8);
	bs.addSignedInt(127, 8);
	bs.addSignedInt(-30000, 16);
	bs.resetReadState();
	BOOST_CHECK_EQUAL(bs.getSignedInt(8), -1);
	BOOST_CHECK_EQUAL(bs.getSignedInt(8), 127);
	BOOST_CHECK_EQUAL(bs.getSignedInt(16), -30000);
}

// ctor accepts the reference _maxfill hint (defaulted) and still works.
BOOST_AUTO_TEST_CASE(ctor_maxfill_hint)
{
	ZCom_BitStream a;        // default _maxfill=64
	ZCom_BitStream b(128);   // explicit hint
	ZCom_BitStream c(0);     // zero hint: no reserve, still usable
	a.addInt(7, 8);
	b.addInt(7, 8);
	c.addInt(7, 8);
	a.resetReadState(); b.resetReadState(); c.resetReadState();
	BOOST_CHECK_EQUAL(a.getInt(8), 7);
	BOOST_CHECK_EQUAL(b.getInt(8), 7);
	BOOST_CHECK_EQUAL(c.getInt(8), 7);
}

// Duplicate() is now const — callable on a const reference.
BOOST_AUTO_TEST_CASE(duplicate_is_const)
{
	ZCom_BitStream bs;
	bs.addInt(42, 8);
	const ZCom_BitStream& cref = bs;
	ZCom_BitStream* dup = cref.Duplicate();
	BOOST_REQUIRE(dup != nullptr);
	dup->resetReadState();
	BOOST_CHECK_EQUAL(dup->getInt(8), 42);
	delete dup;
}

// getString/getStringW maxsize is zU16; sizeof()/literals still bind.
BOOST_AUTO_TEST_CASE(get_string_maxsize_zu16)
{
	ZCom_BitStream bs;
	bs.addString("hello");
	bs.resetReadState();
	char buf[16];
	bs.getString(buf, sizeof(buf));   // size_t -> zU16 narrowing (literal-ish)
	BOOST_CHECK_EQUAL(std::string(buf), "hello");

	ZCom_BitStream ws;
	ws.addStringW(L"yo");
	ws.resetReadState();
	wchar_t wbuf[8];
	ws.getStringW(wbuf, 8);
	BOOST_CHECK(std::wstring(wbuf) == L"yo");
}

// addBitStream/getBitStream with the _allow_align default (1-arg) form.
BOOST_AUTO_TEST_CASE(bitstream_allow_align_default)
{
	ZCom_BitStream inner;
	inner.addInt(11, 8);
	inner.addInt(22, 8);

	ZCom_BitStream outer;
	BOOST_CHECK(outer.addBitStream(&inner));      // default _allow_align=false
	outer.resetReadState();
	int bits = static_cast<int>(outer.getInt(16));
	BOOST_CHECK_EQUAL(bits, 16);

	ZCom_BitStream* ex = outer.getBitStream(16); // default _allow_align=false
	BOOST_REQUIRE(ex != nullptr);
	ex->resetReadState();
	BOOST_CHECK_EQUAL(ex->getInt(8), 11);
	BOOST_CHECK_EQUAL(ex->getInt(8), 22);
	delete ex;
}

// skipBuffer over a buffer larger than 31 bytes — validates the skipBits fix
// (the old skipInt(actualLen*8) would truncate to zU8 and skip the wrong amount).
BOOST_AUTO_TEST_CASE(skip_buffer_large)
{
	ZCom_BitStream bs;
	char buf[64];
	for (int i = 0; i < 64; ++i) buf[i] = static_cast<char>(i);
	BOOST_CHECK(bs.addBuffer(buf, 64));
	BOOST_CHECK(bs.addInt(0xAB, 8));
	bs.resetReadState();
	bs.skipBuffer(64);
	BOOST_CHECK_EQUAL(bs.getInt(8), 0xABu);
}

// skipString over a long string (>31 chars) — validates the skipBits fix.
BOOST_AUTO_TEST_CASE(skip_string_large)
{
	ZCom_BitStream bs;
	std::string s(100, 'x');
	BOOST_CHECK(bs.addString(s.c_str()));
	BOOST_CHECK(bs.addInt(0xCD, 8));
	bs.resetReadState();
	bs.skipString();
	BOOST_CHECK_EQUAL(bs.getInt(8), 0xCDu);
}

// logReadState/logWriteState exist and are callable (no-op stubs).
BOOST_AUTO_TEST_CASE(log_state_stubs_callable)
{
	ZCom_BitStream bs;
	bs.addInt(1, 8);
	bs.logReadState();
	bs.logWriteState();
	BOOST_CHECK(true);
}

// operator new/delete exist and round-trip via new/delete.
BOOST_AUTO_TEST_CASE(custom_new_delete)
{
	ZCom_BitStream* p = new ZCom_BitStream(32);
	BOOST_REQUIRE(p != nullptr);
	p->addInt(5, 8);
	p->resetReadState();
	BOOST_CHECK_EQUAL(p->getInt(8), 5);
	delete p;
}

// getBitCount returns zU32 and remains usable where size_t was expected.
BOOST_AUTO_TEST_CASE(get_bit_count_zu32)
{
	ZCom_BitStream bs;
	BOOST_CHECK_EQUAL(bs.getBitCount(), zU32(0));
	bs.addInt(0, 8);
	BOOST_CHECK_EQUAL(bs.getBitCount(), zU32(8));
	bs.addInt(0, 16);
	BOOST_CHECK_EQUAL(bs.getBitCount(), zU32(24));
}

BOOST_AUTO_TEST_SUITE_END()
