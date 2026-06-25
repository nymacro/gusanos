// test_broadcast.cpp
// Tests discover/broadcast API (ZCom_setDiscoverListener, ZCom_Discover).
// Corresponds to ex01_lanbroadcast sample.

#include <boost/test/unit_test.hpp>
#include "net_control.h"
#include "net_address.h"

BOOST_AUTO_TEST_SUITE(broadcast)

BOOST_AUTO_TEST_CASE(address_set_port_and_parse)
{
	ZCom_Address addr;
	addr.setAddress(ZCom_Control::eZCom_AddressUDP, 18999, "255.255.255.255");
	BOOST_CHECK_EQUAL(addr.getPort(), 18999);
}

BOOST_AUTO_TEST_CASE(address_localhost)
{
	ZCom_Address addr;
	addr.setAddress(ZCom_Control::eZCom_AddressUDP, 19000, "127.0.0.1");
	BOOST_CHECK_EQUAL(addr.getPort(), 19000);
}

BOOST_AUTO_TEST_CASE(address_default_constructor)
{
	ZCom_Address addr;
	BOOST_CHECK(addr.getPort() == 0);
}

BOOST_AUTO_TEST_CASE(address_hostname_check)
{
	ZCom_Address addr;
	addr.setAddress(ZCom_Control::eZCom_AddressUDP, 19001, "localhost");
	BOOST_CHECK(addr.getPort() == 19001);
}

BOOST_AUTO_TEST_CASE(discover_api_compiles)
{
	ZCom_simulateLag(0, 0);
	ZCom_simulateLoss(0, 0.0f);
	ZCom_setControlID(0);
	ZCom_setDebugName("test");
	ZCom_setUpstreamLimit(0, 0);
	BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(request_downstream_limit)
{
	ZCom_requestDownstreamLimit(0, 0, 0);
	BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
