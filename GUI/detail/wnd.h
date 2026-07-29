#ifndef OMFG_GUI_WND_H
#define OMFG_GUI_WND_H

#include <string>
#include "util/rect.h"
#include "util/common.h"
#include "renderer.h"
#include "context.h"
#include "luaapi/types.h"

#include <iostream>
#include <list>
#include <map>
#include <set>
using std::cerr;
using std::endl;

namespace OmfgGUI {

// Tracks live GUI objects (Wnd and ListNode) by pointer VALUE (never
// dereferenced after free). The Lua finalizers (__gc) must not double-free an
// object that was already torn down via a C++ delete (the root window is
// deleted by Context::destroy() and then finalized again at lua_close). The
// destructor erases the pointer, so a later finalizer sees it is gone and
// skips the second delete - without reading freed memory.
inline std::set<void *> &guiAliveSet() {
	static std::set<void *> s;
	return s;
}

class Wnd {
  public:
	friend class Context;
	friend class Context::GSSselector;

	static LuaReference metaTable;

	enum Dir { Up = 0, Right, Down, Left };

	enum LuaCallbacks {
		OnAction = 0,
		OnKeyDown,
		OnActivate,
		LuaCallbacksMax,
	};

	Wnd(Wnd *parent, std::map<std::string, std::string> const &attributes, std::string const &tagLabel = "window")
		: m_focusable(true) /*, m_text(text)*/, m_parent(0), m_lastChildFocus(0), m_context(0), m_font(0),
		  m_tagLabel(tagLabel) /*, m_className(className), m_id(id)*/
		  ,
		  m_attributes(attributes), m_visible(true), m_active(false), m_destroyed(false) {
		guiAliveSet().insert(this);
		getAttrib("label", m_text);
		getAttrib("class", m_id);
		getAttrib("id", m_id);
		getAttrib("group", m_group);
		std::string v;
		if (getAttrib("selectable", v))
			m_focusable = (v != "0");

		// NOTE: parenting is intentionally NOT done here. The Lua creation
		// macros assign luaReference only AFTER the constructor returns, so a
		// parent->addChild() call from inside the constructor would see a zero
		// luaReference and skip creating the strong ownership reference that
		// keeps the child alive for the GC. Callers (xml.cpp, the gui_* Lua
		// bindings) add the window to its parent explicitly after creation.
	}

	virtual ~Wnd();

	void *operator new(size_t count);

	void operator delete(void *block) {
		// Lua frees the memory
	}

	void *operator new(size_t count, void *space) {
		return space;
	}

	bool doRender(Rect const &clip);

	void doProcess();

	/*
		returns: true if anything was rendered, false otherwise
	*/
	virtual bool render();

	virtual void process();

	virtual void setActivation(bool active);

	void doSetActivation(bool active);

	void toggleActivation() {
		doSetActivation(!m_active);
	}

	bool doAction();

	virtual void setText(std::string const &aStr);

	std::string const &getText() const {
		return m_text;
	}

	Rect const &getRect() {
		return m_rect;
	}

	Wnd *getParent() {
		return m_parent;
	}

	std::string const &getID() {
		return m_id;
	}

	bool getAttrib(std::string const &name, std::string &dest);

	void getCoord(int &dx, int &dy, int x, int y);
	void getCoordX(int &dx, int x);
	void getCoordY(int &dy, int y);

	static bool readColor(RGB &dest, std::string const &str);

	void applyGSSreally(Context::GSSselectors const &style);
	// void applyGSSstate(Context::GSSselectors const& style, std::string const& state);
	virtual void applyGSS(Context::GSSselectors const &);

	void applyGSS() {
		applyGSS(m_context->m_gss);
	}

	virtual void applyFormatting(Context::GSSpropertyMap const &);
	void updatePlacement();

	void allocateSpace(int &x, int &y, int width, int height);

	Wnd *findClosestChild(Wnd *org, Dir direction);

	void doUpdateGSS();

	// Sends a cursor relocation event
	bool doMouseMove(ulong aNewX, ulong aNewY);

	// Sends a mouse button down event
	bool doMouseDown(ulong aNewX, ulong aNewY, Context::MouseKey::type aButton);

	// Sends a mouse button up event
	bool doMouseUp(ulong aNewX, ulong aNewY, Context::MouseKey::type aButton);

	bool doMouseScroll(ulong newX, ulong newY, int offs);

