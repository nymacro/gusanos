// test_node_id_recycle.cpp
// Item 21: node-ID exhaustion. Node IDs are written as 16 bits on the wire
// (announcements, events, replicators), so the allocator must keep them in
// [1, 65535] and recycle freed IDs instead of wrapping the wire field. A
// long match with many transient per-particle ZCom_Nodes would otherwise
// overflow 65535 registrations.

#include <boost/test/unit_test.hpp>
#include <set>
#include <vector>

#include "net_control.h"
#include "net_node.h"

BOOST_AUTO_TEST_SUITE(node_id_recycle)

// Basic recycling: a freed ID returns to the free-list and is reused
// (lowest-first) before the fresh counter advances.
BOOST_AUTO_TEST_CASE(node_id_recycles_freed_ids) {
	g_currentControl = nullptr;
	ZCom_Control ctrl;
	g_currentControl = &ctrl;

	ZCom_Node *a = new ZCom_Node();
	ZCom_Node *b = new ZCom_Node();
	ZCom_Node *c = new ZCom_Node();
	BOOST_REQUIRE(ctrl.registerNode(a));
	BOOST_REQUIRE(ctrl.registerNode(b));
	BOOST_REQUIRE(ctrl.registerNode(c));
	BOOST_CHECK_EQUAL(a->getNetworkID(), 1u);
	BOOST_CHECK_EQUAL(b->getNetworkID(), 2u);
	BOOST_CHECK_EQUAL(c->getNetworkID(), 3u);

	// Free the middle node; ID 2 returns to the free-list (~ZCom_Node
	// -> unregisterNode -> removeNode -> releaseNodeID).
	delete b;

	// Next registration recycles the freed ID 2, not the next fresh ID 4.
	ZCom_Node *d = new ZCom_Node();
	BOOST_REQUIRE(ctrl.registerNode(d));
	BOOST_CHECK_EQUAL(d->getNetworkID(), 2u);

	// Free-list empty again -> the fresh counter resumes at 4.
	ZCom_Node *e = new ZCom_Node();
	BOOST_REQUIRE(ctrl.registerNode(e));
	BOOST_CHECK_EQUAL(e->getNetworkID(), 4u);

	delete a;
	delete c;
	delete d;
	delete e;
	g_currentControl = nullptr;
	ctrl.Shutdown();
}

// Exhaustion + recycling at the 16-bit boundary. Filling the whole space
// must exhaust the allocator (next registration fails rather than wrapping);
// freeing one ID lets the next registration recycle it; a larger free+realloc
// batch is served entirely from the free-list (no fresh IDs remain past MAX).
BOOST_AUTO_TEST_CASE(node_id_exhaustion_recycles_past_16bit) {
	g_currentControl = nullptr;
	ZCom_Control ctrl;
	g_currentControl = &ctrl;
	constexpr uint32_t MAX = ZCom_Control::MAX_NODE_ID;

	std::vector<ZCom_Node *> live;
	live.reserve(MAX);
	for (uint32_t i = 0; i < MAX; ++i) {
		ZCom_Node *n = new ZCom_Node();
		BOOST_REQUIRE(ctrl.registerNode(n));
		uint32_t id = n->getNetworkID();
		BOOST_REQUIRE_GE(id, 1u);
		BOOST_REQUIRE_LE(id, MAX);
		live.push_back(n);
	}
	BOOST_REQUIRE_EQUAL(live.size(), MAX);

	// Space exhausted: the next allocation fails and leaves nodeID at 0.
	ZCom_Node *extra = new ZCom_Node();
	BOOST_CHECK(!ctrl.registerNode(extra));
	BOOST_CHECK_EQUAL(extra->getNetworkID(), 0u);
	delete extra; // never registered -> ~ZCom_Node unregisterNode is a no-op

	// Free one node; its ID must be recycled by the next registration.
	uint32_t freedID = live.back()->getNetworkID();
	delete live.back();
	live.pop_back();
	ZCom_Node *reused = new ZCom_Node();
	BOOST_REQUIRE(ctrl.registerNode(reused));
	BOOST_CHECK_EQUAL(reused->getNetworkID(), freedID);
	live.push_back(reused);

	// Free a batch, then re-register the same count. The space is full
	// (m_nextNodeID > MAX), so every new ID must be a recycled freed ID.
	const uint32_t batch = 4096;
	std::set<uint32_t> freed;
	for (uint32_t i = 0; i < batch; ++i) {
		freed.insert(live.back()->getNetworkID());
		delete live.back();
		live.pop_back();
	}
	for (uint32_t i = 0; i < batch; ++i) {
		ZCom_Node *n = new ZCom_Node();
		BOOST_REQUIRE(ctrl.registerNode(n));
		uint32_t id = n->getNetworkID();
		BOOST_REQUIRE_GE(id, 1u);
		BOOST_REQUIRE_LE(id, MAX);
		BOOST_CHECK_MESSAGE(freed.count(id), "recycled ID " << id << " not in the freed set");
		live.push_back(n);
	}

	for (ZCom_Node *n : live)
		delete n;
	live.clear();
	g_currentControl = nullptr;
	ctrl.Shutdown();
}

BOOST_AUTO_TEST_SUITE_END()
