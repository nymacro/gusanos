#ifndef KEYBOARD_h
#define KAYBOARD_h

#include <allegro.h>
#include <list>

#define KEY_EVENT_NONE	0
#define KEY_EVENT_PRESS	1
#define KEY_EVENT_RELEASE 2

#define BUFF_SIZE 64

using namespace std;

struct KeyEvent
{
	int	type;
	char	key;
};

class KeyHandler
{
	public:
	
	KeyHandler(void);
	~KeyHandler(void);
	
	void init();
	void pollKeyboard();
	KeyEvent getEvent();
	
	private:
	
	list<KeyEvent> events;
	
	bool oldKeys[KEY_MAX]; // KEY_MAX is defined by allegro (usually 119)
	
	void addEvent(int type, char key);
	
};

#endif  // _KEYBOARD_h_