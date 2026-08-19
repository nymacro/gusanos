// test_auto_replicator_throttle.cpp
// Item 23: packReplicatorsForRouting now applies per-replicator min/max-delay
// throttling to auto-replications, matching the explicit-replicator path and
// the Zoidcom reference (which throttles every replicator regardless of type).
// Previously auto-replications ignored min/maxDelay: detect() was called
// unconditionally with force=forceAll, so a change always packed immediately
// and a stale-but-unchanged entry never heartbeated.
//
// These tests exercise the throttle purely through the observable
// packReplicatorsForRouting output. The "skipped change stays dirty" property
// is proven indirectly: a skipped tick does not consume the snapshot, so a
// later pack (after the min-delay window) emits the *changed* value. If the
// old code had consumed the change on the skipped tick, the later pack would
// see no change and would not emit.

#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <chrono>
#include <thread>

#include "net_node.h"
#include "net_replicator.h"

BOOST_AUTO_TEST_SUITE(auto_replicator_throttle)

// minDelay: a change made within the window is skipped but kept dirty; once the
// window elapses the queued change is packed.
BOOST_AUTO_TEST_CASE(min_delay_skips_then_sends_kept_change) {
	ZCom_Node node;
	zS32 v = 1;
	node.addReplicationInt(&v, 8, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL,
						   /*mindelay=*/50, /*maxdelay=*/-1);

	// First pack: initial => always sends the seed value.
	std::vector<PackedReplicator> out;
	node.packReplicatorsForRouting(out);
	BOOST_REQUIRE_EQUAL(out.size(), 1u);
	BOOST_CHECK_EQUAL(out[0].hasUpdate, true);
	BOOST_CHECK_EQUAL(out[0].data.getInt(8), 1u);

	// Change the value and re-pack immediately: within minDelay => skipped,
	// but the change must remain dirty (detect NOT called).
	v = 2;
	out.clear();
	node.packReplicatorsForRouting(out);
	BOOST_REQUIRE_EQUAL(out.size(), 1u);
	BOOST_CHECK_EQUAL(out[0].hasUpdate, false);

	// Wait out the min-delay window. The kept change now packs.
	std::this_thread::sleep_for(std::chrono::milliseconds(60));
	out.clear();
	node.packReplicatorsForRouting(out);
	BOOST_REQUIRE_EQUAL(out.size(), 1u);
	BOOST_CHECK_EQUAL(out[0].hasUpdate, true);
	BOOST_CHECK_EQUAL(out[0].data.getInt(8), 2u); // the changed value, not the seed

	// One more immediate pack: value unchanged since the last send => no update.
	out.clear();
	node.packReplicatorsForRouting(out);
	BOOST_REQUIRE_EQUAL(out.size(), 1u);
	BOOST_CHECK_EQUAL(out[0].hasUpdate, false);
}

// maxDelay: an unchanged entry older than the window is force-sent as a
// heartbeat even though nothing changed.
BOOST_AUTO_TEST_CASE(max_delay_heartbeats_unchanged_stale_entry) {
	ZCom_Node node;
	zS32 v = 7;
	node.addReplicationInt(&v, 8, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL,
						   /*mindelay=*/-1, /*maxdelay=*/50);

	// First pack: initial => sends.
	std::vector<PackedReplicator> out;
	node.packReplicatorsForRouting(out);
	BOOST_REQUIRE_EQUAL(out.size(), 1u);
	BOOST_CHECK_EQUAL(out[0].hasUpdate, true);
	BOOST_CHECK_EQUAL(out[0].data.getInt(8), 7u);

	// Immediate re-pack: unchanged and not yet stale => no update.
	out.clear();
	node.packReplicatorsForRouting(out);
	BOOST_REQUIRE_EQUAL(out.size(), 1u);
	BOOST_CHECK_EQUAL(out[0].hasUpdate, false);

	// Wait past maxDelay. Unchanged, but now stale => force-sent heartbeat.
	std::this_thread::sleep_for(std::chrono::milliseconds(60));
	out.clear();
	node.packReplicatorsForRouting(out);
	BOOST_REQUIRE_EQUAL(out.size(), 1u);
	BOOST_CHECK_EQUAL(out[0].hasUpdate, true);
	BOOST_CHECK_EQUAL(out[0].data.getInt(8), 7u);
}

// A freshly connected peer (forceReplicationUpdate) bypasses both delays: the
// current value is packed immediately regardless of min/maxDelay.
BOOST_AUTO_TEST_CASE(force_all_bypasses_delay_on_first_connect) {
	ZCom_Node node;
	zS32 v = 9;
	// Large minDelay and zero maxDelay would otherwise block/heartbeat.
	node.addReplicationInt(&v, 8, false, ZCOM_REPFLAG_MOSTRECENT, ZCOM_REPRULE_AUTH_2_ALL,
						   /*mindelay=*/5000, /*maxdelay=*/-1);

	// Seed the snapshot with a prior send so initial=false and lastSendTime set,
	// then change the value so it's dirty but would be min-delay-blocked.
	std::vector<PackedReplicator> out;
	node.packReplicatorsForRouting(out); // initial send, lastSendTime=now
	v = 11;								 // dirty, but within the 5s min window

	// forceReplicationUpdate (new peer) must pack immediately despite minDelay.
	node.forceReplicationUpdate();
	out.clear();
	node.packReplicatorsForRouting(out);
	BOOST_REQUIRE_EQUAL(out.size(), 1u);
	BOOST_CHECK_EQUAL(out[0].hasUpdate, true);
	BOOST_CHECK_EQUAL(out[0].data.getInt(8), 11u);
}

BOOST_AUTO_TEST_SUITE_END()
