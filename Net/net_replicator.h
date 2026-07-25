#ifndef NET_REPLICATOR_H
#define NET_REPLICATOR_H

#include "net_types.h"
#include "net_bitstream.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <memory>

// Forward declarations
class ZCom_BitStream;
class ZCom_Node;

// ---- ZCom_TypeHelper ----
template <typename T>
struct ZCom_TypeHelper {
	typedef T value_type;
	typedef T *pointer;
};
template <typename T>
struct ZCom_TypeHelper<T *> {
	typedef T value_type;
	typedef T *pointer;
};

// ---- ZCom_ReplicatorValue (value type) ----
template <typename T, int SIZE>
class ZCom_ReplicatorValue {
	T m_data[SIZE];
	bool m_changed;

  public:
	ZCom_ReplicatorValue() : m_changed(true) {
		for (int i = 0; i < SIZE; ++i)
			m_data[i] = T();
	}
	bool hasChanged() {
		bool c = m_changed;
		m_changed = false;
		return c;
	}
	bool getChanged() const {
		return m_changed;
	}
	void setChanged() {
		m_changed = true;
	}
	T *getData() {
		return m_data;
	}
	void setData(T *_data) {
		for (int i = 0; i < SIZE; ++i)
			m_data[i] = _data[i];
		m_changed = true;
	}
	void setData(T _data, uint32_t _idx) {
		if (_idx < static_cast<uint32_t>(SIZE)) {
			m_data[_idx] = _data;
			m_changed = true;
		}
	}
	void updateData(T *_data) {
		for (int i = 0; i < SIZE; ++i)
			m_data[i] = _data[i];
	}
};

// ---- ZCom_ReplicatorValue (pointer type) ----
template <typename T, int SIZE>
class ZCom_ReplicatorValue<T *, SIZE> {
	T *m_data;
	T m_cmp[SIZE];

  public:
	ZCom_ReplicatorValue() : m_data(nullptr) {
		for (int i = 0; i < SIZE; ++i)
			m_cmp[i] = T();
	}
	ZCom_ReplicatorValue(T *_data) : m_data(_data) {}
	bool hasChanged() {
		if (!m_data)
			return false;
		for (int i = 0; i < SIZE; ++i) {
			if (m_data[i] != m_cmp[i]) {
				for (int j = 0; j < SIZE; ++j)
					m_cmp[j] = m_data[j];
				return true;
			}
		}
		return false;
	}
	bool getChanged() const {
		return true;
	}
	void setChanged() {
		if (m_data)
			for (int i = 0; i < SIZE; ++i)
				m_cmp[i] = m_data[i];
	}
	T *getData() {
		return m_data;
	}
	void setData(T *_data) {
		m_data = _data;
	}
	void setData(T _data, uint32_t _idx) {
		if (m_data && _idx < static_cast<uint32_t>(SIZE)) {
			m_data[_idx] = _data;
		}
	}
	void updateData(T *_data) {
		if (m_data) {
			for (int i = 0; i < SIZE; ++i)
				m_data[i] = _data[i];
		}
	}
};

// ---- Replicator Setup ----
class ZCom_ReplicatorSetup {
  public:
	ZCom_ReplicatorSetup() : m_interceptID(-1), m_repFlags(0), m_repRules(0), m_minDelay(0), m_maxDelay(0) {}
	ZCom_ReplicatorSetup(uint32_t repFlags, uint32_t repRules, int interceptID = -1, int minDelay = 0, int maxDelay = 0)
		: m_interceptID(interceptID), m_repFlags(repFlags), m_repRules(repRules), m_minDelay(minDelay),
		  m_maxDelay(maxDelay) {}

	virtual ~ZCom_ReplicatorSetup() {}

	virtual std::unique_ptr<ZCom_ReplicatorSetup> Duplicate() {
		// SETUPPERSISTS is a documented no-op for Duplicate: m_setup is a by-value
		// member of ZCom_Replicator, never separately heap-allocated, so always
		// heap-copy here (callers store the result in a unique_ptr).
		return std::make_unique<ZCom_ReplicatorSetup>(m_repFlags, m_repRules, m_interceptID, m_minDelay, m_maxDelay);
	}

