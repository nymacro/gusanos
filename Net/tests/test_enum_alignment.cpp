// test_enum_alignment.cpp
// Phase A: reference-value alignment assertions for enums/constants that
// gained reference values or were newly introduced (address types, block
// mode, file-transfer bit constants, hostname/discover enums).

#include <boost/test/unit_test.hpp>
#include "network_compat.h"

BOOST_AUTO_TEST_SUITE(enum_alignment)

// A5: all four address types set/get round-trip via setType/getType.
BOOST_AUTO_TEST_CASE(address_type_values_and_roundtrip)
{
	BOOST_CHECK_EQUAL(eZCom_AddressLocal, 0);
	BOOST_CHECK_EQUAL(eZCom_AddressTCP, 1);
	BOOST_CHECK_EQUAL(eZCom_AddressUDP, 2);
	BOOST_CHECK_EQUAL(eZCom_AddressBroadcast, 3);

	ZCom_Address addr;
	for (eZCom_AddressType t : {eZCom_AddressLocal, eZCom_AddressTCP, eZCom_AddressUDP, eZCom_AddressBroadcast}) {
		addr.setType(t);
		BOOST_CHECK_EQUAL(addr.getType(), t);
	}

	// ZCom_Control nested alias must equal the file-scope enumerator.
	BOOST_CHECK_EQUAL((int)ZCom_Control::eZCom_AddressUDP, (int)eZCom_AddressUDP);
}

// A5: setAddress stores the type it was given (round-trip for UDP + Local).
BOOST_AUTO_TEST_CASE(set_address_stores_type)
{
	ZCom_Address a;
	BOOST_CHECK(a.setAddress(eZCom_AddressUDP, 0, "127.0.0.1:8899"));
	BOOST_CHECK_EQUAL(a.getType(), eZCom_AddressUDP);

	ZCom_Address b;
	BOOST_CHECK(b.setAddress(eZCom_AddressLocal, 0, nullptr));
	BOOST_CHECK_EQUAL(b.getType(), eZCom_AddressLocal);
}

// A4: block mode enum values (replaces the old inverted #define eZCom_NoBlock 0).
BOOST_AUTO_TEST_CASE(block_mode_values)
{
	BOOST_CHECK_EQUAL(eZCom_Block, 0);
	BOOST_CHECK_EQUAL(eZCom_NoBlock, 1);
	BOOST_CHECK(eZCom_NoBlock != eZCom_Block);
}

// A6: file-transfer bit constants now match reference (ID/SIZE/CHUNK).
BOOST_AUTO_TEST_CASE(ftrans_bit_constants)
{
	BOOST_CHECK_EQUAL(ZCOM_FTRANS_ID_BITS, 32);
	BOOST_CHECK_EQUAL(ZCOM_FTRANS_SIZE_BITS, 32);
	BOOST_CHECK_EQUAL(ZCOM_FTRANS_CHUNK_BITS, 16);
}

// A3/B1: hostname + discover enums present with reference values.
BOOST_AUTO_TEST_CASE(hostname_and_discover_enums)
{
	BOOST_CHECK_EQUAL(eZCom_HostnameIdle, 0);
	BOOST_CHECK_EQUAL(eZCom_HostnameFailed, 1);
	BOOST_CHECK_EQUAL(eZCom_HostnameSuccess, 2);
	BOOST_CHECK_EQUAL(eZCom_HostnameInProgress, 3);

	BOOST_CHECK_EQUAL(eZCom_DiscoverEnable, 0);
	BOOST_CHECK_EQUAL(eZCom_DiscoverDisableAndKeep, 1);
	BOOST_CHECK_EQUAL(eZCom_DiscoverDisable, 2);
}

BOOST_AUTO_TEST_SUITE_END()
