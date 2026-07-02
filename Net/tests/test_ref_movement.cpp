// test_ref_movement.cpp
// Movement replicator tests from Net/ref_tests/test_movement.cpp

#include <boost/test/unit_test.hpp>
#include "network_compat.h"

BOOST_AUTO_TEST_SUITE(ref_movement)

BOOST_AUTO_TEST_CASE(rsetup_movement)
{
	ZCom_RSetupMovement<zFloat> setup(16, ZCOM_REPFLAG_NONE, ZCOM_REPRULE_AUTH_2_ALL);

	BOOST_CHECK_EQUAL(setup.getRelevantBits(), 16);
	BOOST_CHECK_EQUAL(setup.getInputsizeBits(), 8);
	BOOST_CHECK_EQUAL(setup.getExtendedFlags(), 0);
	BOOST_CHECK_EQUAL(setup.getInterpolationTime(), 100);

	setup.setInputsizeBits(10);
	BOOST_CHECK_EQUAL(setup.getInputsizeBits(), 10);

	setup.setInterpolationTime(200);
	BOOST_CHECK_EQUAL(setup.getInterpolationTime(), 200);

	setup.setConstantErrorThreshold(50.0f);
	BOOST_CHECK_EQUAL(setup.getConstantErrorThreshold(), 50.0f);

	ZCom_ReplicatorSetup* dup = setup.Duplicate();
	BOOST_REQUIRE(dup);
	BOOST_CHECK_EQUAL(dup->getFlags(), setup.getFlags());
	BOOST_CHECK_EQUAL(dup->getRules(), setup.getRules());
	delete dup;
}

BOOST_AUTO_TEST_CASE(movement_replicator_creation)
{
	ZCom_RSetupMovement<zFloat>* setup =
		new ZCom_RSetupMovement<zFloat>(23, ZCOM_REPFLAG_NONE, ZCOM_REPRULE_AUTH_2_ALL);
	setup->setInterpolationTime(100);

	ZCom_Replicate_Movement<zFloat, 3>* move =
		new ZCom_Replicate_Movement<zFloat, 3>(setup);
	BOOST_REQUIRE(move);
	move->setTimeScale(0.05f);
	delete move;
	delete setup;

	ZCom_Replicate_Movement<zFloat, 3>* move2 =
		new ZCom_Replicate_Movement<zFloat, 3>(10, ZCOM_REPFLAG_NONE, ZCOM_REPRULE_AUTH_2_ALL);
	BOOST_REQUIRE(move2);
	delete move2;
}

class TestMoveListener : public ZCom_MoveUpdateListener<zFloat, 3>
{
public:
	bool input_updated_called;
	bool input_sent_called;
	bool correction_called;
	bool update_received_called;

	TestMoveListener()
		: input_updated_called(false), input_sent_called(false),
		  correction_called(false), update_received_called(false) {}

	void inputUpdated(ZCom_BitStream& _inputstream, bool _inputchanged,
	                   uint32_t _client_time, uint32_t _estimated_time_sent) override
	{
		(void)_inputstream; (void)_inputchanged; (void)_client_time;
		(void)_estimated_time_sent;
		input_updated_called = true;
	}

	void inputSent(ZCom_BitStream& _inputstream) override
	{
		(void)_inputstream;
		input_sent_called = true;
	}

	void correctionReceived(zFloat* _pos, zFloat* _vel, zFloat* _acc,
	                         bool _teleport, uint32_t _estimated_time_sent) override
	{
		(void)_pos; (void)_vel; (void)_acc; (void)_teleport;
		(void)_estimated_time_sent;
		correction_called = true;
	}

	void updateReceived(ZCom_BitStream& _inputstream, zFloat* _pos,
	                      zFloat* _vel, zFloat* _acc, uint32_t _estimated_time_sent) override
	{
		(void)_inputstream; (void)_pos; (void)_vel; (void)_acc;
		(void)_estimated_time_sent;
		update_received_called = true;
	}
};

BOOST_AUTO_TEST_CASE(movement_listener)
{
	TestMoveListener listener;
	BOOST_CHECK(listener.input_updated_called == false);

	ZCom_RSetupMovement<zFloat>* setup =
		new ZCom_RSetupMovement<zFloat>(23, ZCOM_REPFLAG_NONE, ZCOM_REPRULE_AUTH_2_ALL);
	ZCom_Replicate_Movement<zFloat, 3>* move =
		new ZCom_Replicate_Movement<zFloat, 3>(setup);
	move->setUpdateListener(&listener);

	delete move;
	delete setup;
}

BOOST_AUTO_TEST_SUITE_END()
