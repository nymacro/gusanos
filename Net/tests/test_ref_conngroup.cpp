// test_ref_conngroup.cpp
// Connection Group Manager tests from Net/ref_tests/test_conngroup.cpp

#include <boost/test/unit_test.hpp>
#include "network_compat.h"

BOOST_AUTO_TEST_SUITE(ref_conngroup)

BOOST_AUTO_TEST_CASE(group_creation) {
	// MiniCtrl that provides ZCom_getGroupManager
	ZCom_Control ctrl;
	ZCom_ConnGroupManager &mgr = ctrl.ZCom_getGroupManager();

	uint32_t gid1 = mgr.createGroup(10);
	BOOST_CHECK(gid1 > 0);

	uint32_t gid2 = mgr.createGroup(5);
	BOOST_CHECK(gid2 > 0 && gid2 != gid1);

	BOOST_CHECK(mgr.checkGroupExists(gid1) == true);
	BOOST_CHECK(mgr.checkGroupExists(gid2) == true);

	mgr.destroyGroup(gid1);
	BOOST_CHECK(mgr.checkGroupExists(gid1) == false);
}

BOOST_AUTO_TEST_CASE(group_add_remove) {
	ZCom_Control ctrl;
	ZCom_ConnGroupManager &mgr = ctrl.ZCom_getGroupManager();

	uint32_t gid = mgr.createGroup(10);

	BOOST_CHECK(mgr.addConnection(gid, 100) == true);
	BOOST_CHECK(mgr.addConnection(gid, 200) == true);
	BOOST_CHECK(mgr.addConnection(gid, 300) == true);

	BOOST_CHECK_EQUAL(mgr.getGroupSize(gid), 3);

	uint32_t iterator;
	uint32_t first = mgr.getFirstConnection(gid, iterator);
	BOOST_CHECK(first == 100 || first == 200 || first == 300);

	int count = 0;
	bool ids[3] = {false, false, false};
	uint32_t cur = mgr.getFirstConnection(gid, iterator);
	while (cur != ZCom_Invalid_ID) {
		if (cur == 100)
			ids[0] = true;
		else if (cur == 200)
			ids[1] = true;
		else if (cur == 300)
			ids[2] = true;
		count++;
		cur = mgr.getNextConnection(gid, iterator);
	}
	BOOST_CHECK_EQUAL(count, 3);
	BOOST_CHECK(ids[0] && ids[1] && ids[2]);

	BOOST_CHECK(mgr.removeConnection(gid, 200) == true);
	BOOST_CHECK_EQUAL(mgr.getGroupSize(gid), 2);

	mgr.destroyGroup(gid);
}

BOOST_AUTO_TEST_CASE(all_connections_group) {
	ZCom_Control ctrl;
	ZCom_ConnGroupManager &mgr = ctrl.ZCom_getGroupManager();

	BOOST_CHECK(mgr.checkGroupExists(ZCOM_CONNGROUP_ALL) == true);
}

BOOST_AUTO_TEST_SUITE_END()
