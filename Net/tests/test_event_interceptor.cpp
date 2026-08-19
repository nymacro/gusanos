// test_event_interceptor.cpp
// Item 22: ZCom_NodeEventInterceptor is invoked during event dispatch (pushEvent)
// and can veto (drop) an event by returning false. Previously setEventInterceptor()
// stored the pointer but rec* were never called — the API was dead. These tests
// exercise the wiring directly via pushEvent (the single ingress for incoming
// node events), matching the Zoidcom reference contract.

#include <boost/test/unit_test.hpp>
#include "net_node.h"

namespace {

// Recording interceptor: captures which callback fired and with what args, and
// returns a configurable verdict per call (default true = let through).
class RecordingInterceptor : public ZCom_NodeEventInterceptor {
  public:
	int userCalls = 0;
	int initCalls = 0;
	int syncCalls = 0;
	int removedCalls = 0;
	int fileIncomingCalls = 0;
	int fileDataCalls = 0;
	int fileAbortedCalls = 0;
	int fileCompleteCalls = 0;

	uint32_t lastFrom = 0xFFFFFFFF;
	eZCom_NodeRole lastRole = eZCom_RoleUndefined;
	ZCom_FileTransID lastFid = 0xFFFFFFFF;
	zU32 lastEst = 0xFFFFFFFF;
	std::string lastOffer; // offer bytes decoded by recFileIncoming (as a string)

	bool letThrough = true; // false => veto the next event

	bool recUserEvent(ZCom_Node *, uint32_t from, eZCom_NodeRole role, ZCom_BitStream &data, uint32_t est) override {
		userCalls++;
		lastFrom = from;
		lastRole = role;
		lastEst = est;
		// Read the whole payload to prove the probe is inspectable and that
		// doing so does NOT corrupt the node's queued copy.
		lastOffer = data.getString();
		return letThrough;
	}
	bool recInit(ZCom_Node *, uint32_t from, eZCom_NodeRole role) override {
		initCalls++;
		lastFrom = from;
		lastRole = role;
		return letThrough;
	}
	bool recSyncRequest(ZCom_Node *, uint32_t from, eZCom_NodeRole role) override {
		syncCalls++;
		lastFrom = from;
		lastRole = role;
		return letThrough;
	}
	bool recRemoved(ZCom_Node *, uint32_t from, eZCom_NodeRole role) override {
		removedCalls++;
		lastFrom = from;
		lastRole = role;
		return letThrough;
	}
	bool recFileIncoming(ZCom_Node *, uint32_t from, eZCom_NodeRole role, ZCom_FileTransID fid,
						 ZCom_BitStream &request) override {
		fileIncomingCalls++;
		lastFrom = from;
		lastRole = role;
		lastFid = fid;
		// request holds the sender's raw offer bytes (no length prefix — matches
		// the reference _data stream).
		size_t n = request.getDataLength();
		const uint8_t *p = request.getData();
		lastOffer.assign(reinterpret_cast<const char *>(p), n);
		return letThrough;
	}
	bool recFileData(ZCom_Node *, uint32_t from, eZCom_NodeRole role, ZCom_FileTransID fid) override {
		fileDataCalls++;
		lastFrom = from;
		lastRole = role;
		lastFid = fid;
		return letThrough;
	}
	bool recFileAborted(ZCom_Node *, uint32_t from, eZCom_NodeRole role, ZCom_FileTransID fid) override {
		fileAbortedCalls++;
		lastFrom = from;
		lastRole = role;
		lastFid = fid;
		return letThrough;
	}
	bool recFileComplete(ZCom_Node *, uint32_t from, eZCom_NodeRole role, ZCom_FileTransID fid) override {
		fileCompleteCalls++;
		lastFrom = from;
		lastRole = role;
		lastFid = fid;
		return letThrough;
	}
};

} // namespace

BOOST_AUTO_TEST_SUITE(event_interceptor)