	int getInterceptID() {
		return m_interceptID;
	}
	void setInterceptID(int id) {
		m_interceptID = id;
	}

	int getMinDelay() {
		return m_minDelay;
	}
	void setMinDelay(int d) {
		m_minDelay = d;
	}

	int getMaxDelay() {
		return m_maxDelay;
	}
	void setMaxDelay(int d) {
		m_maxDelay = d;
	}

	uint32_t getFlags() {
		return m_repFlags;
	}
	uint32_t getRules() {
		return m_repRules;
	}

	int m_interceptID;
	uint32_t m_repFlags;
	uint32_t m_repRules;
	int m_minDelay;
	int m_maxDelay;
};

// ---- Numeric setup ----
class ZCom_RSetupNumeric : public ZCom_ReplicatorSetup {
  public:
	ZCom_RSetupNumeric() : m_relevantBits(8) {}
	ZCom_RSetupNumeric(int relBits, uint32_t repFlags, uint32_t repRules, int interceptID = -1, int minDelay = 0,
					   int maxDelay = 0)
		: ZCom_ReplicatorSetup(repFlags, repRules, interceptID, minDelay, maxDelay), m_relevantBits(relBits) {}

	int getRelevantBits() {
		return m_relevantBits;
	}
	void setRelevantBits(int b) {
		m_relevantBits = b;
	}

	std::unique_ptr<ZCom_ReplicatorSetup> Duplicate() override {
		return std::make_unique<ZCom_RSetupNumeric>(m_relevantBits, m_repFlags, m_repRules, m_interceptID, m_minDelay,
													m_maxDelay);
	}

	int m_relevantBits;
};

// ---- String setup ----
class ZCom_RSetupString : public ZCom_ReplicatorSetup {
  public:
	ZCom_RSetupString() : maxlen(256) {}
	ZCom_RSetupString(int maxLen, uint32_t repFlags, uint32_t repRules)
		: ZCom_ReplicatorSetup(repFlags, repRules), maxlen(maxLen) {}

	int maxlen;

	std::unique_ptr<ZCom_ReplicatorSetup> Duplicate() override {
		return std::make_unique<ZCom_RSetupString>(maxlen, m_repFlags, m_repRules);
	}
};

// ---- Movement setup (template) ----
template <typename T>
class ZCom_RSetupMovement : public ZCom_ReplicatorSetup {
  public:
	ZCom_RSetupMovement() : m_inputsizeBits(8), m_interpolationTime(100), m_constantErrorThreshold(0) {}
	ZCom_RSetupMovement(int relBits, uint32_t repFlags, uint32_t repRules)
		: ZCom_ReplicatorSetup(repFlags, repRules), m_relevantBits(relBits), m_inputsizeBits(8),
		  m_interpolationTime(100), m_constantErrorThreshold(0) {}

	std::unique_ptr<ZCom_ReplicatorSetup> Duplicate() override {
		return std::make_unique<ZCom_RSetupMovement>(m_relevantBits, m_repFlags, m_repRules);
	}

	int getRelevantBits() {
		return m_relevantBits;
	}
	int getInputsizeBits() {
		return m_inputsizeBits;
	}
	void setInputsizeBits(int b) {
		m_inputsizeBits = b;
	}
	int getInterpolationTime() {
		return m_interpolationTime;
	}
	void setInterpolationTime(int t) {
		m_interpolationTime = t;
	}
	float getConstantErrorThreshold() {
		return m_constantErrorThreshold;
	}
	void setConstantErrorThreshold(float t) {
		m_constantErrorThreshold = t;
	}
	uint32_t getExtendedFlags() {
		return 0;
	}

	int m_relevantBits;
	int m_inputsizeBits;
	int m_interpolationTime;
	float m_constantErrorThreshold;
};

