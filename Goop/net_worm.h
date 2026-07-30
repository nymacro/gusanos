#ifndef NET_WORM_H
#define NET_WORM_H

// #include "vec.h"
// #include "base_object.h"
#include "base_worm.h"
// #include "sprite.h"

#include "network_compat.h"

class NetWormInterceptor;

class NetWorm : public BaseWorm {
  public:
	friend class NetWormInterceptor;

	enum NetEvents {
		PosCorrection = 0,
		Respawn,
		Dig,
		Die,
		ChangeWeapon,
		WeaponMessage,
		SetWeapon,
		ClearWeapons,
		SYNC,
		LuaEvent,
		EVENT_COUNT,
	};

	enum ReplicationItems { PlayerID = 0, Position, AIM };

	static ZCom_ClassID classID;
	static constexpr float MAX_ERROR_RADIUS = 10.0f;

	// Minimum distance (px) between renderPos and pos at which a position update
	// is treated as a teleport (initial spawn, server correction, level change)
	// and renderPos snaps to pos instead of interpolating. Worms move only a few
	// pixels per network update, so any gap beyond this is a teleport, not
	// movement. Tune here if needed (expose as a cvar only if runtime tuning is
	// later required).
	static constexpr float RENDER_SNAP_THRESHOLD = 64.0f;

	NetWorm(bool isAuthority);
	~NetWorm();

	void think();
	void correctOwnerPosition();

	void assignOwner(BasePlayer *owner);
	void setOwnerId(ZCom_ConnID _id);
	void sendSyncMessage(ZCom_ConnID id);
	void sendWeaponMessage(int index, ZCom_BitStream *data, zU8 repRules = ZCOM_REPRULE_AUTH_2_ALL);

	eZCom_NodeRole getRole() {
		if (m_node) {
			return (eZCom_NodeRole)m_node->getRole();
		} else
			return eZCom_RoleUndefined;
	}

	virtual void sendLuaEvent(LuaEventDef *event, eZCom_SendMode mode, zU8 rules, ZCom_BitStream *userdata,
							  ZCom_ConnID connID);

	ZCom_NodeID getNodeID();
	void setNodeID(uint32_t id) {
		if (m_node)
			m_node->setNetworkID(id);
	}
	ZCom_Node *getZNode() {
		return m_node;
	}

	// Deterministic burst seeding (see BaseWorm). NetWorm owns the per-worm
	// action counter and its network node id.
	uint32_t fireSeedNodeID() const override {
		return m_node ? static_cast<uint32_t>(m_node->getNetworkID()) : 0;
	}
	uint32_t fireActionSeq() const override { return m_actionSeq; }
	void advanceFireActionSeq() override { ++m_actionSeq; }
	void reconcileFireActionSeq(uint32_t seq) override { m_actionSeq = seq; }

	void respawn();
	void dig();
	void die();
	void changeWeaponTo(unsigned int weapIndex);
	void damage(float amount, BasePlayer *damager, DamageCause const &cause);
	bool isAuthority() const {
		return m_isAuthority;
	}

	// This machine simulates this worm's physics/rope only when it is the
	// true source of truth for the worm:
	//  - On the server (m_isAuthority): local/AI worms (getOwner()==0) are
	//    simulated here; remote-client-owned worms (getOwner()!=0) are
	//    driven by their owner and only relayed to proxies.
	//  - On a client (!m_isAuthority): the owner of its own worm
	//    (role==Owner) simulates; other players' worms (role==Proxy) are
	//    rendered from replicated state.
	bool isLocalAuthority() const override {
		if (!m_node)
			return m_isAuthority;
		if (m_isAuthority)
			return m_node->getOwner() == 0;
		return m_node->getRole() == eZCom_RoleOwner;
	}
	void setWeapon(size_t index, WeaponType *type);
	void clearWeapons();

	// virtual void deleteThis();
	virtual void finalize();

	Vec lastPosUpdate;
	int timeSinceLastUpdate;

  private:
	void addEvent(ZCom_BitStream *data, NetEvents event);

	bool m_isAuthority;
	ZCom_Node *m_node;
	NetWormInterceptor *m_interceptor;
	ZCom_NodeID m_playerID; // The id of the owner player node to replicate to all proxys

	uint32_t m_actionSeq = 0; // per-worm monotonic fire/dig/die burst counter
};

class NetWormInterceptor : public ZCom_NodeReplicationInterceptor {
  public:
	NetWormInterceptor(NetWorm *parent);

	bool inPreUpdateItem(ZCom_Node *_node, ZCom_ConnID _from, eZCom_NodeRole _remote_role, ZCom_Replicator *_replicator,
						 zU32 _estimated_time_sent);

	// Not used virtual stuff
	void outPreReplicateNode(ZCom_Node *_node, ZCom_ConnID _to, eZCom_NodeRole _remote_role) {}
	void outPreDereplicateNode(ZCom_Node *_node, ZCom_ConnID _to, eZCom_NodeRole _remote_role) {}
	bool outPreUpdate(ZCom_Node *_node, ZCom_ConnID _to, eZCom_NodeRole _remote_role) {
		return true;
	}
	bool outPreUpdateItem(ZCom_Node *node, ZCom_ConnID from, eZCom_NodeRole remote_role, ZCom_Replicator *replicator);
	void outPostUpdate(ZCom_Node *_node, ZCom_ConnID _to, eZCom_NodeRole _remote_role, zU32 _rep_bits, zU32 _event_bits,
					   zU32 _meta_bits) {}
	bool inPreUpdate(ZCom_Node *_node, ZCom_ConnID _from, eZCom_NodeRole _remote_role) {
		return true;
	}
	void inPostUpdate(ZCom_Node *_node, ZCom_ConnID _from, eZCom_NodeRole _remote_role, zU32 _rep_bits,
					  zU32 _event_bits, zU32 _meta_bits) {};

	virtual ~NetWormInterceptor() {}

  private:
	NetWorm *m_parent;
};

#endif // _WORM_H_
