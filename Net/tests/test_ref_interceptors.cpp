// test_ref_interceptors.cpp
// Interceptor tests from Net/ref_tests/test_interceptors.cpp

#include <boost/test/unit_test.hpp>
#include "network_compat.h"

BOOST_AUTO_TEST_SUITE(ref_interceptors)

class TestReplicationInterceptor : public ZCom_NodeReplicationInterceptor {
  public:
	bool called_out_pre_replicate;
	bool called_out_pre_dereplicate;
	bool called_out_pre_update;
	bool called_out_pre_update_item;
	bool called_out_post_update;
	bool called_in_pre_update;
	bool called_in_pre_update_item;
	bool called_in_post_update;

	TestReplicationInterceptor()
		: called_out_pre_replicate(false), called_out_pre_dereplicate(false), called_out_pre_update(false),
		  called_out_pre_update_item(false), called_out_post_update(false), called_in_pre_update(false),
		  called_in_pre_update_item(false), called_in_post_update(false) {}

	void outPreReplicateNode(ZCom_Node *node, uint32_t to, eZCom_NodeRole remote_role) override {
		called_out_pre_replicate = true;
	}
	void outPreDereplicateNode(ZCom_Node *node, uint32_t to, eZCom_NodeRole remote_role) override {
		called_out_pre_dereplicate = true;
	}
	bool outPreUpdate(ZCom_Node *node, uint32_t to, eZCom_NodeRole remote_role) override {
		called_out_pre_update = true;
		return true;
	}
	bool outPreUpdateItem(ZCom_Node *node, uint32_t to, eZCom_NodeRole remote_role,
						  ZCom_Replicator *replicator) override {
		called_out_pre_update_item = true;
		return true;
	}
	void outPostUpdate(ZCom_Node *node, uint32_t to, eZCom_NodeRole remote_role, uint32_t rep_bits, uint32_t event_bits,
					   uint32_t meta_bits) override {
		called_out_post_update = true;
	}
	bool inPreUpdate(ZCom_Node *node, uint32_t from, eZCom_NodeRole remote_role) override {
		called_in_pre_update = true;
		return true;
	}
	bool inPreUpdateItem(ZCom_Node *node, uint32_t from, eZCom_NodeRole remote_role, ZCom_Replicator *replicator,
						 uint32_t estimated_time_sent) override {
		called_in_pre_update_item = true;
		return true;
	}
	void inPostUpdate(ZCom_Node *node, uint32_t from, eZCom_NodeRole remote_role, uint32_t rep_bits,
					  uint32_t event_bits, uint32_t meta_bits) override {
		called_in_post_update = true;
	}
};

class TestEventInterceptor : public ZCom_NodeEventInterceptor {
  public:
	bool recUserEvent(ZCom_Node *, uint32_t, eZCom_NodeRole, ZCom_BitStream &, uint32_t) override {
		return true;
	}
	bool recInit(ZCom_Node *, uint32_t, eZCom_NodeRole) override {
		return true;
	}
	bool recSyncRequest(ZCom_Node *, uint32_t, eZCom_NodeRole) override {
		return true;
	}
	bool recRemoved(ZCom_Node *, uint32_t, eZCom_NodeRole) override {
		return true;
	}
	bool recFileIncoming(ZCom_Node *, uint32_t, eZCom_NodeRole, uint32_t, ZCom_BitStream &) override {
		return true;
	}
	bool recFileData(ZCom_Node *, uint32_t, eZCom_NodeRole, uint32_t) override {
		return true;
	}
	bool recFileAborted(ZCom_Node *, uint32_t, eZCom_NodeRole, uint32_t) override {
		return true;
	}
	bool recFileComplete(ZCom_Node *, uint32_t, eZCom_NodeRole, uint32_t) override {
		return true;
	}
};

BOOST_AUTO_TEST_CASE(interceptor_creation) {
	TestReplicationInterceptor *rep_interceptor = new TestReplicationInterceptor();
	delete rep_interceptor;

	TestEventInterceptor *evt_interceptor = new TestEventInterceptor();
	delete evt_interceptor;
}

BOOST_AUTO_TEST_CASE(interceptor_api) {
	TestReplicationInterceptor rep_interceptor;
	BOOST_CHECK(rep_interceptor.called_out_pre_replicate == false);
	BOOST_CHECK(rep_interceptor.outPreUpdate(NULL, 0, eZCom_RoleProxy) == true);
	BOOST_CHECK(rep_interceptor.called_out_pre_update == true);

	TestEventInterceptor evt_interceptor;
	bool result = evt_interceptor.recInit(NULL, 0, eZCom_RoleAuthority);
	BOOST_CHECK(result == true);
}

BOOST_AUTO_TEST_SUITE_END()
