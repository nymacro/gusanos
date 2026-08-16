#include "console.h"
#include "variables.h"
#include "command.h"
#include "special_command.h"
#include "alias.h"
#include "util/text.h"
#include "consoleitem.h"

#include "console-grammar.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <stack>
#include <cctype>
#include <iterator>
#include <sstream>
#include <iostream>
#include <algorithm>

using namespace std;

Console::Console() : m_logMaxSize(256) {
}

Console::Console(int logMaxSize) : m_logMaxSize(logMaxSize) {
}

Console::~Console() {
	for (auto i = items.begin(), end = items.end(); i != end; ++i) {
		delete i->second;
	}
}

//============================= INTERFACE ====================================

void Console::registerItem(std::string const &name, ConsoleItem *item) {
	item->m_owner = this;
	items[name] = item;
}

void Console::registerVariable(Variable *var) {
	string const &name = var->getName();
	if (!name.empty()) {
		std::map<std::string, ConsoleItem *, IStrCompare>::iterator i = items.find(name);
		if (i != items.end()) {
			delete i->second;
		}

		registerItem(name, var);
	} else {
		delete var;
	}
}

void Console::registerCommand(std::string const &name, Command *command) {
	if (!name.empty() && items.find(name) == items.end()) {
		registerItem(name, command);
	} else
		delete command;
}

void Console::registerSpecialCommand(const std::string &name, int index,
									 std::string (*func)(int, const std::list<std::string> &)) {
	if (!name.empty()) {
		ItemMap::iterator tempItem = items.find(name);
		if (tempItem == items.end()) {
			registerItem(name, new SpecialCommand(index, func));
		}
	}
}

void Console::registerAlias(const std::string &name, const std::string &action) {
	if (!name.empty()) {
		ItemMap::iterator tempItem = items.find(name);
		if (tempItem == items.end() || !tempItem->second->isLocked()) {
			if (tempItem != items.end())
				delete tempItem->second;
			registerItem(name, new Alias(name, action));
		}
	}
}

void Console::clearTemporaries() {
	for (auto it = items.begin(); it != items.end();) {
		auto next = std::next(it);
		if (it->second->temp) {
			delete it->second;
			items.erase(it);
		}
		it = next;
	}
}

struct TestHandler : public ConsoleGrammarBase {
	TestHandler(std::istream &str_, Console &console_, bool parseRelease_ = false)
		: str(str_), console(console_), parseRelease(parseRelease_) {
		next();
	}

	int cur() {
		return c;
	}

	void next() {
		c = str.get();
		if (c == std::istream::traits_type::eof())
			c = -1;
	}

	std::string invoke(std::string const &name, std::list<std::string> const &args) {
		return console.invoke(name, args, parseRelease);
	}

	int c;
	std::istream &str;
	Console &console;
	bool parseRelease;
};

void Console::parseLine(const string &text, bool parseRelease) {
	// '#' starts a comment (respects quoted strings); lines that become
	// empty after stripping are silently ignored.
	string line = stripComment(text);
	size_t end = line.find_last_not_of(" \t\r\n");
	if (end != string::npos)
		line = line.substr(0, end + 1);
	else
		line = "";
	if (line.empty())
		return;

	std::istringstream ss(line);
	ConsoleGrammar<TestHandler> handler((TestHandler(ss, *this, parseRelease)));

	try {
		addLogMsg(handler.block());
	} catch (SyntaxError const &error) {
		addLogMsg(text);
		std::streamoff pos = handler.str.tellg();
		if (pos > 0) {
			addLogMsg(std::string(pos - 1, '-') + '^');
			addLogMsg(error.what());
		} else {
			addLogMsg(error.what() + string(" at end of input"));
		}
	}
}

string Console::stripComment(string const &text) {
	bool inQuotes = false;
	size_t firstNonSpace = 0;
	for (size_t i = 0; i < text.size(); ++i) {
		char c = text[i];
		if (!inQuotes && c == '#') {
			if (firstNonSpace == 0 || firstNonSpace > i) {
				return "";
			}
			return text.substr(0, i);
		}
		if (c == '\\') {
			++i;
			continue;
		}
		if (c == '"') {
			inQuotes = !inQuotes;
			continue;
		}
		if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
			if (firstNonSpace == 0)
				firstNonSpace = i;
		}
	}
	return text;
}

std::string Console::invoke(string const &name, list<string> const &args, bool parseRelease) {
	if (!parseRelease || name[0] == '+') {
		std::string nameCopy(name);
		if (parseRelease)
			nameCopy[0] = '-';

		map<string, ConsoleItem *>::iterator tempItem = items.find(nameCopy);
		if (tempItem != items.end()) {
			return tempItem->second->invoke(args);
		} else
			return "UNKNOWN COMMAND: " + name;
	}

	return "";
}