// Without an interceptor, events queue exactly as before (regression guard).
BOOST_AUTO_TEST_CASE(no_interceptor_events_pass_through) {
	ZCom_Node node;
	ZCom_BitStream payload;
	payload.addString("hello");
	node.pushEvent(eZCom_EventUser, eZCom_RoleAuthority, 7, &payload, 12345);

	BOOST_CHECK(node.checkEventWaiting());
	eZCom_Event type = eZCom_EventNoEvent;
	eZCom_NodeRole role = eZCom_RoleUndefined;
	ZCom_ConnID conn = 0;
	zU32 est = 0;
	auto data = node.getNextEvent(&type, &role, &conn, &est);
	BOOST_CHECK_EQUAL(type, eZCom_EventUser);
	BOOST_CHECK_EQUAL(role, eZCom_RoleAuthority);
	BOOST_CHECK_EQUAL(conn, 7u);
	BOOST_CHECK_EQUAL(est, 12345u);
	BOOST_REQUIRE(data);
	BOOST_CHECK_EQUAL(data->getString(), "hello");
}

// recUserEvent fires on a user event; returning true forwards it, and the
// interceptor reading the probe must not corrupt the queued copy.
BOOST_AUTO_TEST_CASE(user_event_invoked_and_payload_preserved) {
	ZCom_Node node;
	RecordingInterceptor ic;
	node.setEventInterceptor(&ic);
	ic.letThrough = true;

	ZCom_BitStream payload;
	payload.addString("payload");
	node.pushEvent(eZCom_EventUser, eZCom_RoleProxy, 42, &payload, 999);

	BOOST_CHECK_EQUAL(ic.userCalls, 1);
	BOOST_CHECK_EQUAL(ic.lastFrom, 42u);
	BOOST_CHECK_EQUAL(ic.lastRole, eZCom_RoleProxy);
	BOOST_CHECK_EQUAL(ic.lastEst, 999u);
	BOOST_CHECK_EQUAL(ic.lastOffer, "payload");

	// The node still receives a pristine copy of the payload.
	BOOST_CHECK(node.checkEventWaiting());
	eZCom_Event type = eZCom_EventNoEvent;
	ZCom_ConnID conn = 0;
	auto data = node.getNextEvent(&type, nullptr, &conn);
	BOOST_CHECK_EQUAL(type, eZCom_EventUser);
	BOOST_REQUIRE(data);
	BOOST_CHECK_EQUAL(data->getString(), "payload");
}

// recUserEvent returning false drops the event entirely.
BOOST_AUTO_TEST_CASE(user_event_dropped_on_false) {
	ZCom_Node node;
	RecordingInterceptor ic;
	node.setEventInterceptor(&ic);
	ic.letThrough = false;

	ZCom_BitStream payload;
	payload.addString("dropped");
	node.pushEvent(eZCom_EventUser, eZCom_RoleAuthority, 1, &payload, 0);

	BOOST_CHECK_EQUAL(ic.userCalls, 1);
	BOOST_CHECK(!node.checkEventWaiting());
}

// recInit / recRemoved fire for their event types with the same drop semantics.
BOOST_AUTO_TEST_CASE(init_and_removed_events) {
	ZCom_Node node;
	RecordingInterceptor ic;
	node.setEventInterceptor(&ic);

	node.pushEvent(eZCom_EventInit, eZCom_RoleOwner, 5, nullptr, 0);
	BOOST_CHECK_EQUAL(ic.initCalls, 1);
	BOOST_CHECK_EQUAL(ic.lastFrom, 5u);
	BOOST_CHECK_EQUAL(ic.lastRole, eZCom_RoleOwner);
	BOOST_CHECK(node.checkEventWaiting());
	node.getNextEvent(nullptr, nullptr, nullptr);

	node.pushEvent(eZCom_EventRemoved, eZCom_RoleProxy, 6, nullptr, 0);
	BOOST_CHECK_EQUAL(ic.removedCalls, 1);
	BOOST_CHECK_EQUAL(ic.lastFrom, 6u);
	BOOST_CHECK(node.checkEventWaiting());
	node.getNextEvent(nullptr, nullptr, nullptr);

	// Veto path: init dropped.
	ic.letThrough = false;
	node.pushEvent(eZCom_EventInit, eZCom_RoleOwner, 8, nullptr, 0);
	BOOST_CHECK_EQUAL(ic.initCalls, 2);
	BOOST_CHECK(!node.checkEventWaiting());
}

