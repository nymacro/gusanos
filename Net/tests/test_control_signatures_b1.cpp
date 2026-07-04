// test_control_signatures_b1.cpp
// Phase B1: ZCom_Control reference member signature alignment.
// Compile-checks that every reference member signature exists with the exact
// types (member-pointer assignments force a precise match) plus runtime checks
// for the no-side-effect stubs/getters. Ownership (delete-after-send) is tested
// in Phase D4 (not here).

#include <boost/test/unit_test.hpp>
#include "net_control.h"
#include "net_address.h"
#include "net_bitstream.h"
#include "net_types.h"

BOOST_AUTO_TEST_SUITE(control_signatures_b1)

// Every reference member signature must match exactly; if any impl drifts,
// these assignments fail to compile.
BOOST_AUTO_TEST_CASE(member_signatures_compile_check)
{
	// B1a game-used members
	void (ZCom_Control::*p_sendData)(ZCom_ConnID, ZCom_BitStream*, eZCom_SendMode) = &ZCom_Control::ZCom_sendData;
	void (ZCom_Control::*p_sendAll)(int, ZCom_BitStream*) = &ZCom_Control::sendToAll;
	ZCom_ConnID (ZCom_Control::*p_connect)(const ZCom_Address&, ZCom_BitStream*) = &ZCom_Control::ZCom_Connect;
	void (ZCom_Control::*p_disc)(ZCom_ConnID, ZCom_BitStream*) = &ZCom_Control::ZCom_Disconnect;
	ZCom_ClassID (ZCom_Control::*p_regClass)(const char*, zU32) = &ZCom_Control::ZCom_registerClass;
	ZCom_ClassID (ZCom_Control::*p_getClassID)(const char*) const = &ZCom_Control::ZCom_getClassID;
	ZCom_Node* (ZCom_Control::*p_getNode)(ZCom_NodeID) const = &ZCom_Control::ZCom_getNode;
	const ZCom_Address* (ZCom_Control::*p_getPeer)(ZCom_ConnID) const = &ZCom_Control::ZCom_getPeer;
	const ZCom_ConnStats& (ZCom_Control::*p_stats)(ZCom_ConnID) const = &ZCom_Control::ZCom_getConnectionStats;
	// B1b reference-completeness members
	bool (ZCom_Control::*p_init)(bool, zU16, zU16, zU8) = &ZCom_Control::ZCom_initSockets;
	void (ZCom_Control::*p_setCID)(zU8) = &ZCom_Control::ZCom_setControlID;
	void (ZCom_Control::*p_setName)(const char*) = &ZCom_Control::ZCom_setDebugName;
	void (ZCom_Control::*p_up)(zU32, zU32) = &ZCom_Control::ZCom_setUpstreamLimit;
	void (ZCom_Control::*p_down)(ZCom_ConnID, zU16, zU16) = &ZCom_Control::ZCom_requestDownstreamLimit;
	void (ZCom_Control::*p_lag)(ZCom_ConnID, zU32) = &ZCom_Control::ZCom_simulateLag;
	void (ZCom_Control::*p_loss)(ZCom_ConnID, zFloat) = &ZCom_Control::ZCom_simulateLoss;
	bool (ZCom_Control::*p_zoid)(ZCom_ConnID, zU8) = &ZCom_Control::ZCom_requestZoidMode;
	bool (ZCom_Control::*p_discover)(const ZCom_Address&, ZCom_BitStream*) = &ZCom_Control::ZCom_Discover;
	void (ZCom_Control::*p_setDisc)(eZCom_DiscoverOpt, zU16) = &ZCom_Control::ZCom_setDiscoverListener;
	void (ZCom_Control::*p_setUD)(ZCom_ConnID, void*) = &ZCom_Control::ZCom_setUserData;
	void* (ZCom_Control::*p_getUD)(ZCom_ConnID) const = &ZCom_Control::ZCom_getUserData;
	void (ZCom_Control::*p_sendGroup)(ZCom_GroupID, ZCom_BitStream*, eZCom_SendMode) = &ZCom_Control::ZCom_sendDataToGroup;
	void (ZCom_Control::*p_sendRaw)(ZCom_Address&, void*, zU32) = &ZCom_Control::ZCom_sendDataRaw;
	zU32 (*p_time)() = &ZCom_Control::ZCom_getCurrentTime;
	(void)p_sendData; (void)p_sendAll; (void)p_connect; (void)p_disc; (void)p_regClass;
	(void)p_getClassID; (void)p_getNode; (void)p_getPeer; (void)p_stats; (void)p_init;
	(void)p_setCID; (void)p_setName; (void)p_up; (void)p_down; (void)p_lag; (void)p_loss;
	(void)p_zoid; (void)p_discover; (void)p_setDisc; (void)p_setUD; (void)p_getUD;
	(void)p_sendGroup; (void)p_sendRaw; (void)p_time;
	BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(get_current_time_is_positive)
{
	BOOST_CHECK(ZCom_Control::ZCom_getCurrentTime() > 0);
}

BOOST_AUTO_TEST_CASE(user_data_roundtrip_and_default_null)
{
	ZCom_Control c;
	BOOST_CHECK(c.ZCom_getUserData(7) == nullptr);
	int sentinel = 42;
	c.ZCom_setUserData(7, &sentinel);
	BOOST_CHECK_EQUAL(c.ZCom_getUserData(7), &sentinel);
	BOOST_CHECK(c.ZCom_getUserData(8) == nullptr);
}

BOOST_AUTO_TEST_CASE(control_id_and_debug_name_roundtrip)
{
	ZCom_Control c;
	BOOST_CHECK_EQUAL(c.ZCom_getControlID(), 0);
	c.ZCom_setControlID(5);
	BOOST_CHECK_EQUAL(c.ZCom_getControlID(), 5);
	c.ZCom_setDebugName("test-ctrl");
	BOOST_CHECK_EQUAL(c.ZCom_getDebugName(), "test-ctrl");
}

BOOST_AUTO_TEST_CASE(discover_stub_returns_false)
{
	ZCom_Control c;
	BOOST_CHECK(!c.ZCom_Discover(ZCom_Address{}, nullptr));
}

BOOST_AUTO_TEST_CASE(request_zoid_mode_unknown_conn_returns_false)
{
	ZCom_Control c;
	BOOST_CHECK(!c.ZCom_requestZoidMode(9999, 2));
}

BOOST_AUTO_TEST_CASE(get_peer_and_node_unknown_returns_null)
{
	ZCom_Control c;
	BOOST_CHECK(c.ZCom_getPeer(4242) == nullptr);
	BOOST_CHECK(c.ZCom_getNode(4242) == nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
