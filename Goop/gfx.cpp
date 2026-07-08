#include "gfx.h"
#include "gconsole.h"

#ifndef DEDSERV
#include "2xsai.h"
//#include "blitters/blitters.h"
#include "blitters/colors.h"
#include "blitters/macros.h"
#include "mouse.h"
#include "sprite_set.h"
#include "sprite.h"
#endif
#include <boost/bind.hpp>
#include <boost/assign/list_inserter.hpp>
using namespace boost::assign;

#include <string>
#include <algorithm>
#include <iostream>
#include <list>
#include <stdexcept>

using namespace std;

Gfx gfx;

namespace
{
	bool m_initialized = false;

#ifndef DEDSERV
	enum Filters
	{
		NO_FILTER,
		NO_FILTER2,
		SCANLINES,
		SCANLINES2,
		BILINEAR,
		SUPER2XSAI,
		SUPEREAGLE,
		PIXELATE
	};
	
	int m_fullscreen = 0;
	int m_doubleRes = 1;
	int m_vwidth = 640;
	int m_vheight = 480;
	int m_vsync = 1;
	int m_clearBuffer = 0;
	int m_filter = NO_FILTER;
	int m_driver = 0;
	int m_bitdepth = 32;

	BITMAP* m_doubleResBuffer = 0;
	SpriteSet* mouseCursor = 0;
	
	string screenShot(const list<string> &args)
	{
        // TODO: Implement SDL3 screenshot
		return "SCREENSHOT NOT YET IMPLEMENTED IN SDL3";
	}
	
	void fullscreen_callback( int oldValue )
	{
		if(m_fullscreen == oldValue)
			return;
			
		if(gfx)
		{
			gfx.fullscreenChange();
		}
	}
	
	void doubleRes_callback( int oldValue )
	{
		if(m_doubleRes == oldValue)
			return;
			
		if(gfx)
		{
			gfx.doubleResChange();
		}
	}
	
	void filter_callback(int /*newValue*/)
	{
		if (gfx && gfx.screenTexture) {
			SDL_ScaleMode mode = (m_filter == NO_FILTER || m_filter == NO_FILTER2 || m_filter == PIXELATE)
				? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR;
			SDL_SetTextureScaleMode(gfx.screenTexture, mode);
		}
	}
#endif
}

Gfx::Gfx()
#ifndef DEDSERV
: buffer(NULL), window(NULL), renderer(NULL), screenTexture(NULL)
#endif
{
}

Gfx::~Gfx()
{
}

void Gfx::init()
{
#ifndef DEDSERV
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        throw std::runtime_error("Couldn't initialize SDL3");
    }


	doubleResChange(); // This sets up window and renderer

	Init_2xSaI(32); 
	
	buffer = create_bitmap(320, 240);
	screen = buffer;

	// Detect CPU capabilities for SIMD blitter dispatch (MMX/SSE paths)
	cpu_capabilities = 0;
	if (SDL_HasMMX()) cpu_capabilities |= CPU_MMX;
	if (SDL_HasSSE()) cpu_capabilities |= CPU_SSE;
#endif

	m_initialized = true; 
}

void Gfx::shutDown()
{
#ifndef DEDSERV
	if (buffer) { destroy_bitmap(buffer); buffer = 0; }
    if (screenTexture) { SDL_DestroyTexture(screenTexture); screenTexture = 0; }
    if (renderer) { SDL_DestroyRenderer(renderer); renderer = 0; }
    if (window) { SDL_DestroyWindow(window); window = 0; }
    SDL_Quit();
#endif
}

