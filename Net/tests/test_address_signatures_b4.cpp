// test_address_signatures_b4.cpp
// Phase B4: reference-alignment of ZCom_Address (zoidcom_address.h).
// Compile-time signature checks via member-pointer static_asserts, plus
// behavioral tests for the newly added/retyped methods.

#include <boost/test/unit_test.hpp>
#include "network_compat.h"
#include <cstring>
#include <string>

BOOST_AUTO_TEST_SUITE(address_signatures_b4)

// ---------------------------------------------------------------------------
// Compile-time signature alignment (member-pointer static_asserts).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(signature_alignment)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress"
	// setAddress(eZCom_AddressType, zU8, const char*)
	static_assert(static_cast<bool (ZCom_Address::*)(eZCom_AddressType, zU8, const char*)>(
	                  &ZCom_Address::setAddress),
	              "setAddress signature");
	// getAddressIP(eZCom_GetIPAddressOption) const -> const char*
	static_assert(static_cast<const char* (ZCom_Address::*)(eZCom_GetIPAddressOption) const>(
	                  &ZCom_Address::getAddressIP),
	              "getAddressIP signature");
	// getAddressHostname() const -> const char*
	static_assert(static_cast<const char* (ZCom_Address::*)() const>(&ZCom_Address::getAddressHostname),
	              "getAddressHostname signature");
	// toString() const -> const char*
	static_assert(static_cast<const char* (ZCom_Address::*)() const>(&ZCom_Address::toString),
	              "toString signature");
	// setIP(zU8,zU8,zU8,zU8)
	static_assert(static_cast<void (ZCom_Address::*)(zU8, zU8, zU8, zU8)>(&ZCom_Address::setIP),
	              "setIP(4) signature");
	// setIP(zU32)
	static_assert(static_cast<void (ZCom_Address::*)(zU32)>(&ZCom_Address::setIP),
	              "setIP(zU32) signature");
	// setPort(zU16)
	static_assert(static_cast<void (ZCom_Address::*)(zU16)>(&ZCom_Address::setPort),
	              "setPort signature");
	// setType(eZCom_AddressType)
	static_assert(static_cast<void (ZCom_Address::*)(eZCom_AddressType)>(&ZCom_Address::setType),
	              "setType signature");
	// setControlID(zU8)
	static_assert(static_cast<void (ZCom_Address::*)(zU8)>(&ZCom_Address::setControlID),
	              "setControlID signature");
	// getPort() const -> zU16
	static_assert(static_cast<zU16 (ZCom_Address::*)() const>(&ZCom_Address::getPort),
	              "getPort signature");
	// getIP() const -> zU32
	static_assert(static_cast<zU32 (ZCom_Address::*)() const>(&ZCom_Address::getIP),
	              "getIP() signature");
	// getIP(zU8) const -> zU8
	static_assert(static_cast<zU8 (ZCom_Address::*)(zU8) const>(&ZCom_Address::getIP),
	              "getIP(zU8) signature");
	// getType() const -> eZCom_AddressType
	static_assert(static_cast<eZCom_AddressType (ZCom_Address::*)() const>(&ZCom_Address::getType),
	              "getType signature");
	// getControlID() const -> zU8
	static_assert(static_cast<zU8 (ZCom_Address::*)() const>(&ZCom_Address::getControlID),
	              "getControlID signature");
	// operator==(const ZCom_Address&) const -> bool
	static_assert(static_cast<bool (ZCom_Address::*)(const ZCom_Address&) const>(&ZCom_Address::operator==),
	              "operator== signature");
	// resolveHostname(bool, zU32) -> bool
	static_assert(static_cast<bool (ZCom_Address::*)(bool, zU32)>(&ZCom_Address::resolveHostname),
	              "resolveHostname signature");
	// checkHostname() -> eZCom_HostnameResult
	static_assert(static_cast<eZCom_HostnameResult (ZCom_Address::*)()>(&ZCom_Address::checkHostname),
	              "checkHostname signature");
	// computeHashKey(zU32) const -> zU32
	static_assert(static_cast<zU32 (ZCom_Address::*)(zU32) const>(&ZCom_Address::computeHashKey),
	              "computeHashKey signature");
#pragma GCC diagnostic pop
	BOOST_CHECK(true);
}

