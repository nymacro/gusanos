// test_ref_address.cpp
// ZoidCom ZCom_Address reference tests from Net/ref_tests/test_address.cpp

#include <boost/test/unit_test.hpp>
#include "network_compat.h"

BOOST_AUTO_TEST_SUITE(ref_address)

BOOST_AUTO_TEST_CASE(default_construction)
{
	ZCom_Address addr;
	BOOST_CHECK(true); // construction succeeds
}

BOOST_AUTO_TEST_CASE(set_ip)
{
	ZCom_Address addr;
	addr.setIP(192, 168, 1, 100);
	addr.setPort(8899);
	addr.setType(eZCom_AddressUDP);
	addr.setControlID(0);

	BOOST_CHECK_EQUAL(addr.getIP(0), 192);
	BOOST_CHECK_EQUAL(addr.getIP(1), 168);
	BOOST_CHECK_EQUAL(addr.getIP(2), 1);
	BOOST_CHECK_EQUAL(addr.getIP(3), 100);
	BOOST_CHECK_EQUAL(addr.getPort(), 8899);
	BOOST_CHECK_EQUAL(addr.getType(), eZCom_AddressUDP);
	BOOST_CHECK_EQUAL(addr.getControlID(), 0);
}

BOOST_AUTO_TEST_CASE(set_address_string)
{
	ZCom_Address addr;
	bool ok = addr.setAddress(eZCom_AddressUDP, 1, "127.0.0.1:8899");
	BOOST_REQUIRE(ok);

	BOOST_CHECK_EQUAL(addr.getPort(), 8899);
	BOOST_CHECK_EQUAL(addr.getType(), eZCom_AddressUDP);
	BOOST_CHECK_EQUAL(addr.getControlID(), 1);
	BOOST_CHECK_EQUAL(addr.getIP(0), 127);
	BOOST_CHECK_EQUAL(addr.getIP(1), 0);
	BOOST_CHECK_EQUAL(addr.getIP(2), 0);
	BOOST_CHECK_EQUAL(addr.getIP(3), 1);
}

BOOST_AUTO_TEST_CASE(equality)
{
	ZCom_Address a, b;
	a.setAddress(eZCom_AddressUDP, 0, "10.0.0.1:8000");
	b.setAddress(eZCom_AddressUDP, 0, "10.0.0.1:8000");
	BOOST_CHECK(a == b);

	ZCom_Address c;
	c.setAddress(eZCom_AddressUDP, 0, "10.0.0.1:8001");
	BOOST_CHECK(!(a == c));
}

BOOST_AUTO_TEST_CASE(copy_assignment)
{
	ZCom_Address a;
	a.setAddress(eZCom_AddressUDP, 2, "192.168.1.1:5000");

	ZCom_Address b(a);
	BOOST_CHECK(b == a);

	ZCom_Address c;
	c = a;
	BOOST_CHECK(c == a);
}

BOOST_AUTO_TEST_CASE(local_address)
{
	ZCom_Address addr;
	addr.setType(eZCom_AddressLocal);
	addr.setPort(1);
	addr.setControlID(0);

	BOOST_CHECK_EQUAL(addr.getType(), eZCom_AddressLocal);
	BOOST_CHECK_EQUAL(addr.getPort(), 1);
}

BOOST_AUTO_TEST_SUITE_END()
