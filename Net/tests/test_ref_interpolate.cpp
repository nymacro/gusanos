// test_ref_interpolate.cpp
// Interpolation replicator tests from Net/ref_tests/test_interpolate.cpp

#include <boost/test/unit_test.hpp>
#include "network_compat.h"

BOOST_AUTO_TEST_SUITE(ref_interpolate)

BOOST_AUTO_TEST_CASE(rsetup_interpolate)
{
	ZCom_RSetupInterpolate<zS32> setup(16, ZCOM_REPFLAG_NONE, ZCOM_REPRULE_AUTH_2_ALL,
	                                     5, 0, -1, -1, 0.4f);

	BOOST_CHECK_EQUAL(setup.getRelevantBits(), 16);
	BOOST_CHECK_EQUAL(setup.ipol_treshold, 5);
	BOOST_CHECK_EQUAL(setup.ipol_factor, 0.4f);

	ZCom_ReplicatorSetup* dup = setup.Duplicate();
	BOOST_REQUIRE(dup);
	delete dup;
}

BOOST_AUTO_TEST_CASE(interpolate_api)
{
	ZCom_RSetupInterpolate<zS32> setup(32, ZCOM_REPFLAG_NONE, ZCOM_REPRULE_AUTH_2_ALL,
	                                     100, 0, -1, -1, 0.5f);
	BOOST_CHECK_EQUAL(setup.ipol_treshold, 100);
	BOOST_CHECK_EQUAL(setup.ipol_factor, 0.5f);

	zS32 val = 42;
	ZCom_Interpolate<zS32, 1>* interp =
		new ZCom_Interpolate<zS32, 1>(&val, 32, ZCOM_REPFLAG_NONE, ZCOM_REPRULE_AUTH_2_ALL, 100);

	BOOST_CHECK_EQUAL(interp->getSize(), 1);

	interp->setRecVal(0, 150);
	BOOST_CHECK_EQUAL(interp->getRecVal(0), 150);

	interp->Process(eZCom_RoleProxy, 16);

	delete interp;
}

BOOST_AUTO_TEST_SUITE_END()