void Console::addLogMsg(const string &msg) {
	if (!msg.empty()) {
		if (log.size() >= m_logMaxSize)
			log.pop_front();

		log.push_back(msg);
	}
}

void Console::analizeKeyEvent(bool state, char key) {
	if (state == true)
		parseLine(bindTable.getBindingAction(key));
	else
		parseLine(bindTable.getBindingAction(key), true);
}

void Console::bind(char key, const string &action) {
	bindTable.bind(key, action);
}

char Console::getKeyForBinding(std::string const &action) {
	return bindTable.getKeyForAction(action);
}

std::string Console::getActionForBinding(char key) {
	return bindTable.getBindingAction(key);
}

int Console::executeConfig(const string &filename) {
	ifstream file(filename.c_str());

	if (file.is_open() && file.good()) {
		string text2Parse;
		while (portable_getline(file, text2Parse)) {
			parseLine(text2Parse);
		}

		return 1;
	}

	return 0;
};

struct CompletionHandler : public ConsoleGrammarBase {
	struct State {
		State() {}

		State(string::const_iterator b_)
			: commandComplete(false), argumentComplete(false), beginArgument(b_), beginCommand(b_), argumentIdx(0) {}

		bool commandComplete;
		bool argumentComplete;
		std::string command;
		std::string argument;
		string::const_iterator beginArgument;
		string::const_iterator beginCommand;
		int argumentIdx;
	};

	CompletionHandler(string::const_iterator b_, string::const_iterator e_, Console &console_)
		: beginPrefix(b_), b(b_), e(e_), endPrefix(b_), current(b_), console(console_) {
		c = (unsigned char)*b;
	}

	int cur() {
		return c;
	}

	void next() {
		++b;
		if (b == e) {
			if (!current.commandComplete) {
				current.command = std::string(current.beginCommand, b);
			} else if (!current.argumentComplete) {
				if (current.beginArgument != e)
					current.argument = std::string(current.beginArgument, b);
				else
					endPrefix = b;
			}

			throw current;
		}
		c = (unsigned char)*b;
	}

	void inCommand() {
		states.push(current);
		current.commandComplete = false;
		current.beginCommand = b;
		endPrefix = b;
	}

	void inArguments(std::string const &command) {
		current.commandComplete = true;
		current.argumentIdx = 0;
		current.argumentComplete = false;
		current.argument = "";
		current.beginArgument = e;
		current.command = command;
	}

	void inArgument(int idx) {
		current.commandComplete = true;
		current.argumentComplete = false;
		current.argumentIdx = idx;
		current.beginArgument = b;
		endPrefix = b;
	}

	void outArgument(int idx, std::string const &argument) {
		current.argumentComplete = false;
		current.argumentIdx = idx + 1;
		current.argument = "";
		current.beginArgument = e;
	}

	void outCommand() {
		if (!states.empty()) {
			current = states.top();
			states.pop();
		}
	}

	std::string prefix() {
		return std::string(beginPrefix, endPrefix);
	}

	int c;
	string::const_iterator beginPrefix;
	string::const_iterator b;
	string::const_iterator e;
	string::const_iterator endPrefix;

	State current;
	std::stack<State> states;
	Console &console;
};

struct ItemGetText {
	template <class IteratorT>
	std::string const &operator()(IteratorT i) const {
		return i->first;
	}
};

std::string Console::completeCommand(std::string const &b) {
	return shellComplete(items, b.begin(), b.end(), ItemGetText(), ConsoleAddLines(*this));
}

string Console::autoComplete(string const &text) {
	string returnText = text;

	if (!text.empty()) {
		ConsoleGrammar<CompletionHandler> handler((CompletionHandler(text.begin(), text.end(), *this)));

		try {
			handler.block();
		} catch (CompletionHandler::State result) {
			if (result.commandComplete) {
				ItemMap::const_iterator item = items.find(result.command);

				if (item != items.end()) {
					return handler.prefix() + item->second->completeArgument(result.argumentIdx, result.argument);
				}

				return text;
			} else {
				return handler.prefix() + completeCommand(result.command);
			}
		} catch (SyntaxError const &error) {
			return text;
		}
	}

	return text;
}

void Console::listItems(const string &text) {
	if (!text.empty()) {
		map<string, ConsoleItem *>::iterator item = items.lower_bound(text);
		if (item != items.end() && text == item->first.substr(0, text.length())) // If found
		{
			map<string, ConsoleItem *>::iterator tempItem = item;
			tempItem++;

			// If the temp item is equal to the first item found there are more than 1 matches
			if (tempItem != items.end())
				if (tempItem->first.substr(0, text.length()) == item->first.substr(0, text.length())) {

					addLogMsg("]");

					while (item != items.end() && text == item->first.substr(0, text.length())) {
						addLogMsg(item->first);
						item++;
					}
				}
		}
	}
}