// ---- Interpolate setup (template) ----
template <typename T>
class ZCom_RSetupInterpolate : public ZCom_ReplicatorSetup {
  public:
	ZCom_RSetupInterpolate() : m_relevantBits(16), ipol_treshold(0), ipol_factor(0.5f) {}
	ZCom_RSetupInterpolate(int relBits, uint32_t repFlags, uint32_t repRules, int threshold = 0, int unused = 0,
						   int unused2 = -1, int unused3 = -1, float factor = 0.5f)
		: ZCom_ReplicatorSetup(repFlags, repRules), m_relevantBits(relBits), ipol_treshold(threshold),
		  ipol_factor(factor) {}

	std::unique_ptr<ZCom_ReplicatorSetup> Duplicate() override {
		return std::make_unique<ZCom_RSetupInterpolate>(m_relevantBits, m_repFlags, m_repRules, ipol_treshold, 0, -1,
														-1, ipol_factor);
	}

	int getRelevantBits() {
		return m_relevantBits;
	}

	int m_relevantBits;
	int ipol_treshold;
	float ipol_factor;
};

// ---- Base Replicator ----
class ZCom_Replicator {
  public:
	virtual ~ZCom_Replicator() {}

	ZCom_ReplicatorSetup *getSetup() {
		return &m_setup;
	}
	zU8 getFlags() const {
		return static_cast<zU8>(m_flags);
	}
	zU16 getId() const {
		return m_id;
	}
	bool callProcess() const {
		return (m_flags & ZCOM_REPLICATOR_CALLPROCESS) ? true : false;
	}

	uint32_t getLastSendTime() const {
		return m_lastSendTime;
	}
	void setLastSendTime(uint32_t t) {
		m_lastSendTime = t;
	}

	virtual bool checkState() {
		return false;
	}
	virtual bool checkInitialState() {
		return true;
	}
	virtual void packData(ZCom_BitStream *stream) {}
	virtual void unpackData(ZCom_BitStream *stream, bool store, uint32_t estimatedTimeSent) {}
	virtual void Process(eZCom_NodeRole _localrole, zU32 _simulation_time_passed) {}
	virtual void *peekData() {
		return nullptr;
	}
	virtual ZCom_BitStream *getPeekStream() {
		return m_peekStream;
	}
	virtual void setPeekStream(ZCom_BitStream *s) {
		m_peekStream = s;
	}
	virtual void peekDataStore(void *data) {
		m_peekData = data;
	}
	virtual void *peekDataRetrieve() {
		return m_peekData;
	}
	virtual void clearPeekData() {}

	ZCom_ReplicatorSetup m_setup;
	// Non-owning peek observers — set briefly during unpack and cleared after use.
	ZCom_BitStream *m_peekStream = nullptr;
	void *m_peekData = nullptr;
	uint32_t m_flags = 0;
	zU16 m_id = 0;
	uint32_t m_lastSendTime = 0; // last pack/send time (ZoidCom::getTime()) for T1.3 min/max-delay throttling
};

// ---- Basic Replicator (default implementation holder) ----
class ZCom_ReplicatorBasic : public ZCom_Replicator {
  public:
	ZCom_ReplicatorBasic() : m_flags(0) {}
	ZCom_ReplicatorBasic(ZCom_ReplicatorSetup *setup) {
		if (setup)
			m_setup = *setup;
		m_flags = 0;
	}
	uint32_t m_flags;

	// For auto-replications, peekData() returns the decoded value stashed via
	// peekDataStore() (a pointer the caller can dereference). Custom replicators
	// (PosSpd/Vector/Angle) override peekData() to read from getPeekStream().
	void *peekData() override {
		return m_peekData;
	}

	bool checkState() override {
		return true;
	}
};

// ---- Bool replicator (value type) ----
class ZCom_Replicate_Bool : public ZCom_Replicator {
  public:
	ZCom_Replicate_Bool(bool initial, uint32_t flags, uint32_t rules) : m_value(initial), m_oldValue(initial) {
		m_setup = ZCom_ReplicatorSetup(flags, rules);
	}

	bool getValue() {
		return m_value;
	}
	void setValue(bool val) {
		m_value = val;
	}

	bool checkState() override {
		return m_value != m_oldValue;
	}

	void packData(ZCom_BitStream *stream) override;
	void unpackData(ZCom_BitStream *stream, bool store, uint32_t estimatedTimeSent) override;

  private:
	bool m_value;
	bool m_oldValue;
};

