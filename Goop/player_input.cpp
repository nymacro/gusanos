#ifndef DEDSERV
#include "player_input.h"

#include "gamepad.h"
#include "game.h"
#include "player.h"
#include "gconsole.h"
#include "glua.h"
#include "luaapi/context.h"
#include "util/text.h"
#include "util/log.h"
#include "util/stringbuild.h"
#include <boost/bind/bind.hpp>
using namespace boost::placeholders;
#include <boost/lexical_cast.hpp>

#include <string>
#include <list>
#include <vector>
#include <array>
#include <cctype>

using namespace std;

namespace {

// Per-key active state for one-shot (non-continuous) analog bindings.
std::array<bool, 256> g_analogActive = {};

bool parsePlayerAction(const std::string &s, size_t &index, Player::Actions &action) {
	if (s.size() < 3 || (s[0] != '+' && s[0] != '-'))
		return false;
	size_t i = 1;
	if (s[i] != 'P')
		return false;
	++i;
	if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i])))
		return false;
	index = 0;
	while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
		index = index * 10 + (s[i] - '0');
		++i;
	}
	std::string suffix = s.substr(i);

	static const struct {
		const char *name;
		Player::Actions action;
	} map[] = {
		{"_LEFT", Player::LEFT},
		{"_RIGHT", Player::RIGHT},
		{"_UP", Player::UP},
		{"_DOWN", Player::DOWN},
		{"_FIRE", Player::FIRE},
		{"_JUMP", Player::JUMP},
		{"_CHANGE", Player::CHANGE},
		{"_WEAPON_NEXT", Player::WEAPON_NEXT},
		{"_WEAPON_PREV", Player::WEAPON_PREV},
		{"_NINJAROPE", Player::NINJAROPE},
	};
	for (size_t k = 0; k < sizeof(map) / sizeof(map[0]); ++k) {
		if (map[k].name == suffix) {
			action = map[k].action;
			return true;
		}
	}
	return false;
}

bool isContinuousAction(Player::Actions action) {
	return action == Player::LEFT || action == Player::RIGHT || action == Player::UP || action == Player::DOWN;
}

} // namespace

// Called from GamepadHandler::analogInput for the eight stick directions.
// The normalized magnitude is an implementation detail and is not exposed in
// bind strings. One-shot actions stay boolean (one activation per deadzone
// crossing); continuous actions receive the live intensity every frame.
void onGamepadAnalog(int key, float value) {
	unsigned char ukey = static_cast<unsigned char>(key);
	std::string action = console.getActionForBinding(static_cast<char>(ukey));
	if (action.empty())
		return;

	size_t index;
	Player::Actions paction;
	if (!parsePlayerAction(action, index, paction)) {
		// Non-player command bound to a stick: treat as one-shot.
		bool active = g_analogActive[ukey];
		if (value > 0.0f && !active) {
			console.invoke(action, std::list<std::string>(), false);
			g_analogActive[ukey] = true;
		} else if (value == 0.0f && active) {
			std::string release = action;
			if (!release.empty() && release[0] == '+')
				release[0] = '-';
			console.invoke(release, std::list<std::string>(), false);
			g_analogActive[ukey] = false;
		}
		return;
	}

	if (index >= game.localPlayers.size())
		return;

	Player &player = *game.localPlayers[index];

	if (isContinuousAction(paction)) {
		if (value > 0.0f)
			player.actionStart(paction, value);
		else
			player.actionStop(paction);
	} else {
		bool active = g_analogActive[ukey];
		if (value > 0.0f && !active) {
			player.actionStart(paction, 1.0f);
			g_analogActive[ukey] = true;
		} else if (value == 0.0f && active) {
			player.actionStop(paction);
			g_analogActive[ukey] = false;
		}
	}
}

string eventStart(size_t index, Player::Actions action, list<string> const &args) {
	float intensity = 1.0f;
	if (!args.empty()) {
		try {
			intensity = boost::lexical_cast<float>(args.front());
		} catch (...) {
			intensity = 1.0f;
		}
	}
	if (index < game.localPlayers.size()) {
		Player &player = *game.localPlayers[index];

		bool ignore = dispatchCallbacksVeto(LuaCallbacks::localplayerEvent + action, player.getLuaReference(), true);
		if (dispatchCallbacksVeto(LuaCallbacks::localplayerEventAny, player.getLuaReference(), static_cast<int>(action),
								  true))
			ignore = true;

		if (!ignore)
			player.actionStart(action, intensity);
	}
	return "";
}

string eventStop(size_t index, Player::Actions action, list<string> const &args) {
	if (index < game.localPlayers.size()) {
		Player &player = *game.localPlayers[index];

		bool ignore = dispatchCallbacksVeto(LuaCallbacks::localplayerEvent + action, player.getLuaReference(), false);
		if (dispatchCallbacksVeto(LuaCallbacks::localplayerEventAny, player.getLuaReference(), static_cast<int>(action),
								  false))
			ignore = true;

		if (!ignore)
			player.actionStop(action);
	}
	return "";
}

void registerPlayerInput() {
	gamepadHandler.analogInput.connect(0, &onGamepadAnalog);

	for (size_t i = 0; i < Game::MAX_LOCAL_PLAYERS; ++i) {
		static char const *actionNames[] = {"_LEFT", "_RIGHT",	"_UP",			"_DOWN",		"_FIRE",
											"_JUMP", "_CHANGE", "_WEAPON_NEXT", "_WEAPON_PREV", "_NINJAROPE"};

		for (int action = Player::LEFT; action < Player::ACTION_COUNT; ++action) {
			console.registerCommands()((S_("+P") << i << actionNames[action]),
									   boost::bind(eventStart, i, (Player::Actions)action, _1))(
				(S_("-P") << i << actionNames[action]), boost::bind(eventStop, i, (Player::Actions)action, _1));
		}
	}
	console.registerCommands()("SAY", say);
}

string say(const list<string> &args) {
	if (!args.empty()) {
		if (!game.localPlayers.empty()) {
			game.localPlayers[0]->sendChatMsg(*(args.begin()));
		}
	} else {
		return "SAY <MESSAGE> : SENDS A MESSAGE TO THE OTHER PLAYERS ON THE SERVER";
	}
	return "";
}

#endif
