#ifndef GUSANOS_GCONSOLE_H
#define GUSANOS_GCONSOLE_H

#include <console.h>
// #include "font.h"

#ifndef DEDSERV
#include "allegro_compat.h"
#endif

#include <list>
#include <string>
#include <list>
#include <map>
#include <set>
#include <boost/array.hpp>
using boost::array;

#ifndef DEDSERV
class SpriteSet;
class Font;
#endif

class GConsole : public Console {
  public:
#ifndef DEDSERV
	struct BindingLock {
		BindingLock() {
			enable.fill(true);
		}

		array<bool, 256> enable;
	};
#endif

	GConsole();

	void init();
	void shutDown();
	void loadResources();
	void checkInput();
#ifndef DEDSERV
	void render(BITMAP *where, bool fullScreen = false);
#endif
	void think();
	int executeConfig(const std::string &filename);
	void addLogMsg(const std::string &msg) override;

#ifndef DEDSERV
	bool eventPrintableChar(char c, int k);
	bool eventKeyDown(int k);
	bool eventKeyUp(int k);
	bool eventGamepadDown(int k);
	bool eventGamepadUp(int k);

	std::string setConsoleKey(std::list<std::string> const &args);

	void lockBindings(BindingLock const &lock) {
		if (m_locks.insert(&lock).second) {
			for (size_t i = 0; i < m_lockRefCount.size(); ++i) {
				if (lock.enable[i])
					++m_lockRefCount[i];
			}
		}
		m_bindingsLocked = true;
	}

	void releaseBindings(BindingLock const &lock) {
		std::set<BindingLock const *>::iterator l = m_locks.find(&lock);
		if (l != m_locks.end()) {
			m_locks.erase(l);

			for (size_t i = 0; i < m_lockRefCount.size(); ++i) {
				if (lock.enable[i])
					--m_lockRefCount[i];
			}
		}
		m_bindingsLocked = false;
	}

	// Returns true if normal key bindings are currently locked out.
	// Gamepad button codes can exceed the 256-entry m_lockRefCount array,
	// so bound them before indexing to avoid std::out_of_range.
	bool bindingsLocked(int k) const {
		if (k < 0 || k >= static_cast<int>(m_lockRefCount.size()))
			return false;
		return m_lockRefCount[k] > 0;
	}

	void varCbFont(std::string oldValue);
#endif

	void addQueueCommand(std::string const &command);

  private:
	float m_pos;
	float speed;
	int height;
	int m_mode;

	// KeyHandler keyHandler;

#ifndef DEDSERV
	Font *m_font;
	std::string m_fontName;
	int m_consoleKey;

	std::string m_inputBuff;
	SpriteSet *background;
	std::set<BindingLock const *> m_locks;
	array<int, 256> m_lockRefCount;

	// Tracks whether this GContext currently holds the binding lock.
	// Used to make lock/release idempotent (see GContext::setFocus /
	// hiddenFocus / shownFocus), preventing the once-global lock from
	// getting stuck on after menu navigation or chat focus churn.
	bool m_bindingsLocked = false;
#endif

	std::list<std::string>::reverse_iterator logRenderPos; // For scrolling
	bool scrolling;

	std::list<std::string> commandsLog;
	std::list<std::string>::iterator currentCommand;

	std::list<std::string> commandsQueue;

	enum { CONSOLE_MODE_INPUT, CONSOLE_MODE_BINDINGS };
};

std::string bindCmd(const std::list<std::string> &args);
std::string gpInfoCmd(const std::list<std::string> &args);

extern GConsole console;

#endif // GUSANOS_GCONSOLE_H
