#include "gfx.h"
#include "gconsole.h"

#ifndef DEDSERV
#include "2xsai.h"
//#include "blitters/blitters.h"
#include "blitters/colors.h"
#include "blitters/macros.h"
#include "game.h"
#include "mouse.h"
#include "sprite_set.h"
#endif
#include <boost/assign/list_inserter.hpp>
using namespace boost::assign;

#include <string>
#include <algorithm>
#include <iostream>
#include <list>
#include <vector>
#include <stdexcept>

using namespace std;

Gfx gfx;

namespace
{
	bool m_initialized = false;

#ifndef DEDSERV
	enum Filters
	{
		NEAREST = 0, // Nearest
		LINEAR = 1,  // Smooth
		PIXELART = 2 // Nearest with better scaling
	};
#ifndef SDL_SCALEMODE_PIXELART
#  define SDL_SCALEMODE_PIXELART SDL_SCALEMODE_NEAREST
#endif

	int m_fullscreen = 0;
	int m_doubleRes = 1;
	int m_vwidth = 640;
	int m_vheight = 480;
	int m_vsync = 1;
	int m_clearBuffer = 0;
	int m_filter = PIXELART;
	int m_driver = 0;
	int m_bitdepth = 32;

	BITMAP* m_doubleResBuffer = 0;

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

	void filter_callback( const int newValue )
	{
		if (!gfx || !gfx.screenTexture) return;

		switch (newValue) {
		case NEAREST:
			SDL_SetTextureScaleMode(gfx.screenTexture, SDL_SCALEMODE_NEAREST);
			break;
		case LINEAR:
			SDL_SetTextureScaleMode(gfx.screenTexture, SDL_SCALEMODE_LINEAR);
			break;
		case PIXELART:
			SDL_SetTextureScaleMode(gfx.screenTexture, SDL_SCALEMODE_PIXELART);
			break;
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
	if (cursorSpriteSet) { delete cursorSpriteSet; cursorSpriteSet = 0; }
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
			("NEAREST", NEAREST)
			("LINEAR", LINEAR)
			("PIXELART", PIXELART)
			;

		console.registerVariable(new EnumVariable("VID_FILTER", &m_filter, PIXELART, videoFilters, filter_callback));
	}
#endif
}

void Gfx::loadResources()
{
#ifndef DEDSERV
	// Clean up a previously loaded cursor (e.g. when reloading a mod)
	if (cursorSpriteSet) { delete cursorSpriteSet; cursorSpriteSet = 0; }
	cursorFrame = 0;

	// Build a list of candidate paths for the cursor sprite set. The active mod
	// directory is searched first, then the default mod directory, and finally
	// the current working directory as a last resort.
	std::vector<fs::path> cursorCandidates;
	cursorCandidates.push_back(game.getModPath() / "sprites" / "cursor.png");
	cursorCandidates.push_back(game.getModPath() / "sprites" / "cursor.bmp");
	cursorCandidates.push_back(game.getDefaultPath() / "sprites" / "cursor.png");
	cursorCandidates.push_back(game.getDefaultPath() / "sprites" / "cursor.bmp");
	cursorCandidates.push_back(fs::path("cursor.png"));
	cursorCandidates.push_back(fs::path("cursor.bmp"));

	for (std::vector<fs::path>::const_iterator it = cursorCandidates.begin();
	     it != cursorCandidates.end() && !cursorSpriteSet; ++it)
	{
		cursorSpriteSet = new SpriteSet();
		if (!cursorSpriteSet->load(*it)) {
			delete cursorSpriteSet;
			cursorSpriteSet = nullptr;
		}
	}
#endif
}

#ifndef DEDSERV

void Gfx::updateScreen()
{
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

	// Note: NVidia GPU requires new texture even if just changing vsync
	// Keep render logical presentation in place
	SDL_SetRenderVSync(renderer, m_vsync);

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
	if (screenTexture) {
		SDL_ScaleMode mode = (m_filter == LINEAR) ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
		SDL_SetTextureScaleMode(screenTexture, mode);
	}
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