	// Sends a cursor relocation event
	virtual bool mouseMove(ulong aNewX, ulong aNewY);

	// Sends a mouse button down event
	virtual bool mouseDown(ulong aNewX, ulong aNewY, Context::MouseKey::type aButton);

	// Sends a mouse button up event
	virtual bool mouseUp(ulong aNewX, ulong aNewY, Context::MouseKey::type aButton);

	virtual bool mouseScroll(ulong newX, ulong newY, int offs);

	bool doKeyDown(int key);

	bool doKeyUp(int key);

	virtual bool keyDown(int key);

	virtual bool keyUp(int key);

	virtual bool charPressed(char c, int key);

	virtual int classID();

	void pushReference();

	virtual bool registerCallback(std::string const &name, LuaReference callb);

	void setGroup(std::string newGroup);

	void addChild(Wnd *ch);

	void removeChild(Wnd *ch);

	Wnd *getChildByName(std::string const &name) {
		std::map<std::string, Wnd *>::const_iterator i = m_namedChildren.find(name);
		if (i != m_namedChildren.end())
			return i->second;
		return 0;
	}

	void setVisibility(bool v) {
		m_visible = v;
		if (!v)
			notifyHide();
		else
			notifyShow();
	}

	void notifyHide();

	void notifyShow();

	bool isVisibile();

	bool isActive() {
		return m_active;
	}

	bool isFocused() {
		return m_context && m_context->getFocus() == this;
	}

	bool switchTo();

	void focus() {
		if (m_context)
			m_context->setFocus(this);
	}

	void setSubFocus(Wnd *p) {
		m_lastChildFocus = p;
	}

	Context *context() {
		return m_context;
	}

	// std::string const& getText() const;

	bool m_focusable;
	LuaReference luaReference;
	bool m_destroyed;

  protected:
	void setContext_(Context *context);

	bool readSpriteSet(BaseSpriteSet *&dest, std::string const &str);

	bool readSkin(BaseSpriteSet *&dest, std::string const &str);

	LuaReference m_callbacks[LuaCallbacksMax];

	/*
		//Transfers ownership
		Wnd* add_(Wnd* wnd)
		{
			if(m_context)
			{
				wnd->setContext_(m_context); //Inherit root
			}

			m_children.push_back(wnd);
			return wnd;
		}*/

	std::string m_text;
	Rect m_rect;
	std::list<Wnd *> m_children;
	Wnd *m_parent;
	Wnd *m_lastChildFocus;
	Context *m_context;
	BaseFont *m_font;

	std::string m_tagLabel;
	std::string m_className;
	std::string m_id;
	std::string m_state;
	std::string m_group;

	std::map<std::string, std::string> m_attributes;

	std::map<std::string, Wnd *> m_namedChildren;

	// Strong Lua references this window holds to keep its children alive for
	// the garbage collector. The C++ parent owns the child's lifetime, but Lua
	// cannot see that edge, so without an explicit reference a parented child
	// whose only other reference was transient would be collected and vanish
	// from the UI. These are released on removeChild() and in the destructor.
	std::map<Wnd *, LuaReference> m_ownedRefs;

	bool m_visible;
	bool m_active;

	Rect m_freeRect;
	int m_freeNextX;
	int m_freeNextY;

	// Formatting
	struct Formatting {
		enum Flags {
			HasLeft = 1 << 0,
			HasTop = 1 << 1,
			HasRight = 1 << 2,
			HasBottom = 1 << 3,
		};

		enum Blender { None = 0, Alpha, Add };

		Formatting()
			: width(50), height(50), spacing(5), padding(5), flags(0), alpha(255), rect(10, 10, 0, 0),
			  fontColor(255, 255, 255), blender(Alpha) {}

		struct Border {
			Border() : color(255, 255, 255) {}

			RGB color;
		} borders[4];

		struct Background {
			Background() : color(128, 128, 128), spriteSet(0), skin(0), invisible(false) {}

			~Background() {
				delete spriteSet;
				delete skin;
			}

			Background(Background const &) = delete;
			Background &operator=(Background const &) = delete;

			RGB color;
			BaseSpriteSet *spriteSet;
			BaseSpriteSet *skin;
			bool invisible;
		} background;

		int width;
		int height;

		int spacing;
		int padding;
		long flags;
		int alpha;

		Rect rect;
		RGB fontColor;
		Blender blender;

	} m_formatting;
};

} // namespace OmfgGUI

#endif // OMFG_GUI_WND_H
