#include "menu.h"
#include "keyboard.h"
#include "gfx.h"
#include "blitters/blitters.h"
#include "font.h"
#include "sprite_set.h"
#include "sprite.h"
#include "script.h"
#include "lua/context.h"
#include "omfggui_windows.h"
#include "gconsole.h"
#include <boost/bind.hpp>
#include <iostream>
#include <list>
#include <string>
#include <fstream>
#include <sstream>
using std::cout;
using std::endl;

ResourceLocator<XMLFile, false, false> xmlLocator;
ResourceLocator<GSSFile, false, false> gssLocator;

namespace OmfgGUI
{

GContext menu;

std::string cmdLoadXML(std::list<std::string> const& args)
{
	if(args.size() > 0)
	{
		std::string ret;

		std::list<std::string>::const_iterator i = args.begin();
		std::string path;
		Wnd* loadTo = menu.getRoot();
		
		path = *i++;
		if(i != args.end())
			loadTo = menu.findNamedWindow(*i++);
			
		if(!loadTo)
			return "DESTINATION WINDOW NOT FOUND";
			
		XMLFile f;
		if(xmlLocator.load(&f, path))
		{
			menu.buildFromXML(f.f, loadTo);
			return "";
		}
		else
			return "ERROR LOADING \"" + path + '"';
	}
	
	return "GUI_LOADXML <FILE> [<DEST>] : LOADS AN XML FILE INTO A WINDOW (ROOT BY DEFAULT)";
}

std::string cmdLoadGSS(std::list<std::string> const& args)
{
	if(args.size() > 0)
	{
		std::string ret;
				
		std::list<std::string>::const_iterator i = args.begin();
		std::string path = *i++;
		
		bool passive = false;
		for(; i != args.end(); ++i)
		{
			if(*i == "passive")
				passive = true;
		}
		

		GSSFile f;
		if(gssLocator.load(&f, path))
		{
			menu.loadGSS(f.f);
			if(!passive) menu.updateGSS();
			return "";
		}
		else
			return "ERROR LOADING \"" + path + '"';
	}
	
	return "GUI_LOADGSS <FILE> : LOADS A GSS FILE";
}

std::string cmdGSS(std::list<std::string> const& args)
{
	if(args.size() > 0)
	{
		std::string ret;
				
		std::list<std::string>::const_iterator i = args.begin();
		std::string gss = *i++;
		
		bool passive = false;
		for(; i != args.end(); ++i)
		{
			if(*i == "passive")
				passive = true;
		}
		

		std::istringstream f(gss);

		menu.loadGSS(f);
		if(!passive) menu.updateGSS();
		
		return "";
	}
	
	return "GUI_GSS <GSS> : LOADS INLINED GSS";
}

std::string cmdFocus(std::list<std::string> const& args)
{
	if(args.size() > 0)
	{
		std::string ret;
				
		std::list<std::string>::const_iterator i = args.begin();
		std::string const& name = *i++;

		Wnd* newFocus = menu.findNamedWindow(name);

		if(newFocus)
		{
			menu.setFocus(newFocus);
			return "";
		}
		else
			return "NO WINDOW WITH ID \"" + name + '"';
	}
	
	return "GUI_FOCUS <WINDOW ID> : FOCUSES A WINDOW";
}

int GusanosSpriteSet::getFrameCount() const
{
	return spriteSet->getFramesWidth();
}

ulong GusanosSpriteSet::getFrameWidth(int frame, int angle) const
{
	return spriteSet->getSprite(frame)->getWidth();
}

ulong GusanosSpriteSet::getFrameHeight(int frame, int angle) const
{
	return spriteSet->getSprite(frame)->getHeight();
}

void GContext::init()
{
	keyHandler.keyDown.connect(boost::bind(&GContext::eventKeyDown, this, _1));
	keyHandler.keyUp.connect(boost::bind(&GContext::eventKeyUp, this, _1));
	keyHandler.printableChar.connect(boost::bind(&GContext::eventPrintableChar, this, _1, _2));
	
	console.registerCommands()
		("GUI_LOADXML", cmdLoadXML)
		("GUI_LOADGSS", cmdLoadGSS)
		("GUI_GSS", cmdGSS)
		("GUI_FOCUS", cmdFocus)
	;
}

bool GContext::eventKeyDown(int k)
{
	Wnd* focus = getFocus();
	if(focus && focus->keyDown(k) && focus->isVisibile())
	{
		Wnd* parent = focus->getParent();
		if(parent && parent->isVisibile())
		{
			switch(k)
			{
				case KEY_UP:
				{
					if(Wnd* next = parent->findClosestChild(
						focus,
						Wnd::Up))
					{
						setFocus(next);
					}
						
				}
				break;
				
				case KEY_DOWN:
				{
					if(Wnd* next = parent->findClosestChild(
						focus,
						Wnd::Down))
					{
						setFocus(next);
					}
						
				}
				break;
				
				case KEY_LEFT:
				{
					if(Wnd* next = parent->findClosestChild(
						focus,
						Wnd::Left))
					{
						setFocus(next);
					}
						
				}
				break;
				
				case KEY_RIGHT:
				{
					if(Wnd* next = parent->findClosestChild(
						focus,
						Wnd::Right))
					{
						setFocus(next);
					}
						
				}
				break;
				
				
			}
		}
		
		
	}

	return true;
}

bool GContext::eventKeyUp(int k)
{
	Wnd* focus = getFocus();
	if(focus && focus->keyUp(k) && focus->isVisibile())
	{
		// Do sth?
		switch(k)
		{
			case KEY_ENTER:
			{
				std::string cmd;
				if(focus && focus->getAttrib("command", cmd))
				{
					std::string::size_type p = cmd.find('.');
					if(p != std::string::npos)
					{
						Script* s = scriptLocator.load(cmd.substr(0, p));
						if(s)
						{
							s->pushFunction(cmd.substr(p + 1, cmd.size() - p - 1));
							s->lua->call();
						}
					}
				}
			}
			break;
		}
	}
	
	return true;
}

bool GContext::eventPrintableChar(char c, int k)
{
	Wnd* focus = getFocus();
	if(focus && focus->charPressed(c, k))
	{
		// Do sth?
	}
	
	return true;
}

void GContext::clear()
{
	cerr << "Deleting root window " << m_rootWnd << " ...";
	delete m_rootWnd;
	m_rootWnd = 0;
	cerr << "done" << endl;
	
	std::istringstream rootGSS(
		"#root { background: #000080 ; left: 0 ; top: 0 ; bottom : -1 ; right: -1; padding: 29; spacing: 20 }"
		"edit { background: #FFFFFF ; border: #666666; border-bottom: #A0A0A0 ; border-right: #A0A0A0 ;"
		" width: 100 ; height: 15 ; font-family: big }");
		
	std::istringstream rootXML("<window id=\"root\" />");
	
	cerr << "Begins loading root GSS" << endl;
	loadGSS(rootGSS);
	buildFromXML(rootXML, 0);
}

BaseFont* GContext::loadFont(std::string const& name)
{
	Font* f = fontLocator.load(name);
	if(!f)
		return 0;
	return new GusanosFont(f);
}

BaseSpriteSet* GContext::loadSpriteSet(std::string const& name)
{
	SpriteSet *s = spriteList.load(name);
	if(!s)
		return 0;
	return new GusanosSpriteSet(s);
}

int allegroColor(RGB const& rgb)
{
	return makecol(rgb.r, rgb.g, rgb.b);
}

// Draws a box
void AllegroRenderer::drawBox(
	Rect const& rect,
	RGB const& color,
	RGB const& borderLeftColor,
	RGB const& borderTopColor,
	RGB const& borderRightColor,
	RGB const& borderBottomColor)
{
	blitter.rectfill(gfx.buffer, rect.x1, rect.y1, rect.x2, rect.y2, allegroColor(color));
	//rectfill(gfx.buffer, rect.x1, rect.y1, rect.x2, rect.y2, allegroColor(color));
	hline(gfx.buffer, rect.x1, rect.y2, rect.x2, allegroColor(borderBottomColor));
	vline(gfx.buffer, rect.x2, rect.y1, rect.y2, allegroColor(borderRightColor));
	vline(gfx.buffer, rect.x1, rect.y1, rect.y2, allegroColor(borderLeftColor));
	hline(gfx.buffer, rect.x1, rect.y1, rect.x2, allegroColor(borderTopColor));
}

// Draws a box
void AllegroRenderer::drawBox(
	Rect const& rect,
	RGB const& color)
{
	blitter.rectfill(gfx.buffer, rect.x1, rect.y1, rect.x2, rect.y2, allegroColor(color));
	
	//rectfill(gfx.buffer, rect.x1, rect.y1, rect.x2, rect.y2, allegroColor(color));
}

void AllegroRenderer::drawVLine(ulong x, ulong y1, ulong y2, RGB const& color)
{
	vline(gfx.buffer, x, y1, y2, allegroColor(color));
}

// Draws text
void AllegroRenderer::drawText(BaseFont const& font, std::string const& str, ulong flags, ulong x, ulong y, RGB const& aColor)
{
	const int spacing = 0;

	if(GusanosFont const* f = dynamic_cast<GusanosFont const*>(&font))
	{
		if(flags & (BaseFont::CenterH | BaseFont::CenterV))
		{
			std::pair<int, int> dim = f->font->getDimensions(str);
			
			if(flags & BaseFont::CenterH)
				x -= dim.first / 2;
			if(flags & BaseFont::CenterV)
				y -= dim.second / 2;
		}
		
		f->font->draw(gfx.buffer, str, x, y, spacing, aColor.r, aColor.g, aColor.b);
	}
}

std::pair<int, int> AllegroRenderer::getTextDimensions(BaseFont const& font, std::string::const_iterator b, std::string::const_iterator e)
{
	if(GusanosFont const* f = dynamic_cast<GusanosFont const*>(&font))
	{
		return f->font->getDimensions(b, e);
	}
	return std::make_pair(0, 0);
}

void AllegroRenderer::drawSprite(BaseSpriteSet const& spriteSet, int frame, ulong x, ulong y)
{
	if(GusanosSpriteSet const* s = dynamic_cast<GusanosSpriteSet const*>(&spriteSet))
	{
		s->spriteSet->getSprite(frame)->draw(gfx.buffer, x, y);
	}
}

void AllegroRenderer::drawSprite(BaseSpriteSet const& spriteSet, int frame, ulong x, ulong y, ulong left, ulong top, ulong bottom, ulong right)
{
	if(GusanosSpriteSet const* s = dynamic_cast<GusanosSpriteSet const*>(&spriteSet))
	{
		s->spriteSet->getSprite(frame)->drawCut(gfx.buffer, x, y, 0, left, top, bottom, right);
	}
}

void AllegroRenderer::setClip(Rect const& rect)
{
	set_clip_rect(gfx.buffer, rect.x1, rect.y1, rect.x2, rect.y2);
}

Rect const& AllegroRenderer::getClip()
{
	get_clip_rect(gfx.buffer, &clipRect.x1, &clipRect.y1, &clipRect.x2, &clipRect.y2);
	return clipRect;
}

Rect const& AllegroRenderer::getViewportRect()
{
	screenRect = Rect(0, 0, SCREEN_W - 1, SCREEN_H - 1);
	return screenRect;
}

void AllegroRenderer::setBlending(int alpha_)
{
	//gfx.setBlender(ALPHA, alpha_);
	//alpha = alpha_;
	blitter.set(BlitterContext::Alpha, alpha_);
}

void AllegroRenderer::resetBlending()
{
	//solid_mode();
	//alpha = 255;
	blitter.set(BlitterContext::none());
}

}