void Gfx::registerInConsole()
{
#ifndef DEDSERV
	console.registerCommands()
		("SCREENSHOT", screenShot)
	;
	
	console.registerVariables()
		("VID_FULLSCREEN", &m_fullscreen, 0, fullscreen_callback)
		("VID_DOUBLERES", &m_doubleRes, 0, doubleRes_callback)
		("VID_VSYNC", &m_vsync, 1)
		("VID_CLEAR_BUFFER", &m_clearBuffer, 0)
		("VID_BITDEPTH", &m_bitdepth, 32)
		("VID_DISTORTION_AA", &m_distortionAA, 1)
		("VID_HAX_WORMLIGHT", &m_haxWormLight, 1)
	;
	
	{
		EnumVariable::MapType videoFilters;

		insert(videoFilters) 
			("NOFILTER", NO_FILTER)
			("NOFILTER2", NO_FILTER2)
			("SCANLINES", SCANLINES)
			("SCANLINES2", SCANLINES2)
			("BILINEAR", BILINEAR)
			("SUPER2XSAI",SUPER2XSAI)
			("SUPEREAGLE", SUPEREAGLE)
			("PIXELATE", PIXELATE)
		;

		console.registerVariable(new EnumVariable("VID_FILTER", &m_filter, NO_FILTER, videoFilters, filter_callback));
	}
#endif
}

void Gfx::loadResources()
{
#ifndef DEDSERV
	mouseCursor = spriteList.load("cursor");
#endif
}

#ifndef DEDSERV

void Gfx::updateScreen()
{
	if(mouseCursor)
	{
		int x = mouseHandler.getX();
		int y = mouseHandler.getY();
		mouseCursor->getSprite()->draw(buffer, x, y);
	}
	
    // Apply current filter so runtime VID_FILTER changes take effect
    if (screenTexture) {
        SDL_ScaleMode mode = (m_filter == NO_FILTER || m_filter == NO_FILTER2 || m_filter == PIXELATE)
            ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR;
        SDL_SetTextureScaleMode(screenTexture, mode);
    }

    // Upload buffer to texture
    if (buffer && screenTexture) {
        SDL_UpdateTexture(screenTexture, NULL, buffer->pixels, buffer->sdl_surface->pitch);
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    if (screenTexture) {
        SDL_RenderTexture(renderer, screenTexture, NULL, NULL);
    }

    SDL_RenderPresent(renderer);

	if ( m_clearBuffer ) clear_bitmap(buffer);
}

int Gfx::getGraphicsDriver()
{
	return 0;
}

void Gfx::fullscreenChange()
{
    if (!window) return;
    SDL_SetWindowFullscreen(window, m_fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
}

void Gfx::doubleResChange()
{
	if ( m_doubleRes )
	{
		m_vwidth = 640;
		m_vheight = 480;
	} else {
		m_vwidth = 320;
		m_vheight = 240;
	}

    if (!window) {
        window = SDL_CreateWindow("Gusanos", m_vwidth, m_vheight, SDL_WINDOW_RESIZABLE);
        if (!window) throw std::runtime_error("Couldn't create SDL3 window");
        
        renderer = SDL_CreateRenderer(window, NULL);
        if (!renderer) throw std::runtime_error("Couldn't create SDL3 renderer");

        SDL_SetRenderLogicalPresentation(renderer, 320, 240, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_SetRenderVSync(renderer, m_vsync);
    } else {
        SDL_SetWindowSize(window, m_vwidth, m_vheight);
    }

    if (screenTexture) SDL_DestroyTexture(screenTexture);
    screenTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 240);

    // Apply filter setting to newly created texture
    SDL_ScaleMode mode = (m_filter == NO_FILTER || m_filter == NO_FILTER2 || m_filter == PIXELATE)
        ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR;
    SDL_SetTextureScaleMode(screenTexture, mode);
}

int Gfx::getScalingFactor()
{
	return m_doubleRes ? 2 : 1;
}

#endif

BITMAP* Gfx::loadBitmap( const string& filename, RGB* palette, bool keepAlpha )
{
    // Try original filename first, then with extensions
    BITMAP* bmp = load_bitmap(filename.c_str(), palette);
    if (!bmp) {
        bmp = load_bitmap((filename + ".png").c_str(), palette);
    }
    if (!bmp) {
        bmp = load_bitmap((filename + ".bmp").c_str(), palette);
    }
    return bmp;
}

bool Gfx::saveBitmap( const string &filename, BITMAP* image, RGB* palette )
{
	return save_bitmap(filename.c_str(), image, palette) == 0;
}

Gfx::operator bool()
{
	return m_initialized;
}
