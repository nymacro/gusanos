#ifndef NETWORK_H
#define NETWORK_H

#include "network_compat.h"
#include <string>
#include <memory>
#include <boost/function.hpp>
#include "luaapi/types.h"
#include "base_player.h"

#include "message_queue.h"

const unsigned int INVALID_NODE_ID = 0;

namespace HTTP {
struct Request;
}

typedef boost::function<void(std::unique_ptr<HTTP::Request>)> HttpRequestCallback;

struct LuaEventDef {
	static LuaReference metaTable;

	LuaEventDef(std::string name_, LuaReference callb_) : name(name_), idx(0), callb(callb_) {}

	~LuaEventDef();
	void call(ZCom_BitStream *);
	void call(LuaReference, ZCom_BitStream *);

	void *operator new(size_t count);

	void operator delete(void *block) {
		// Lua frees the memory
	}

	void *operator new(size_t count, void *space) {
		return space;
	}

	std::string name;
	size_t idx;
	LuaReference callb;
	LuaReference luaReference;
};

class Network {
  public:
	friend class Client;
	friend class Server;

	static int const protocolVersion;

	enum NetEvents {
		PLAYER_REQUEST = 10,
		RConMsg,
		ConsistencyInfo,
	};

	enum DConnEvents { Kick, ServerMapChange, Quit, IncompatibleData, IncompatibleProtocol };

	enum State {
		StateIdle,			// Not doing anything
		StateDisconnecting, // Starting disconnection sequence
		StateDisconnected,	// Disconnected

		StateConnecting, // Pseudo-state
		StateHosting,	 // Pseudo-state
	};

	struct ClientEvents {
		enum type { LuaEvents, Max };
	};

	struct ConnectionReply {
		enum type {
			Refused = 0,
			Retry,
			Banned,
		};
	};

	struct LuaEventGroup {
		enum type { Game, Player, Worm, Particle, Max };
	};

  public:
	Network();
	~Network();

	static void log(char const *msg);

	static void init();
	static void shutDown();
	static void registerInConsole();
	static void update();

	static void host();
	static void connect(const std::string &address);
	static void disconnect(DConnEvents event = Quit);
	static void disconnect(ZCom_ConnID id, DConnEvents event);
	static void reconnect(int delay = 1);
	static void clear();

	static void kick(ZCom_ConnID connID);
	static void ban(ZCom_ConnID connID);

	static void setServerID(ZCom_ConnID serverID);
	static ZCom_ConnID getServerID();

	static bool isHost();
	static bool isClient();

	static std::unique_ptr<HTTP::Request> fetchServerList();
	static void addHttpRequest(std::unique_ptr<HTTP::Request>, HttpRequestCallback);

	static LuaEventDef *addLuaEvent(LuaEventGroup::type, char const *name, LuaEventDef *event);
	static void indexLuaEvent(LuaEventGroup::type, char const *name);
	static LuaEventDef *indexToLuaEvent(LuaEventGroup::type type, int idx);
	static void encodeLuaEvents(ZCom_BitStream *data);

	static ZCom_Control *getZControl();
	static int getServerPing();

	static BasePlayer::Stats *findSavedStats(unsigned int uniqueID);
	static std::string findSavedName(unsigned int uniqueID);

	static void incConnCount();
	static void decConnCount();

	static bool isDisconnected();
	static bool isDisconnecting();

	static bool isBanned(ZCom_ConnID connID);

	int simLag;
	float simLoss;
	int upLimit;
	int downBPP;
	int downPPS;
	int checkCRC;
	bool clientRetry;
	bool autoDownloads;

  public:
	static void setClient(bool v);

  private:
};

extern Network network;

#endif // NETWORK_H