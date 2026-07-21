// test_connection_timeout.cpp
// C6: verify the Zoidcom connection-timeout default and the global setter. The
// value is applied per-peer via enet_peer_timeout by ZCom_Control; this test
// covers the global default stored on the ZoidCom singleton.

#include <boost/test/unit_test.hpp>
#include "net_node.h"

BOOST_AUTO_TEST_SUITE(connection_timeout)

BOOST_AUTO_TEST_CASE(default_is_20000)
{
	BOOST_CHECK_EQUAL(ZoidCom::getConnectionTimeout(), (zU32)20000);
}

BOOST_AUTO_TEST_CASE(set_updates_global_default)
{
	zU32 prev = ZoidCom::getConnectionTimeout();
	ZoidCom::setConnectionTimeout(5000);
	BOOST_CHECK_EQUAL(ZoidCom::getConnectionTimeout(), (zU32)5000);
	// restore so other tests are unaffected
	ZoidCom::setConnectionTimeout(prev);
	BOOST_CHECK_EQUAL(ZoidCom::getConnectionTimeout(), prev);
}

BOOST_AUTO_TEST_SUITE_END()
