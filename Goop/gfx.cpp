#include "gfx.h"
#include "text.h"
#include "gconsole.h"

#include <allegro.h>

#include <loadpng.h>

#include <string>
#include <algorithm>
#include <list>

using namespace std;

Gfx gfx;

string fullscreenCmd(const list<string> &args)
{
	gfx.toggleState();
	return "";
}

Gfx::Gfx()
{
	buffer = NULL;
}

Gfx::~Gfx()
{
}

void Gfx::init()
{	
	set_color_depth(16);
	set_gfx_mode(GFX_AUTODETECT, 320, 240, 0, 0);
	m_fullscreen = true;
	//set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
	
	if(set_display_switch_mode(SWITCH_BACKAMNESIA) == -1)
		set_display_switch_mode(SWITCH_BACKGROUND);
	
	loadpng_init();
	
	buffer = create_bitmap(screen->w,screen->h);
}

void Gfx::shutDown()
{
	if ( buffer ) destroy_bitmap(buffer);
}

void Gfx::registerInConsole()
{
	console.registerCommand("SCREENSHOT", screenShot);
	console.registerCommand("VID_TOGGLE_FULLSCREEN", fullscreenCmd);
	console.registerIntVariable("VID_VSYNC", &m_vsync, 1);
	console.registerIntVariable("VID_CLEAR_BUFFER", &m_clearBuffer, 1);
}

void Gfx::toggleState()
{
	set_color_depth(16);
	
	if ( !m_fullscreen )
	{
		set_gfx_mode(GFX_AUTODETECT, 320, 240, 0, 0);
		m_fullscreen = true;
	}else
	{
		set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
		m_fullscreen = false;
	}
	
	if(set_display_switch_mode(SWITCH_BACKAMNESIA) == -1)
		set_display_switch_mode(SWITCH_BACKGROUND);
}

BITMAP* Gfx::loadBitmap( const string& filename, RGB* palette )
{
	BITMAP* returnValue = NULL;
	
	if ( exists( filename.c_str() ) )
	{
		string extension = filename.substr(filename.length() - 3);
		transform(extension.begin(), extension.end(), extension.begin(), (int(*)(int)) toupper);
		
		if ( extension == "PNG" )
		{
			returnValue = load_png(filename.c_str(), palette);
		}
		else
		{
			returnValue = load_bitmap(filename.c_str(), palette);
		}
	}else
	{
		string tmp = filename;
		tmp += ".png";
		if ( exists( tmp.c_str() ) )
		{
			returnValue = load_png( tmp.c_str() , palette );
		}
		else
		{
			tmp = filename;
			tmp += ".bmp";
			if ( exists( tmp.c_str() ))
			{
				returnValue = load_bmp( tmp.c_str() , palette );
			}
		}
	}
	
	return returnValue;
}

bool Gfx::saveBitmap( const string &filename,BITMAP* image, RGB* palette )
{
	bool returnValue = false;
	
	string extension = filename.substr(filename.length() - 3);
	transform(extension.begin(), extension.end(), extension.begin(), (int(*)(int)) toupper);
	
	if ( extension == "PNG" )
	{
		if ( !save_png(filename.c_str(), image, palette) ) returnValue = true;
	}
	else
	{
		if ( !save_bitmap(filename.c_str(), image, palette) ) returnValue = true;
	}
	
	return returnValue;
}

string screenShot(const list<string> &args)
{
	int nameIndex = 0;
	string filename = "screenshots/ss" + cast<string>(nameIndex) + ".png";
	while( exists( filename.c_str() ) )
	{
		nameIndex += 1;
		filename = "screenshots/ss" + cast<string>(nameIndex) + ".png";
	}
	
	BITMAP * tmpbitmap = create_bitmap(screen->w,screen->h);
	blit(screen,tmpbitmap,0,0,0,0,320,240);
	bool success = gfx.saveBitmap( filename.c_str(),tmpbitmap,0);
	destroy_bitmap(tmpbitmap);
	
	if ( success )
		return "SCREENSHOT SAVED AS: SS" + cast<string>(nameIndex) + ".png";
	else 
		return "UNABLE TO SAVE SCREENSHOT";
}

