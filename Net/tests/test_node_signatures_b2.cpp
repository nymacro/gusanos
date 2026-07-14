// test_node_signatures_b2.cpp
// Phase B2: ZCom_Node signature alignment (reference zoidcom_node.h).
// Verifies: getNextEvent 4th optional param, getRole() returns eZCom_NodeRole,
// addReplication* min/max-delay default overloads, setInterceptID(ZCom_InterceptID=zU8),
// registerNodeUnique(eZCom_NodeRole).

#include <boost/test/unit_test.hpp>
#include "net_node.h"

BOOST_AUTO_TEST_SUITE(node_signatures_b2)

// getNextEvent: 4th optional out-param (_estimated_time_sent) is populated.
BOOST_AUTO_TEST_CASE(get_next_event_fourth_param)
{
	ZCom_Node node;
	node.pushEvent(eZCom_EventUser, eZCom_RoleAuthority, 5, nullptr);

	eZCom_Event type = eZCom_EventNoEvent;
	eZCom_NodeRole role = eZCom_RoleUndefined;
	ZCom_ConnID conn = 0;
	zU32 sent = 0xFFFFFFFF;

	auto data = node.getNextEvent(&type, &role, &conn, &sent);
	BOOST_CHECK_EQUAL(type, eZCom_EventUser);
	BOOST_CHECK_EQUAL(role, eZCom_RoleAuthority);
	BOOST_CHECK_EQUAL(conn, 5u);
	BOOST_CHECK_EQUAL(sent, 0u); // populated (0 = no estimate yet)
	BOOST_CHECK(data == nullptr); // we pushed a null bitstream
}

// getNextEvent: 3-arg form (game/updater.cpp usage) still works — source-compatible.
BOOST_AUTO_TEST_CASE(get_next_event_three_arg_form)
{
	ZCom_Node node;
	node.pushEvent(eZCom_EventInit, eZCom_RoleProxy, 9, nullptr);

	eZCom_Event type = eZCom_EventNoEvent;
	eZCom_NodeRole role = eZCom_RoleUndefined;
	ZCom_ConnID conn = 0;

	auto data = node.getNextEvent(&type, &role, &conn);
	BOOST_CHECK_EQUAL(type, eZCom_EventInit);
	BOOST_CHECK_EQUAL(role, eZCom_RoleProxy);
	BOOST_CHECK_EQUAL(conn, 9u);
	BOOST_CHECK(data == nullptr);
}

// getRole() returns eZCom_NodeRole (not int) — assignment to eZCom_NodeRole must compile.
BOOST_AUTO_TEST_CASE(get_role_returns_enum)
{
	ZCom_Node node;
	node.setRole(eZCom_RoleProxy);
	eZCom_NodeRole r = node.getRole(); // compile-time type check
	BOOST_CHECK_EQUAL(r, eZCom_RoleProxy);

	node.setRole(eZCom_RoleAuthority);
	BOOST_CHECK_EQUAL(node.getRole(), eZCom_RoleAuthority);

	node.setRole(eZCom_RoleOwner);
	BOOST_CHECK_EQUAL(node.getRole(), eZCom_RoleOwner);
}

// setInterceptID takes ZCom_InterceptID (zU8): literal + cast forms compile.
BOOST_AUTO_TEST_CASE(set_intercept_id_zU8_param)
{
	ZCom_Node node;
	BOOST_CHECK_NO_THROW(node.setInterceptID(0));
	BOOST_CHECK_NO_THROW(node.setInterceptID(7));
	BOOST_CHECK_NO_THROW(node.setInterceptID(static_cast<ZCom_InterceptID>(5)));
}

// addReplicationInt: 5-arg (default delays) and 6-arg (explicit mindelay) overloads.
BOOST_AUTO_TEST_CASE(add_replication_int_delay_overloads)
{
	ZCom_Node node;
	int32_t v1 = 0, v2 = 0;
	BOOST_CHECK_NO_THROW(node.addReplicationInt(&v1, 32, false,
		ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL)); // 5-arg, defaults
	BOOST_CHECK_NO_THROW(node.addReplicationInt(&v2, 16, true,
		ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL, 0)); // 6-arg, mindelay=0
}

// addReplicationFloat / addReplicationBool: reference default-delay overloads.
BOOST_AUTO_TEST_CASE(add_replication_float_bool_defaults)
{
	ZCom_Node node;
	float f = 0.0f;
	bool b = false;
	BOOST_CHECK_NO_THROW(node.addReplicationFloat(&f, 16,
		ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL)); // 4-arg, defaults
	BOOST_CHECK_NO_THROW(node.addReplicationFloat(&f, 16,
		ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL, 50, 200)); // 6-arg
	BOOST_CHECK_NO_THROW(node.addReplicationBool(&b,
		ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL)); // 3-arg, defaults
	BOOST_CHECK_NO_THROW(node.addReplicationBool(&b,
		ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL, 50, 200)); // 5-arg
}

// registerNodeUnique takes eZCom_NodeRole (enum) — compiles with role enumerators.
BOOST_AUTO_TEST_CASE(register_node_unique_enum_role)
{
	ZCom_Node node;
	// nullptr control → returns false (no crash); verifies 2nd param type match.
	BOOST_CHECK_EQUAL(node.registerNodeUnique(1, eZCom_RoleAuthority, nullptr), false);
	BOOST_CHECK_EQUAL(node.registerNodeUnique(1, eZCom_RoleProxy, nullptr), false);
}

BOOST_AUTO_TEST_SUITE_END()