// ---- Bool replicator (pointer type) ----
class ZCom_Replicate_Boolp : public ZCom_Replicator {
  public:
	ZCom_Replicate_Boolp(bool *ptr, uint32_t flags, uint32_t rules) : m_ptr(ptr), m_oldValue(ptr ? *ptr : false) {
		m_setup = ZCom_ReplicatorSetup(flags, rules);
	}

	bool checkState() override;
	void packData(ZCom_BitStream *stream) override;
	void unpackData(ZCom_BitStream *stream, bool store, uint32_t estimatedTimeSent) override;

  private:
	bool *m_ptr;
	bool m_oldValue;
};

// ---- Numeric replicator (value type) ----
template <typename T, int N = 1>
class ZCom_Replicate_Numeric : public ZCom_Replicator {
  public:
	ZCom_Replicate_Numeric(T initial, int bits, uint32_t flags, uint32_t rules)
		: m_bits(bits), m_value(initial), m_oldValue(initial) {
		m_setup = ZCom_ReplicatorSetup(flags, rules);
	}

	T getValue() const {
		return m_value;
	}
	void setValue(T val) {
		m_value = val;
	}

	bool checkState() override {
		return m_value != m_oldValue;
	}

	void packData(ZCom_BitStream *stream) override {
		if (std::is_signed<T>::value)
			stream->addSignedInt(static_cast<int>(m_value), m_bits);
		else
			stream->addInt(static_cast<int>(m_value), m_bits);
		m_oldValue = m_value;
	}

	void unpackData(ZCom_BitStream *stream, bool store, uint32_t estimatedTimeSent) override {
		(void)estimatedTimeSent;
		T val;
		if (std::is_signed<T>::value)
			val = static_cast<T>(stream->getSignedInt(m_bits));
		else
			val = static_cast<T>(stream->getInt(m_bits));
		if (store) {
			m_value = val;
			m_oldValue = val;
		}
	}

	int getRelevantBits() const {
		return m_bits;
	}

  private:
	int m_bits;
	T m_value;
	T m_oldValue;
};

// ---- Numeric replicator (pointer type) ----
template <typename T>
class ZCom_Replicate_Numericp : public ZCom_Replicator {
  public:
	ZCom_Replicate_Numericp(T *ptr, int bits, uint32_t flags, uint32_t rules)
		: m_ptr(ptr), m_bits(bits), m_oldValue(ptr ? *ptr : T()) {
		m_setup = ZCom_ReplicatorSetup(flags, rules);
	}

	bool checkState() override {
		if (!m_ptr)
			return false;
		return *m_ptr != m_oldValue;
	}

	void packData(ZCom_BitStream *stream) override {
		if (!m_ptr)
			return;
		if (std::is_signed<T>::value)
			stream->addSignedInt(static_cast<int>(*m_ptr), m_bits);
		else
			stream->addInt(static_cast<int>(*m_ptr), m_bits);
		m_oldValue = *m_ptr;
	}

	void unpackData(ZCom_BitStream *stream, bool store, uint32_t estimatedTimeSent) override {
		(void)estimatedTimeSent;
		T val;
		if (std::is_signed<T>::value)
			val = static_cast<T>(stream->getSignedInt(m_bits));
		else
			val = static_cast<T>(stream->getInt(m_bits));
		if (store && m_ptr) {
			*m_ptr = val;
			m_oldValue = val;
		}
	}

  private:
	T *m_ptr;
	int m_bits;
	T m_oldValue;
};

// ---- String replicator (pointer type) ----
class ZCom_Replicate_Stringp : public ZCom_Replicator {
  public:
	ZCom_Replicate_Stringp(const char **ptr, int maxSize, uint32_t flags, uint32_t rules)
		: m_ptr(ptr), m_maxSize(maxSize), m_oldValue("") {
		m_setup = ZCom_ReplicatorSetup(flags, rules);
	}

	bool checkState() override;
	void packData(ZCom_BitStream *stream) override;
	void unpackData(ZCom_BitStream *stream, bool store, uint32_t estimatedTimeSent) override;

  private:
	const char **m_ptr;
	int m_maxSize;
	std::string m_oldValue;
	std::string m_lastRead;
};

