#include "context.h"

#include "renderer.h"
#include "wnd.h"
#include <iostream>

namespace OmfgGUI
{

Context::GSSpropertyMap Context::GSSpropertyMapStandard;
Context::GSSstate Context::GSSstate::standard;
Context::GSSid Context::GSSid::standard;
Context::GSSclass Context::GSSclass::standard;
	
Context::~Context()
{
	delete m_rootWnd;
}

void Context::setRoot_(Wnd* wnd)
{
	m_rootWnd = wnd;
	if(m_rootWnd)
		m_rootWnd->setContext_(this);
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

void Context::render(Renderer* renderer)
{
	if(m_rootWnd)
	{
		Rect oldClip(renderer->getClip());
		m_rootWnd->doRender(renderer, renderer->getViewportRect());
		renderer->setClip(oldClip);
	}	
}

void Context::process()
{
	if(m_rootWnd)
		m_rootWnd->doProcess();
}

void Context::setFocus(Wnd* aWnd)
{
	Wnd* oldFocus = m_keyboardFocusWnd;
	m_keyboardFocusWnd = aWnd;
	if(oldFocus)
		oldFocus->applyGSS(m_gss);
	if(m_keyboardFocusWnd)
		m_keyboardFocusWnd->applyGSS(m_gss);
}

void Context::registerWindow(Wnd* wnd)
{
	registerNamedWindow(wnd->m_id, wnd);
}

void Context::registerNamedWindow(std::string const& id, Wnd* wnd)
{
	if(id.size() == 0)
		return;

	std::map<std::string, Wnd*>::iterator i = m_namedWindows.find(id);
	if(i != m_namedWindows.end())
	{
		delete i->second; //Delete conflicting window
	}
	m_namedWindows[id] = wnd;
}

void Context::deregisterWindow(Wnd* wnd)
{
	if(getFocus() == wnd)
		setFocus(0);
	if(getMouseCapture() == wnd)
		captureMouse(0);
	if(getRoot() == wnd)
		setRoot((Wnd *)0);
	deregisterNamedWindow(wnd->m_id);
}


}