// File events: the interceptor receives the parsed fid, and File_Incoming gets
// the sender's offer bytes as a fresh stream. All four file events can be vetoed.
BOOST_AUTO_TEST_CASE(file_events_carry_fid_and_offer) {
	ZCom_Node node;
	RecordingInterceptor ic;
	node.setEventInterceptor(&ic);

	const ZCom_FileTransID fid = 0x1234;
	const std::string offer = "offer-bytes";

	auto buildFileEvent = [&](eZCom_Event type, bool withOffer) {
		ZCom_BitStream ev;
		ev.addInt(fid, ZCOM_FTRANS_ID_BITS);
		if (withOffer)
			ev.addBuffer(offer.data(), static_cast<zU16>(offer.size()));
		node.pushEvent(type, eZCom_RoleAuthority, 11, &ev, 0);
	};

	buildFileEvent(eZCom_EventFile_Incoming, true);
	BOOST_CHECK_EQUAL(ic.fileIncomingCalls, 1);
	BOOST_CHECK_EQUAL(ic.lastFid, fid);
	BOOST_CHECK_EQUAL(ic.lastOffer, offer);
	BOOST_CHECK(node.checkEventWaiting());
	node.getNextEvent(nullptr, nullptr, nullptr);

	buildFileEvent(eZCom_EventFile_Data, false);
	BOOST_CHECK_EQUAL(ic.fileDataCalls, 1);
	BOOST_CHECK_EQUAL(ic.lastFid, fid);

	buildFileEvent(eZCom_EventFile_Aborted, false);
	BOOST_CHECK_EQUAL(ic.fileAbortedCalls, 1);
	BOOST_CHECK_EQUAL(ic.lastFid, fid);

	buildFileEvent(eZCom_EventFile_Complete, false);
	BOOST_CHECK_EQUAL(ic.fileCompleteCalls, 1);
	BOOST_CHECK_EQUAL(ic.lastFid, fid);

	// Drain the three non-incoming file events queued above.
	BOOST_CHECK(node.checkEventWaiting());
	node.getNextEvent(nullptr, nullptr, nullptr);
	BOOST_CHECK(node.checkEventWaiting());
	node.getNextEvent(nullptr, nullptr, nullptr);
	BOOST_CHECK(node.checkEventWaiting());
	node.getNextEvent(nullptr, nullptr, nullptr);
	BOOST_CHECK(!node.checkEventWaiting());

	// Veto a file data event.
	ic.letThrough = false;
	buildFileEvent(eZCom_EventFile_Data, false);
	BOOST_CHECK_EQUAL(ic.fileDataCalls, 2);
	BOOST_CHECK(!node.checkEventWaiting());
}

// The interceptor can be unregistered; subsequent events bypass it.
BOOST_AUTO_TEST_CASE(unregister_interceptor) {
	ZCom_Node node;
	RecordingInterceptor ic;
	node.setEventInterceptor(&ic);
	node.setEventInterceptor(nullptr);

	ZCom_BitStream payload;
	payload.addString("x");
	node.pushEvent(eZCom_EventUser, eZCom_RoleAuthority, 1, &payload, 0);

	BOOST_CHECK_EQUAL(ic.userCalls, 0);
	BOOST_CHECK(node.checkEventWaiting());
}

BOOST_AUTO_TEST_SUITE_END()