// ---- Wide string replicator (pointer type) ----
class ZCom_Replicate_StringWp : public ZCom_Replicator {
  public:
	ZCom_Replicate_StringWp(const wchar_t **ptr, int maxSize, uint32_t flags, uint32_t rules)
		: m_ptr(ptr), m_maxSize(maxSize) {
		m_setup = ZCom_ReplicatorSetup(flags, rules);
	}

	bool checkState() override;
	void packData(ZCom_BitStream *stream) override;
	void unpackData(ZCom_BitStream *stream, bool store, uint32_t estimatedTimeSent) override;

  private:
	const wchar_t **m_ptr;
	int m_maxSize;
	std::wstring m_oldValue;
	std::wstring m_lastWRead;
};

// ---- Memory block replicator ----
class ZCom_Replicate_Memblock : public ZCom_Replicator {
  public:
	ZCom_Replicate_Memblock(void *ptr, uint32_t blockSize, uint32_t flags, uint32_t rules)
		: m_ptr(ptr), m_blockSize(blockSize), m_oldData(blockSize) {
		m_setup = ZCom_ReplicatorSetup(flags, rules);
		if (blockSize)
			memcpy(m_oldData.data(), ptr, blockSize);
	}

	bool checkState() override;
	void packData(ZCom_BitStream *stream) override;
	void unpackData(ZCom_BitStream *stream, bool store, uint32_t estimatedTimeSent) override;

  private:
	void *m_ptr;
	uint32_t m_blockSize;
	std::vector<char> m_oldData;
};

// ---- Advanced replicator base ----
class ZCom_ReplicatorAdvanced : public ZCom_Replicator {
  public:
	virtual ~ZCom_ReplicatorAdvanced() {}
};

// ---- Interpolating replicator (template) ----
template <typename T, int N>
class ZCom_Interpolate : public ZCom_Replicator {
  public:
	ZCom_Interpolate(T *val, int bits, uint32_t flags, uint32_t rules, int threshold = 0)
		: m_val(val), m_size(N), m_threshold(threshold) {
		m_setup = ZCom_ReplicatorSetup(flags, rules);
	}

	int getSize() {
		return m_size;
	}

	void setRecVal(int idx, T val) {
		if (idx >= 0 && idx < N)
			m_recv[idx] = val;
	}
	T getRecVal(int idx) {
		if (idx >= 0 && idx < N)
			return m_recv[idx];
		return T();
	}

	void Process(eZCom_NodeRole _localrole, zU32 _simulation_time_passed) override {
		(void)_localrole;
		(void)_simulation_time_passed;
		// Interpolation processing would happen here
	}

  private:
	T *m_val;
	T m_recv[N];
	int m_size;
	int m_threshold;
};

// ---- Movement replicator (template) ----
template <typename T, int N>
class ZCom_MoveUpdateListener;

template <typename T, int N>
class ZCom_Replicate_Movement : public ZCom_Replicator {
  public:
	ZCom_Replicate_Movement(ZCom_RSetupMovement<T> *setup) : m_timeScale(0.05f), m_listener(nullptr) {
		if (setup)
			m_setup = *setup;
	}

	ZCom_Replicate_Movement(int bits, uint32_t flags, uint32_t rules) : m_timeScale(0.05f), m_listener(nullptr) {
		m_setup = ZCom_ReplicatorSetup(flags, rules);
	}

	void setTimeScale(float scale) {
		m_timeScale = scale;
	}
	void setUpdateListener(ZCom_MoveUpdateListener<T, N> *listener) {
		m_listener = listener;
	}

  private:
	float m_timeScale;
	void *m_listener;
};

// ---- Movement update listener base (template) ----
template <typename T, int N>
class ZCom_MoveUpdateListener {
  public:
	virtual ~ZCom_MoveUpdateListener() {}
	virtual void inputUpdated(ZCom_BitStream &_inputstream, bool _inputchanged, uint32_t _client_time,
							  uint32_t _estimated_time_sent) {}
	virtual void inputSent(ZCom_BitStream &_inputstream) {}
	virtual void correctionReceived(T *_pos, T *_vel, T *_acc, bool _teleport, uint32_t _estimated_time_sent) {}
	virtual void updateReceived(ZCom_BitStream &_inputstream, T *_pos, T *_vel, T *_acc,
								uint32_t _estimated_time_sent) {}
};