// ---------------------------------------------------------------------------
// Behavioral tests.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(set_ip_u32_roundtrip)
{
	ZCom_Address a;
	a.setIP(0xC0A80164u); // 192.168.1.100
	BOOST_CHECK_EQUAL(a.getIP(), 0xC0A80164u);
	BOOST_CHECK_EQUAL(a.getIP(0), 192);
	BOOST_CHECK_EQUAL(a.getIP(1), 168);
	BOOST_CHECK_EQUAL(a.getIP(2), 1);
	BOOST_CHECK_EQUAL(a.getIP(3), 100);
}

BOOST_AUTO_TEST_CASE(get_address_ip_with_and_without_port)
{
	ZCom_Address a;
	a.setIP(127, 0, 0, 1);
	a.setPort(8899);
	BOOST_CHECK(std::string(a.getAddressIP()) == "127.0.0.1:8899");
	BOOST_CHECK(std::string(a.getAddressIP(eZCom_AddressWithoutPort)) == "127.0.0.1");
}

BOOST_AUTO_TEST_CASE(get_address_ip_invalid_returns_null)
{
	ZCom_Address a; // default: invalid
	BOOST_CHECK(a.getAddressIP() == nullptr);
}

BOOST_AUTO_TEST_CASE(get_address_hostname_returns_set_string_or_null)
{
	ZCom_Address a;
	BOOST_CHECK(a.setAddress(eZCom_AddressUDP, 1, "127.0.0.1:8899"));
	BOOST_CHECK(std::string(a.getAddressHostname()) == "127.0.0.1:8899");

	ZCom_Address b; // default: no hostname
	BOOST_CHECK(b.getAddressHostname() == nullptr);
}

BOOST_AUTO_TEST_CASE(toString_local_and_udp_and_invalid)
{
	{
		ZCom_Address a;
		a.setType(eZCom_AddressLocal);
		a.setPort(5);
		BOOST_CHECK(std::string(a.toString()) == "[local]::5");
	}
	{
		ZCom_Address a;
		a.setType(eZCom_AddressUDP);
		a.setIP(127, 0, 0, 1);
		a.setPort(8899);
		BOOST_CHECK(std::string(a.toString()) == "[udp]:127.0.0.1:8899");
	}
	{
		ZCom_Address a; // default invalid
		BOOST_CHECK(std::string(a.toString()) == "(invalid)");
	}
}

BOOST_AUTO_TEST_CASE(resolve_and_check_hostname)
{
	ZCom_Address a; // invalid
	BOOST_CHECK(!a.resolveHostname(false, 0));
	BOOST_CHECK_EQUAL(a.checkHostname(), eZCom_HostnameFailed);

	ZCom_Address b;
	BOOST_CHECK(b.setAddress(eZCom_AddressUDP, 0, "127.0.0.1:8899"));
	BOOST_CHECK(b.resolveHostname(false, 0));
	BOOST_CHECK(b.resolveHostname(true, 0));
	BOOST_CHECK_EQUAL(b.checkHostname(), eZCom_HostnameSuccess);
}

BOOST_AUTO_TEST_CASE(compute_hash_key_range_and_determinism)
{
	ZCom_Address a;
	a.setIP(127, 0, 0, 1);
	a.setPort(8899);
	a.setControlID(1);

	BOOST_CHECK_EQUAL(a.computeHashKey(0), 0); // guard against div-by-zero
	zU32 h1 = a.computeHashKey(1000);
	BOOST_CHECK(h1 < 1000);

	ZCom_Address b;
	b.setIP(127, 0, 0, 1);
	b.setPort(8899);
	b.setControlID(1);
	BOOST_CHECK_EQUAL(a.computeHashKey(1000), b.computeHashKey(1000));
}

BOOST_AUTO_TEST_CASE(control_id_and_port_u8_u16_roundtrip)
{
	ZCom_Address a;
	a.setControlID(5);
	BOOST_CHECK_EQUAL(a.getControlID(), 5);
	a.setPort(65535);
	BOOST_CHECK_EQUAL(a.getPort(), 65535);
	BOOST_CHECK_EQUAL(a.getType(), eZCom_AddressLocal);
}

BOOST_AUTO_TEST_CASE(getType_returns_enum)
{
	ZCom_Address a;
	a.setType(eZCom_AddressUDP);
	BOOST_CHECK_EQUAL(a.getType(), eZCom_AddressUDP);
	BOOST_CHECK(a.getType() == eZCom_AddressUDP);
}

BOOST_AUTO_TEST_SUITE_END()
