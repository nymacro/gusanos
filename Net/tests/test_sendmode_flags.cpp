// test_sendmode_flags.cpp
// C1: verify the Zoidcom send-mode -> ENet packet-flag mapping, in particular
// that ReliableUnordered maps to RELIABLE | UNSEQUENCED (reliable but
// unordered) rather than plain RELIABLE.

#include <boost/test/unit_test.hpp>
#include "net_control.h"

BOOST_AUTO_TEST_SUITE(sendmode_flags)

BOOST_AUTO_TEST_CASE(reliable_ordered_maps_to_reliable)
{
	BOOST_CHECK_EQUAL(enetPacketFlags(eZCom_ReliableOrdered),
		(enet_uint32)ENET_PACKET_FLAG_RELIABLE);
}

BOOST_AUTO_TEST_CASE(reliable_maps_to_reliable)
{
	BOOST_CHECK_EQUAL(enetPacketFlags(eZCom_Reliable),
		(enet_uint32)ENET_PACKET_FLAG_RELIABLE);
}

BOOST_AUTO_TEST_CASE(reliable_unordered_maps_to_reliable_unsequenced)
{
	BOOST_CHECK_EQUAL(enetPacketFlags(eZCom_ReliableUnordered),
		(enet_uint32)(ENET_PACKET_FLAG_RELIABLE | ENET_PACKET_FLAG_UNSEQUENCED));
}

BOOST_AUTO_TEST_CASE(unreliable_maps_to_zero)
{
	BOOST_CHECK_EQUAL(enetPacketFlags(eZCom_Unreliable), (enet_uint32)0);
}

BOOST_AUTO_TEST_CASE(unreliable_notify_maps_to_zero)
{
	BOOST_CHECK_EQUAL(enetPacketFlags(eZCom_UnreliableNotify), (enet_uint32)0);
}

BOOST_AUTO_TEST_CASE(unknown_mode_maps_to_zero)
{
	BOOST_CHECK_EQUAL(enetPacketFlags((eZCom_SendMode)123), (enet_uint32)0);
}

BOOST_AUTO_TEST_SUITE_END()
