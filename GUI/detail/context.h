#ifndef OMFG_GUI_CONTEXT_H
#define OMFG_GUI_CONTEXT_H

#include "util/rect.h"
#include "util/common.h"
#include "renderer.h"
#include "luaapi/types.h"
#include <iostream>
#include <map>
#include <list>
#include <string>
#include <vector>
#include <stack>

class LuaContext;

namespace OmfgGUI {

class Wnd;

typedef int KeyType;

class Context {
  public:
	friend struct GSSHandler;
	friend class Wnd; // TEMP

	enum { Unknown = 0, Button, Edit, List, Group, WndCount };

	struct MouseKey {
		enum type { Left = 0, Right, Middle };
	};

	typedef std::list<std::pair<std::string, std::list<std::string>>> GSSpropertyMap;

	struct GSSselector {
		struct Condition {
			enum Type { Tag = 0, Class, ID, State, Group };

			Condition(Type type, std::string v) : type(type), v(v) {}

			Type type;
			std::string v;
		};

		void addTag(std::string const &name) {
			cond.push_back(Condition(Condition::Tag, name));
		}

		void addClass(std::string const &name) {
			cond.push_back(Condition(Condition::Class, name));
		}

		void addID(std::string const &name) {
			cond.push_back(Condition(Condition::ID, name));
		}

		void addState(std::string const &name) {
			cond.push_back(Condition(Condition::State, name));
		}

		void addGroup(std::string const &name) {
			cond.push_back(Condition(Condition::Group, name));
		}

		int matchesWindow(Wnd *) const;

		std::list<std::string> &addProperty(std::string const &name) {
			props.push_back(std::pair<std::string, std::list<std::string>>(name, std::list<std::string>()));
			return props.back().second;
		}

		std::list<Condition> cond;
		GSSpropertyMap props;
	};

	GSSselector &addSelector() {
		m_gss.push_back(GSSselector());
		return m_gss.back();
	}

	typedef std::list<GSSselector> GSSselectors;

	Context(Renderer *renderer)
		: m_mouseCaptureWnd(0), m_rootWnd(0), m_keyboardFocusWnd(0), m_mouseFocusWnd(0), m_renderer(renderer) {}

	virtual ~Context() {}

	void updateGSS();

	// Sends a cursor relocation event
	void mouseMove(ulong newX, ulong newY);

	// Sends a mouse button down event
	void mouseDown(ulong newX, ulong newY, MouseKey::type button);

	// Sends a mouse button up event
	void mouseUp(ulong newX, ulong newY, MouseKey::type button);

	// Sends a printable character
	void charPressed(char c);

	// Sends a keydown event
	void keyDown(KeyType k, bool aShift, bool aAlt, bool aCtrl);

	// Sends a keyup event
	void keyUp(KeyType k, bool aShift, bool aAlt, bool aCtrl);

	void captureMouse(Wnd *aWnd) {
		m_mouseCaptureWnd = aWnd;
	}

	Wnd *getMouseCapture() {
		return m_mouseCaptureWnd;
	}

	virtual void setFocus(Wnd *aWnd);

	// Called by a window if it has focus and
	// is being hidden.
	virtual void hiddenFocus() {}

	// Called by a window if it has focus and
	// is turned visible again.
	virtual void shownFocus() {}

	virtual bool keyState(int key) = 0;

	Wnd *getFocus() {
		return m_keyboardFocusWnd;
	}

	void setActive(Wnd *wnd);

	void process();

	void render();

	void mouseDown(int x, int y, MouseKey::type button);
	void mouseUp(int x, int y, MouseKey::type button);
	void mouseMove(int x, int y);
	void mouseScroll(int x, int y, int offs);

	virtual void loadGSSFile(std::string const &, bool passive) = 0;
	virtual Wnd *loadXMLFile(std::string const &, Wnd *loadTo) = 0;

	// This is defined in gss.cpp
	void loadGSS(std::istream &s, std::string const &fileName);

	// This is defined in xml.cpp
	Wnd *buildFromXML(std::istream &s, Wnd *dest);

	template <class WndT>
	WndT *setRoot(WndT *wnd) {
		setRoot_(wnd);

		return wnd;
	}

	Wnd *getRoot() {
		return m_rootWnd;
	}

	void registerNamedWindow(std::string const &id, Wnd *wnd);

	void registerWindow(Wnd *wnd);

	void deregisterWindow(Wnd *wnd);

	Wnd *findNamedWindow(std::string const &id) {
		std::map<std::string, Wnd *>::iterator i = m_namedWindows.find(id);
		if (i == m_namedWindows.end())
			return 0;
		return i->second;
	}

	void destroy();

	Renderer *renderer() {
		return m_renderer;
	}

	virtual BaseFont *loadFont(std::string const &name) = 0;
	virtual BaseSpriteSet *loadSpriteSet(std::string const &name) = 0;

  protected:
	void setRoot_(Wnd *wnd);

	Wnd *m_mouseCaptureWnd;
	Wnd *m_rootWnd;
	Wnd *m_keyboardFocusWnd;
	Wnd *m_activeWnd;
	Wnd *m_mouseFocusWnd;
	Renderer *m_renderer;

	// Strong Lua reference keeping the root window (and thus the whole tree)
	// alive for the garbage collector. Without it the root's only reference
	// would be its own weak self-reference and the entire UI would be
	// collected. Released in destroy().
	LuaReference m_rootRef;

	std::map<std::string, Wnd *> m_namedWindows;

	GSSselectors m_gss;

	long m_cursorX;
	long m_cursorY;
};

} // namespace OmfgGUI

#endif // OMFG_GUI_CONTEXT_H