// ---- Node Replication Interceptor (callback base) ----
class ZCom_NodeReplicationInterceptor {
  public:
	virtual ~ZCom_NodeReplicationInterceptor() {}
	virtual void ZCom_cbNodeReplicationIntercept(ZCom_BitStream *stream, ZCom_Node *node, int mode, int event,
												 eZCom_NodeRole role, uint32_t connID) {}

	// ZoidCom ref_tests API
	virtual void outPreReplicateNode(ZCom_Node *node, uint32_t to, eZCom_NodeRole remote_role) {}
	virtual void outPreDereplicateNode(ZCom_Node *node, uint32_t to, eZCom_NodeRole remote_role) {}
	virtual bool outPreUpdate(ZCom_Node *node, uint32_t to, eZCom_NodeRole remote_role) {
		return true;
	}
	virtual bool outPreUpdateItem(ZCom_Node *node, uint32_t to, eZCom_NodeRole remote_role,
								  ZCom_Replicator *replicator) {
		return true;
	}
	virtual void outPostUpdate(ZCom_Node *node, uint32_t to, eZCom_NodeRole remote_role, uint32_t rep_bits,
							   uint32_t event_bits, uint32_t meta_bits) {}
	virtual bool inPreUpdate(ZCom_Node *node, uint32_t from, eZCom_NodeRole remote_role) {
		return true;
	}
	virtual bool inPreUpdateItem(ZCom_Node *node, uint32_t from, eZCom_NodeRole remote_role,
								 ZCom_Replicator *replicator, uint32_t estimated_time_sent) {
		return true;
	}
	virtual void inPostUpdate(ZCom_Node *node, uint32_t from, eZCom_NodeRole remote_role, uint32_t rep_bits,
							  uint32_t event_bits, uint32_t meta_bits) {}
};

// ---- Node Event Interceptor ----
class ZCom_NodeEventInterceptor {
  public:
	virtual ~ZCom_NodeEventInterceptor() {}
	virtual bool recUserEvent(ZCom_Node *node, uint32_t from, eZCom_NodeRole remoterole, ZCom_BitStream &data,
							  uint32_t estimated_time_sent) {
		return true;
	}
	virtual bool recInit(ZCom_Node *node, uint32_t from, eZCom_NodeRole remoterole) {
		return true;
	}
	virtual bool recSyncRequest(ZCom_Node *node, uint32_t from, eZCom_NodeRole remoterole) {
		return true;
	}
	virtual bool recRemoved(ZCom_Node *node, uint32_t from, eZCom_NodeRole remoterole) {
		return true;
	}
	virtual bool recFileIncoming(ZCom_Node *node, uint32_t from, eZCom_NodeRole remoterole, uint32_t fid,
								 ZCom_BitStream &request) {
		return true;
	}
	virtual bool recFileData(ZCom_Node *node, uint32_t from, eZCom_NodeRole remoterole, uint32_t fid) {
		return true;
	}
	virtual bool recFileAborted(ZCom_Node *node, uint32_t from, eZCom_NodeRole remoterole, uint32_t fid) {
		return true;
	}
	virtual bool recFileComplete(ZCom_Node *node, uint32_t from, eZCom_NodeRole remoterole, uint32_t fid) {
		return true;
	}
};

// ---- File transfer info struct ----
// Reference fields (zoidcom.h:285): if id == ZCom_Invalid_ID the whole struct
// is invalid. The game (updater.cpp) reads .path/.bps/.transferred/.size.
// Legacy m_* fields retained for compatibility.
struct ZCom_FileTransInfo {
	ZCom_FileTransID id;
	zU32 size;
	zU32 transferred;
	const char *path;
	zU32 bps;
	uint32_t m_id;
	const char *m_filename;
	float m_progress;
	uint32_t m_bytes_downloaded;
	uint32_t m_file_size;
};

#endif // NET_REPLICATOR_H
