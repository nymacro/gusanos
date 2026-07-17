#include "context.h"
#include "wnd.h"
#include "luaapi/context.h"
#include "renderer.h"
#include "wnd.h"
#include <iostream>
using std::cerr;
using std::endl;

namespace OmfgGUI
{

/*
Context::GSSpropertyMap Context::GSSpropertyMapStandard;
Context::GSSstate Context::GSSstate::standard;
Context::GSSid Context::GSSid::standard;
Context::GSSclass Context::GSSclass::standard;
*/

void Context::destroy()
{
	// Clear keyboard focus so derived classes (GContext) can release any
	// focus-driven binding lock. Otherwise a window that was focused at
	// teardown time keeps the lock held forever, swallowing keyboard input
	// in the next session.
	if(m_keyboardFocusWnd)
		setFocus(0);

	// Release the strong Lua reference to the root before deleting it, so the
	// destructor (and the final GC of the userdata) doesn't try to touch a
	// dangling registry slot.
	if(m_rootRef)
	{
		lua.destroyReference(m_rootRef);
		m_rootRef.reset();
	}

	delete m_rootWnd; m_rootWnd = 0;
}

void Context::setRoot_(Wnd* wnd)
{
	// Drop any previous root reference.
	if(m_rootRef)
	{
		lua.destroyReference(m_rootRef);
		m_rootRef.reset();
	}

	m_rootWnd = wnd;
	if(m_rootWnd)
	{
		m_rootWnd->setContext_(this);
		// Keep the root (and therefore the whole tree) alive for the GC by
		// holding a strong Lua reference. The root's only other reference is
		// its own weak self-reference.
		lua.pushWeakReference(m_rootWnd->luaReference);
		m_rootRef = lua.createReference();
	}
}

void Context::updateGSS()
{
	if(m_rootWnd)
		m_rootWnd->doUpdateGSS();
}

//Sends a cursor relocation event
void Context::mouseMove(ulong newX, ulong newY)
{
	m_cursorX = newX;
	m_cursorY = newY;
	
	if(m_mouseCaptureWnd)
		m_mouseCaptureWnd->mouseMove(newX, newY);
	else if(m_rootWnd)
		m_rootWnd->doMouseMove(newX, newY);
}

//Sends a mouse button down event
void Context::mouseDown(ulong newX, ulong newY, MouseKey::type button)
{
	if(m_mouseCaptureWnd)
		m_mouseCaptureWnd->mouseDown(newX, newY, button);
	else if(m_rootWnd)
		m_rootWnd->doMouseDown(newX, newY, button);
}

//Sends a mouse button up event
void Context::mouseUp(ulong newX, ulong newY, MouseKey::type button)
{
	if(m_mouseCaptureWnd)
		m_mouseCaptureWnd->mouseUp(newX, newY, button);
	else if(m_rootWnd)
		m_rootWnd->doMouseUp(newX, newY, button);
}

//Sends a printable character
void Context::charPressed(char c)
{
}

//Sends a keydown event
void Context::keyDown(KeyType k, bool shift, bool alt, bool ctrl)
{
}

//Sends a keyup event
void Context::keyUp(KeyType k, bool shift, bool alt, bool ctrl)
{
}

void Context::render()
{
	if(m_rootWnd)
	{
		Rect oldClip(renderer()->getClip());
		m_rootWnd->doRender(renderer()->getViewportRect());
		renderer()->setClip(oldClip);
		renderer()->resetBlending();
	}	
}

void Context::process()
{
	if(m_rootWnd)
		m_rootWnd->doProcess();
}

void Context::mouseDown(int x, int y, MouseKey::type button)
{
	m_rootWnd->doMouseDown(x, y, button);
}

void Context::mouseUp(int x, int y, MouseKey::type button)
{
	if(m_mouseFocusWnd)
	{
		m_mouseFocusWnd->mouseUp(x, y, button);
		m_mouseFocusWnd = 0;
	}
}

void Context::mouseMove(int x, int y)
{
	if(m_mouseFocusWnd)
		m_mouseFocusWnd->mouseMove(x, y);
	else
		m_rootWnd->doMouseMove(x, y);
}

void Context::mouseScroll(int x, int y, int offs)
{
	m_rootWnd->doMouseScroll(x, y, offs);
}

void Context::setFocus(Wnd* aWnd)
{
	if(aWnd)
	{
		/* TODO: Fix
		if(aWnd->m_parent)
		{
			Wnd* parent = aWnd->m_parent;
			std::list<Wnd*>::iterator i = std::find(parent->m_children.begin(), parent->m_children.end(), aWnd);
			assert(i != parent->m_children.end());
			parent->m_children.splice(parent->m_children.end(), parent->m_children, i);
		}
		*/
		
		while(aWnd->m_lastChildFocus)
		{
			aWnd = aWnd->m_lastChildFocus;
		}
	}
	
	if(aWnd == m_keyboardFocusWnd)
		return;
	
	Wnd* oldFocus = m_keyboardFocusWnd;
	m_keyboardFocusWnd = aWnd;
	if(oldFocus)
		oldFocus->applyGSS(m_gss);
	if(m_keyboardFocusWnd)
		m_keyboardFocusWnd->applyGSS(m_gss);
	
	if(aWnd)
	{
		aWnd->m_lastChildFocus = 0;
		Wnd* lastChild = aWnd;
		for(Wnd* p = aWnd->m_parent; p; p = p->m_parent)
		{
			p->m_lastChildFocus = lastChild;
			lastChild = p;
		}
	}
}

void Context::setActive(Wnd* wnd)
{
	if(m_activeWnd)
		m_activeWnd->doSetActivation(false);
	m_activeWnd = wnd;
}

void Context::registerWindow(Wnd* wnd)
{
	registerNamedWindow(wnd->m_id, wnd);
}

void Context::registerNamedWindow(std::string const& id, Wnd* wnd)
{
	if(id.empty())
		return;

	m_namedWindows.insert(std::make_pair(id, wnd));
	
	/*
	std::map<std::string, Wnd*>::iterator i = m_namedWindows.find(id);
	if(i != m_namedWindows.end())
	{
		//cerr << "Deleting conflicting window (named '" << id << "'): " << i->second << endl;
		//delete i->second; //Delete conflicting window
	}
	m_namedWindows[id] = wnd;
	*/
}

void Context::deregisterWindow(Wnd* wnd)
{
	if(getFocus() == wnd)
		setFocus(0);
	if(getMouseCapture() == wnd)
		captureMouse(0);
	if(getRoot() == wnd)
		setRoot((Wnd *)0);
	if(m_activeWnd == wnd)
		m_activeWnd = 0;
	if(m_mouseFocusWnd == wnd)
		m_mouseFocusWnd = 0;
	//deregisterNamedWindow(wnd->m_id);
	for (auto it = m_namedWindows.begin(); it != m_namedWindows.end(); )
	{
		if(it->second == wnd)
			it = m_namedWindows.erase(it);
		else
			++it;
	}
}

int Context::GSSselector::matchesWindow(Wnd* w) const
{
	int matchLevel = 1;
	
	for (const auto& c : cond)
	{
		switch(c.type)
		{
			case Condition::Tag:
				if(w->m_tagLabel != c.v)
					return 0;
				matchLevel += 1;
			break;

			case Condition::Class:
				if(w->m_className != c.v)
					return 0;
				matchLevel += 2;
			break;
			
			case Condition::ID:
				if(w->m_id != c.v)
					return 0;
				matchLevel += 4;
			break;
			
			case Condition::State:
				if(w->m_state != c.v)
					return 0;
				matchLevel += 8;
			break;
			
			case Condition::Group:
				if(w->m_group != c.v)
					return 0;
				matchLevel += 16;
			break;
		}
	}
		
	return matchLevel;
}


}
