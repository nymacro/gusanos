#ifndef BASE_PLAYER_H
#define BASE_PLAYER_H

#include <string>
#include <map>

#include "luaapi/types.h"
#include <stdexcept>
#include <boost/shared_ptr.hpp>
#include <vector>

#include "network_compat.h"

struct PlayerOptions;
class BaseWorm;
class BasePlayerInterceptor;
class WeaponType;
struct LuaEventDef;

#define COMPACT_EVENTS
#define COMPACT_ACTIONS

class BasePlayer {
  public:
	enum BaseActions {
		LEFT = 0,
		RIGHT,
		// UP,
		// DOWN,
		FIRE,
		JUMP,
		// CHANGE, // Probably useless Action
		NINJAROPE,
		DIG,
		RESPAWN,
		//
		ACTION_COUNT,
	};

	enum NetEvents {
		SYNC = 0,
		ACTION_STOP,
		ACTION_START,
		NAME_CHANGE,
		CHAT_MSG,
		COLOR_CHANGE,
		SELECT_WEAPONS,
		TEAM_CHANGE,
		LuaEvent,
		//
		EVENT_COUNT,
	};

	enum ReplicationItems { WormID, Other };

	struct Stats {
		Stats() : deaths(0), kills(0), damageDealt(0.f), damageTaken(0.f) {}

		~Stats();

		int deaths;
		int kills;
		float damageDealt;				// server-authoritative
		float damageTaken;				// server-authoritative
		std::map<int, int> weaponKills; // WeaponType index -> count (server-authoritative)
		LuaReference luaData;
	};

	// static LuaReference metaTable();

	// ClassID is Used by zoidcom to identify the class over the network,
	// do not confuse with the node ID which identifies instances of the class.
	static ZCom_ClassID classID;

	BasePlayer(boost::shared_ptr<PlayerOptions> options, BaseWorm *worm);
	virtual ~BasePlayer();

	void think();
	// subThink() gets called inside think() and its used to give the derivations
	// the ability to think without replacing the main BasePlayer::think().
	virtual void subThink() = 0;
#ifndef DEDSERV
	virtual void render() {}
#endif

	void assignNetworkRole(bool authority, ZCom_BitStream *announceData = nullptr, ZCom_NodeID net_id = 0);
	void setOwnerId(ZCom_ConnID id);

	void assignWorm(BaseWorm *worm);
	void removeWorm();

	void sendChatMsg(std::string const &message);
	void
	sendSyncMessage(ZCom_ConnID id); // Its the initializing message that is sent to new clients that recieve the node.

	void nameChangePetition(); // Asks the server to change the name to the one in the player options.

	void baseActionStart(BaseActions action, float intensity = 1.0f);
	void baseActionStop(BaseActions action, float intensity = 0.0f);

	void addKill();
	void addDeath();

	ZCom_NodeID getNodeID();
	void setNodeID(uint32_t id) {
		if (m_node)
			m_node->setNetworkID(id);
	}
	ZCom_Node *getNode() {
		return m_node;
	}
	ZCom_ConnID getConnectionID();
	void sendLuaEvent(LuaEventDef *event, eZCom_SendMode mode, zU8 rules, ZCom_BitStream *userdata, ZCom_ConnID connID);
	boost::shared_ptr<PlayerOptions> getOptions();
	BaseWorm *getWorm() {
		return m_worm;
	}

	LuaReference getLuaReference();
	void pushLuaReference();
	virtual void deleteThis();

	/*
		void* operator new(size_t count);

		void operator delete(void* block)
		{

		}

		void* operator new(size_t count, void* space)
		{
			return space;
		}
	*/
	boost::shared_ptr<Stats> stats;

	bool deleteMe;

	std::string m_name;
	int colour;
	int team;
	bool local;

	LuaReference luaData;

	void selectWeapons(std::vector<WeaponType *> const &weaps);

	void changeName(std::string const &name);

	// Changes the name only locally
	void localChangeName(std::string const &name, bool forceChange = false);

  protected:
	LuaReference luaReference;

	void addEvent(ZCom_BitStream *data, NetEvents event);
	void addActionStart(ZCom_BitStream *data, BaseActions action, float intensity = 1.0f);
	void addActionStop(ZCom_BitStream *data, BaseActions action, float intensity = 0.0f);

	void changeName_(const std::string &name); // Changes the name and if its server it will tell all clients about it.

	void changeColor_(int colour_);
	void colorChangePetition_(int colour_);
	void changeTeam_(int team_);
	void teamChangePetition_(int team_);

	BaseWorm *m_worm;
	boost::shared_ptr<PlayerOptions> m_options;

	bool m_isAuthority;
	bool m_processingNetworkEvent;
	// Slides for network throttling of analog movement intensity.
	int m_lastMoveQuant[2];

	ZCom_Node *m_node;
	BasePlayerInterceptor *m_interceptor;
	ZCom_NodeID m_wormID;
	ZCom_ConnID m_id;

	bool deleted; // TEMP
};

class BasePlayerInterceptor : public ZCom_NodeReplicationInterceptor {
  public:
	BasePlayerInterceptor(BasePlayer *parent);

	bool inPreUpdateItem(ZCom_Node *_node, ZCom_ConnID _from, eZCom_NodeRole _remote_role, ZCom_Replicator *_replicator,
						 zU32 _estimated_time_sent);

	// Not used virtual stuff
	void outPreReplicateNode(ZCom_Node *_node, ZCom_ConnID _to, eZCom_NodeRole _remote_role) {}
	void outPreDereplicateNode(ZCom_Node *_node, ZCom_ConnID _to, eZCom_NodeRole _remote_role) {}
	bool outPreUpdate(ZCom_Node *_node, ZCom_ConnID _to, eZCom_NodeRole _remote_role) {
		return true;
	}
	bool outPreUpdateItem(ZCom_Node *_node, ZCom_ConnID _to, eZCom_NodeRole _remote_role,
						  ZCom_Replicator *_replicator) {
		return true;
	}
	void outPostUpdate(ZCom_Node *_node, ZCom_ConnID _to, eZCom_NodeRole _remote_role, zU32 _rep_bits, zU32 _event_bits,
					   zU32 _meta_bits) {}
	bool inPreUpdate(ZCom_Node *_node, ZCom_ConnID _from, eZCom_NodeRole _remote_role) {
		return true;
	}
	void inPostUpdate(ZCom_Node *_node, ZCom_ConnID _from, eZCom_NodeRole _remote_role, zU32 _rep_bits,
					  zU32 _event_bits, zU32 _meta_bits) {};

	virtual ~BasePlayerInterceptor() {}

  private:
	BasePlayer *m_parent;
};

#endif // _BASE_PLAYER_H_
