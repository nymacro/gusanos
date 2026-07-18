// test_ref_replicators.cpp
// Replicator type tests from Net/ref_tests/test_replicators.cpp

#include <boost/test/unit_test.hpp>
#include "network_compat.h"

BOOST_AUTO_TEST_SUITE(ref_replicators)

BOOST_AUTO_TEST_CASE(replicator_setup)
{
	ZCom_ReplicatorSetup setup(ZCOM_REPFLAG_NONE, ZCOM_REPRULE_AUTH_2_ALL, 5, 100, 500);

	BOOST_CHECK_EQUAL(setup.getFlags(), ZCOM_REPFLAG_NONE);
	BOOST_CHECK_EQUAL(setup.getRules(), ZCOM_REPRULE_AUTH_2_ALL);
	BOOST_CHECK_EQUAL(setup.getInterceptID(), 5);
	BOOST_CHECK_EQUAL(setup.getMinDelay(), 100);
	BOOST_CHECK_EQUAL(setup.getMaxDelay(), 500);

	setup.setInterceptID(10);
	BOOST_CHECK_EQUAL(setup.getInterceptID(), 10);
	setup.setMinDelay(50);
	BOOST_CHECK_EQUAL(setup.getMinDelay(), 50);
	setup.setMaxDelay(1000);
	BOOST_CHECK_EQUAL(setup.getMaxDelay(), 1000);

	auto dup = setup.Duplicate();
	BOOST_REQUIRE(dup);
	BOOST_CHECK_EQUAL(dup->getFlags(), setup.getFlags());
	BOOST_CHECK_EQUAL(dup->getRules(), setup.getRules());

	// SETUPPERSISTS test
	ZCom_ReplicatorSetup setup2(ZCOM_REPFLAG_SETUPPERSISTS, ZCOM_REPRULE_NONE);
	auto dup2 = setup2.Duplicate();
	BOOST_CHECK(dup2.get() != &setup2);
	(void)dup2;
}

BOOST_AUTO_TEST_CASE(rsetup_numeric)
{
	ZCom_RSetupNumeric rsetup(16, ZCOM_REPFLAG_NONE, ZCOM_REPRULE_AUTH_2_ALL, 3, 10, 100);
	BOOST_CHECK_EQUAL(rsetup.getRelevantBits(), 16);
	BOOST_CHECK_EQUAL(rsetup.getInterceptID(), 3);

	rsetup.setRelevantBits(8);
	BOOST_CHECK_EQUAL(rsetup.getRelevantBits(), 8);

	auto dup = rsetup.Duplicate();
	BOOST_REQUIRE(dup);
}

BOOST_AUTO_TEST_CASE(rsetup_string)
{
	ZCom_RSetupString ssetup(128, ZCOM_REPFLAG_NONE, ZCOM_REPRULE_AUTH_2_ALL);
	BOOST_CHECK_EQUAL(ssetup.maxlen, 128);
}

BOOST_AUTO_TEST_CASE(bool_replicator_value)
{
	ZCom_Replicate_Bool* rep = new ZCom_Replicate_Bool(
		false, ZCOM_REPFLAG_NONE, ZCOM_REPRULE_AUTH_2_ALL);

	BOOST_CHECK(rep->getValue() == false);

	rep->setValue(true);
	bool changed = rep->checkState();
	BOOST_CHECK(changed);
	BOOST_CHECK(rep->getValue() == true);

	// Pack/unpack roundtrip
	ZCom_BitStream bs;
	rep->setValue(true);
	rep->checkState();
	rep->packData(&bs);

	ZCom_Replicate_Bool* rep2 = new ZCom_Replicate_Bool(
		false, ZCOM_REPFLAG_NONE, ZCOM_REPRULE_AUTH_2_ALL);

	bs.resetReadState();
	rep2->unpackData(&bs, true, 0);
	BOOST_CHECK(rep2->getValue() == true);

	delete rep;
	delete rep2;
}

BOOST_AUTO_TEST_CASE(bool_replicator_pointer)
{
	bool actual_bool = false;
	ZCom_Replicate_Boolp* repp = new ZCom_Replicate_Boolp(
		&actual_bool, ZCOM_REPFLAG_NONE, ZCOM_REPRULE_AUTH_2_ALL);

	actual_bool = true;
	bool changed = repp->checkState();
	BOOST_CHECK(changed);

	ZCom_BitStream bsp;
	repp->packData(&bsp);

	actual_bool = false;
	ZCom_Replicate_Boolp* repp2 = new ZCom_Replicate_Boolp(
		&actual_bool, ZCOM_REPFLAG_NONE, ZCOM_REPRULE_AUTH_2_ALL);

	bsp.resetReadState();
	repp2->unpackData(&bsp, true, 0);
	BOOST_CHECK(actual_bool == true);

	delete repp;
	delete repp2;
}

BOOST_AUTO_TEST_SUITE_END()
