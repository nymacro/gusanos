#ifndef KEYBOARD_h
#define KEYBOARD_h

#ifdef DEDSERV
#error "Can't use this in dedicated server"
#endif // DEDSERV

#include "allegro_compat.h"
#include <boost/signals2/signal.hpp>

struct StopEarly {
	typedef bool result_type;

	template <typename InputIterator>
	bool operator()(InputIterator first, InputIterator last) const {
		for (; first != last; ++first) {
			if (!*first)
				return false;
		}

		return true;
	}
};

class KeyHandler {
  public:
	KeyHandler(void);
	~KeyHandler(void);

	void init();
	void shutDown();
	void pollKeyboard(); // Isn't "poll" a better name?

	static int mapKey(int k);
	static bool getKey(int k);

	boost::signals2::signal<bool(int), StopEarly> keyDown;
	boost::signals2::signal<bool(int), StopEarly> keyUp;
	boost::signals2::signal<bool(char, int), StopEarly> printableChar;

  private:
	bool oldKeys[KEY_MAX]; // KEY_MAX is defined by allegro (usually 119)
};

extern KeyHandler keyHandler;

#endif // _KEYBOARD_h_
