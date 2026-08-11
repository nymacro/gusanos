#include "game.h"
#include "network.h"
#include "base_player.h"
#include "console.h"
#include "util/text.h"

#include <string>
#include <list>
#include <algorithm>
#include <cctype>

using std::list;
using std::string;

namespace {

string mapCmd(const list<string> &args) {
	if (!args.empty()) {
		string tmp = *args.begin();
		std::transform(tmp.begin(), tmp.end(), tmp.begin(), (int (*)(int))tolower);
		/*
		if(!game.changeLevelCmd( tmp ))
			return "ERROR LOADING MAP";
		*/
		// mq_queue(game.msg, Game::ChangeLevel, tmp);
		game.changeLevelCmd(tmp);
		return "";
	}
	return "MAP <MAPNAME> : LOAD A MAP";
}

struct MapIterGetText {
	template <class IteratorT>
	std::string const &operator()(IteratorT i) const {
		return i->first;
	}
};

string mapCompleter(Console *con, int idx, std::string const &beginning) {
	if (idx != 0)
		return beginning;

	return shellComplete(levelLocator.getMap(), beginning.begin(), beginning.end(), MapIterGetText(),
						 ConsoleAddLines(*con));
}

string gameCmd(const list<string> &args) {
	if (!args.empty()) {
		string tmp = *args.begin();
		std::transform(tmp.begin(), tmp.end(), tmp.begin(), (int (*)(int))tolower);
		if (!game.setMod(tmp))
			return "MOD " + tmp + " NOT FOUND";
		return "THE GAME WILL CHANGE THE NEXT TIME YOU CHANGE MAP";
	}
	return "GAME <MODNAME> : SET THE MOD TO LOAD THE NEXT MAP CHANGE";
}

struct GameIterGetText {
	template <class IteratorT>
	std::string const &operator()(IteratorT i) const {
		return *i;
	}
};

string gameCompleter(Console *con, int idx, std::string const &beginning) {
	if (idx != 0)
		return beginning;

	return shellComplete(game.modList, beginning.begin(), beginning.end(), GameIterGetText(), ConsoleAddLines(*con));
}

string addbotCmd(const list<string> &args) {
	if (!network.isClient()) {
		int team = -1;
		list<string>::const_iterator i = args.begin();
		if (i != args.end()) {
			team = cast<int>(*i);
			++i;
		}
		game.addBot(team);
		return "";
	} else {
		return "You cant add bots as client";
	}
}

string connectCmd(const list<string> &args) {
	if (!args.empty()) {
		network.connect(*args.begin());
		return "";
	}
	return "CONNECT <HOST_ADDRESS> : JOIN A NETWORK SERVER";
}

string rConCmd(const list<string> &args) {
	if (!args.empty() && network.isClient()) {

		list<string>::const_iterator iter = args.begin();
		string tmp = *iter++;
		for (; iter != args.end(); ++iter) {
			tmp += " \"" + *iter + '"';
		}
		game.sendRConMsg(tmp);
		return "";
	}
	return "";
}

string rConCompleter(Console *con, int idx, std::string const &beginning) {
	if (idx != 0)
		return beginning;

	return con->completeCommand(beginning);
}

BasePlayer *findPlayerByName(std::string const &name) {
	// BasePlayer* player2Kick = 0;
	// for ( std::list<BasePlayer*>::iterator iter = game.players.begin(); iter != game.players.end(); iter++)
	for (auto iter : game.players) {
		if (iter->m_name == name) {
			return iter;
		}
	}

	return 0;
}

string banCmd(list<string> const &args) {
	if (!network.isClient() && !args.empty()) {
		if (BasePlayer *player = findPlayerByName(*args.begin()))
			if (!player->local) {
				network.ban(player->getConnectionID());
				return "PLAYER BANNED";
			}
		return "PLAYER NOT FOUND OR IS LOCAL";
	}
	return "BAN <PLAYER_NAME> : BANS THE PLAYER WITH THE SPECIFIED NAME";
}

string kickCmd(const list<string> &args) {
	if (!network.isClient() && !args.empty()) {
		if (BasePlayer *player2Kick = findPlayerByName(*args.begin()))
			if (!player2Kick->local) {
				player2Kick->deleteMe = true;
				network.kick(player2Kick->getConnectionID());
				return "PLAYER KICKED";
			}
		return "PLAYER NOT FOUND OR IS LOCAL";
	}
	return "KICK <PLAYER_NAME> : KICKS THE PLAYER WITH THE SPECIFIED NAME";
}

struct BasePlayerIterGetText {
	template <class IteratorT>
	std::string const &operator()(IteratorT i) const {
		return (*i)->m_name;
	}
};

string kickCompleter(Console *con, int idx, std::string const &beginning) {
	if (idx != 0)
		return beginning;

	return shellComplete(game.players, beginning.begin(), beginning.end(), BasePlayerIterGetText(),
						 ConsoleAddLines(*con));
}

} // namespace

void registerGameCommands(Console &console) {
	console.registerCommands()("MAP", mapCmd, mapCompleter)("GAME", gameCmd, gameCompleter)("ADDBOT", addbotCmd)(
		"CONNECT", connectCmd)("RCON", rConCmd, rConCompleter)("KICK", kickCmd, kickCompleter)("BAN", banCmd,
																							   kickCompleter);
}
