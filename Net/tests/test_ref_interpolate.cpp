// test_ref_interpolate.cpp
// Interpolation replicator tests from Net/ref_tests/test_interpolate.cpp

#include <boost/test/unit_test.hpp>
#include "network_compat.h"

BOOST_AUTO_TEST_SUITE(ref_interpolate)

BOOST_AUTO_TEST_CASE(rsetup_interpolate) {
	ZCom_RSetupInterpolate<zS32> setup(16, ZCOM_REPFLAG_NONE, ZCOM_REPRULE_AUTH_2_ALL, 5, 0, -1, -1, 0.4f);

	BOOST_CHECK_EQUAL(setup.getRelevantBits(), 16);
	BOOST_CHECK_EQUAL(setup.ipol_treshold, 5);
	BOOST_CHECK_EQUAL(setup.ipol_factor, 0.4f);

	auto dup = setup.Duplicate();
	BOOST_REQUIRE(dup);
}

BOOST_AUTO_TEST_CASE(interpolate_api) {
	ZCom_RSetupInterpolate<zS32> setup(32, ZCOM_REPFLAG_NONE, ZCOM_REPRULE_AUTH_2_ALL, 100, 0, -1, -1, 0.5f);
	BOOST_CHECK_EQUAL(setup.ipol_treshold, 100);
	BOOST_CHECK_EQUAL(setup.ipol_factor, 0.5f);

	zS32 val = 42;
	ZCom_Interpolate<zS32, 1> *interp =
		new ZCom_Interpolate<zS32, 1>(&val, 32, ZCOM_REPFLAG_NONE, ZCOM_REPRULE_AUTH_2_ALL, 100);

	BOOST_CHECK_EQUAL(interp->getSize(), 1);

	interp->setRecVal(0, 150);
	BOOST_CHECK_EQUAL(interp->getRecVal(0), 150);

	interp->Process(eZCom_RoleProxy, 16);

	delete interp;
}

class CustomProcReplicator : public ZCom_Replicator {
  public:
	eZCom_NodeRole lastRole = eZCom_RoleUndefined;
	zU32 lastTime = 0;
	int callCount = 0;
	void Process(eZCom_NodeRole _localrole, zU32 _simulation_time_passed) override {
		lastRole = _localrole;
		lastTime = _simulation_time_passed;
		callCount++;
	}
};

BOOST_AUTO_TEST_CASE(replicator_process_override_resolution) {
	CustomProcReplicator rep;
	ZCom_Replicator *basePtr = &rep;
	basePtr->Process(eZCom_RoleAuthority, 100);
	BOOST_CHECK_EQUAL(rep.callCount, 1);
	BOOST_CHECK_EQUAL(rep.lastRole, eZCom_RoleAuthority);
	BOOST_CHECK_EQUAL(rep.lastTime, 100u);

	BOOST_CHECK(!rep.callProcess());
	rep.m_flags |= ZCOM_REPLICATOR_CALLPROCESS;
	BOOST_CHECK(rep.callProcess());
}

BOOST_AUTO_TEST_SUITE_END()
