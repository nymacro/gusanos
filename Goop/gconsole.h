#ifndef gconsole_h
#define gconsole_h

// SYSTEM INCLUDES
//

// PROJECT INCLUDES
//
#include "keyboard.h"
#include "console.h"
#include "font.h"

// LOCAL INCLUDES
//
#include <string>
#include <list>
#include <map>

// FORWARD REFERENCES
//

class GConsole : public Console
{
	public:

	GConsole();
	
	void init();
	void shutDown();
	void checkInput();
	void render(BITMAP *where);

	private:
	
	KeyHandler keyHandler;
	int m_mode;
	Font *m_font;
	std::string m_inputBuff;
	
	enum
	{
		CONSOLE_MODE_INPUT,
		CONSOLE_MODE_BINDINGS
	};
};

std::string bindCmd(const std::list<std::string> &args);

extern GConsole console;

#endif  // _gconsole_h_
