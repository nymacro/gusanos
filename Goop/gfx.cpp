#include "gfx.h"
#include "gconsole.h"
#include "dedicated.h"

#ifndef DEDSERV
#include "2xsai.h"
// #include "blitters/blitters.h"
#include "blitters/colors.h"
#include "blitters/macros.h"
#include "game.h"
#include "mouse.h"
#include "sprite_set.h"
#include "xbrz.h"
#endif
#include <boost/assign/list_inserter.hpp>
using namespace boost::assign;

#include <string>
#include <algorithm>
#include <iostream>
#include <list>
#include <vector>
#include <stdexcept>
#include <cstring>

using namespace std;

Gfx gfx;

namespace {
bool m_initialized = false;

#ifndef DEDSERV
enum Filters {
	NEAREST = 0,  // Nearest
	LINEAR = 1,	  // Smooth
	PIXELART = 2, // Nearest with better scaling
	XBRZ2X = 3,
	XBRZ3X = 4,
	XBRZ4X = 5,
};
#ifndef SDL_SCALEMODE_PIXELART
#define SDL_SCALEMODE_PIXELART SDL_SCALEMODE_NEAREST
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

BITMAP *m_doubleResBuffer = 0;

std::vector<uint32_t> m_xbrzDst; // 320f x 240f xBRZ output staging
std::vector<uint32_t> m_xbrzSrc; // contiguous row staging (only if pitch != 320*4)
int m_xbrzFactor = 0;			 // 0 = disabled; 2/3/4 = xBRZ scale factor

int xbrzFactorOf(int filter) {
	switch (filter) {
		case XBRZ2X:
			return 2;
		case XBRZ3X:
			return 3;
		case XBRZ4X:
			return 4;
		default:
			return 0;
	}
}

string screenShot(const list<string> &args) {
	// TODO: Implement SDL3 screenshot
	return "SCREENSHOT NOT YET IMPLEMENTED IN SDL3";
}

void fullscreen_callback(int oldValue) {
	if (m_fullscreen == oldValue)
		return;

	if (gfx) {
		gfx.fullscreenChange();
	}
}

void doubleRes_callback(int oldValue) {
	if (m_doubleRes == oldValue)
		return;

	if (gfx) {
		gfx.doubleResChange();
	}
}

void filter_callback(const int oldValue) {
	if (!gfx || !gfx.screenTexture)
		return;

	// Entering or leaving an xBRZ mode needs a full rebuild (window size,
	// logical presentation, texture at the scaled resolution) via
	// doubleResChange(). EnumVariable invokes this with the *old* value;
	// m_filter already holds the new one.
	if (xbrzFactorOf(oldValue) > 0 || xbrzFactorOf(m_filter) > 0) {
		gfx.doubleResChange();
		return;
	}

	switch (m_filter) {
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
} // namespace

Gfx::Gfx()
#ifndef DEDSERV
	: buffer(NULL), window(NULL), renderer(NULL), screenTexture(NULL)
#endif
{
}

Gfx::~Gfx() {}

void Gfx::init() {
	if (!g_dedicated) {
		if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
			throw std::runtime_error("Couldn't initialize SDL3");
		}

		doubleResChange(); // This sets up window and renderer

		buffer = create_bitmap(320, 240);
		screen = buffer;

		// Detect CPU capabilities for SIMD blitter dispatch (AVX2/SSE2/NEON paths;
		// MMX/SSE are kept for the inert HAS_MMX/HAS_SSE markers).
		cpu_capabilities = 0;
		std::cout << "CPU Capabilities:";
		if (SDL_HasMMX()) {
			std::cout << " MMX";
			cpu_capabilities |= CPU_MMX;
		}
		if (SDL_HasSSE()) {
			std::cout << " SSE";
			cpu_capabilities |= CPU_SSE;
		}
		if (SDL_HasSSE2()) {
			std::cout << " SSE2";
			cpu_capabilities |= CPU_SSE2;
		}
		if (SDL_HasAVX2()) {
			std::cout << " AVX2";
			cpu_capabilities |= CPU_AVX2;
		}
		if (SDL_HasNEON()) {
			std::cout << " NEON";
			cpu_capabilities |= CPU_NEON;
		}
	}

	m_initialized = true;
}

void Gfx::shutDown() {
	if (!g_dedicated) {
		if (buffer) {
			destroy_bitmap(buffer);
			buffer = 0;
		}
		if (screenTexture) {
			SDL_DestroyTexture(screenTexture);
			screenTexture = 0;
		}
		if (renderer) {
			SDL_DestroyRenderer(renderer);
			renderer = 0;
		}
		if (cursorSpriteSet) {
			delete cursorSpriteSet;
			cursorSpriteSet = 0;
		}
		if (window) {
			SDL_DestroyWindow(window);
			window = 0;
		}
		SDL_Quit();
	}
}

void Gfx::registerInConsole() {
	if (!g_dedicated) {
		console.registerCommands()("SCREENSHOT", screenShot);

		console.registerVariables()("VID_FULLSCREEN", &m_fullscreen, 0,
									fullscreen_callback)("VID_DOUBLERES", &m_doubleRes, 0, doubleRes_callback)(
			"VID_VSYNC", &m_vsync, 1)("VID_CLEAR_BUFFER", &m_clearBuffer, 0)("VID_BITDEPTH", &m_bitdepth, 32)(
			"VID_DISTORTION_AA", &m_distortionAA, 1)("VID_HAX_WORMLIGHT", &m_haxWormLight, 1);

		{
			EnumVariable::MapType videoFilters;

			insert(videoFilters)("NEAREST", NEAREST)("LINEAR", LINEAR)("PIXELART", PIXELART)("XBRZ2X", XBRZ2X)(
				"XBRZ3X", XBRZ3X)("XBRZ4X", XBRZ4X);

			console.registerVariable(
				new EnumVariable("VID_FILTER", &m_filter, PIXELART, videoFilters, filter_callback));
		}
	}
}

void Gfx::loadResources() {
	if (!g_dedicated) {
		// Clean up a previously loaded cursor (e.g. when reloading a mod)
		if (cursorSpriteSet) {
			delete cursorSpriteSet;
			cursorSpriteSet = 0;
		}
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
			 it != cursorCandidates.end() && !cursorSpriteSet; ++it) {
			cursorSpriteSet = new SpriteSet();
			if (!cursorSpriteSet->load(*it)) {
				delete cursorSpriteSet;
				cursorSpriteSet = nullptr;
			}
		}
	}
}

#ifndef DEDSERV

void Gfx::updateScreen() {
	// Upload buffer to texture
	if (buffer && screenTexture) {
		if (m_xbrzFactor > 0) {
			const uint32_t *src = static_cast<uint32_t *>(buffer->pixels);
			if (buffer->sdl_surface->pitch != 320 * 4) {
				// Row copy into a contiguous staging buffer for xbrz::scale.
				for (int y = 0; y < 240; ++y)
					memcpy(&m_xbrzSrc[y * 320], buffer->line[y], 320 * 4);
				src = m_xbrzSrc.data();
			}
			xbrz::scale(m_xbrzFactor, src, m_xbrzDst.data(), 320, 240, xbrz::ColorFormat::argbUnbuffered);
			SDL_UpdateTexture(screenTexture, NULL, m_xbrzDst.data(), 320 * m_xbrzFactor * 4);
		} else {
			SDL_UpdateTexture(screenTexture, NULL, buffer->pixels, buffer->sdl_surface->pitch);
		}
	}

	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);

	if (screenTexture) {
		SDL_RenderTexture(renderer, screenTexture, NULL, NULL);
	}

	SDL_RenderPresent(renderer);

	if (m_clearBuffer)
		clear_bitmap(buffer);
}

int Gfx::getGraphicsDriver() {
	return 0;
}

void Gfx::fullscreenChange() {
	if (!window)
		return;
	SDL_SetWindowFullscreen(window, m_fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
}

void Gfx::doubleResChange() {
	int f = xbrzFactorOf(m_filter);

	if (f > 0) {
		// An active xBRZ filter forces the window to the xBRZ-native size;
		// VID_DOUBLERES only matters for the non-xBRZ filters below.
		m_vwidth = 320 * f;
		m_vheight = 240 * f;
	} else if (m_doubleRes) {
		m_vwidth = 640;
		m_vheight = 480;
	} else {
		m_vwidth = 320;
		m_vheight = 240;
	}

	// Note: NVidia GPU requires new texture even if just changing vsync
	SDL_SetRenderVSync(renderer, m_vsync);

	if (!window) {
		window = SDL_CreateWindow("Gusanos", m_vwidth, m_vheight, SDL_WINDOW_RESIZABLE);
		if (!window)
			throw std::runtime_error("Couldn't create SDL3 window");

		renderer = SDL_CreateRenderer(window, NULL);
		if (!renderer)
			throw std::runtime_error("Couldn't create SDL3 renderer");

		SDL_SetRenderVSync(renderer, m_vsync);
	} else {
		SDL_SetWindowSize(window, m_vwidth, m_vheight);
	}

	// Re-apply on every call so runtime filter changes take effect. The logical
	// space matches the texture size: 320x240 normally, 320f x 240f for xBRZ
	// (1:1 pixels); letterbox keeps the 4:3 aspect on window resize.
	if (f > 0)
		SDL_SetRenderLogicalPresentation(renderer, 320 * f, 240 * f, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	else
		SDL_SetRenderLogicalPresentation(renderer, 320, 240, SDL_LOGICAL_PRESENTATION_LETTERBOX);

	if (screenTexture)
		SDL_DestroyTexture(screenTexture);
	screenTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
									  f > 0 ? 320 * f : 320, f > 0 ? 240 * f : 240);

	// Apply filter setting to newly created texture. xBRZ output is already
	// smooth; NEAREST avoids double-smoothing on the final GPU scale.
	if (screenTexture) {
		SDL_ScaleMode mode = SDL_SCALEMODE_NEAREST;
		if (f == 0)
			mode = (m_filter == LINEAR) ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
		SDL_SetTextureScaleMode(screenTexture, mode);
	}

	// (Re)allocate xBRZ staging buffers when the factor changes.
	if (m_xbrzFactor != f) {
		m_xbrzFactor = f;
		if (f > 0) {
			m_xbrzDst.resize(320 * f * 240 * f);
			m_xbrzSrc.resize(320 * 240);
		} else {
			m_xbrzDst.clear();
			m_xbrzSrc.clear();
		}
	}
}

int Gfx::getScalingFactor() {
	return m_doubleRes ? 2 : 1;
}

#endif

BITMAP *Gfx::loadBitmap(const string &filename, RGB *palette, bool keepAlpha) {
	// Try original filename first, then with extensions
	BITMAP *bmp = load_bitmap(filename.c_str(), palette);
	if (!bmp) {
		bmp = load_bitmap((filename + ".png").c_str(), palette);
	}
	if (!bmp) {
		bmp = load_bitmap((filename + ".bmp").c_str(), palette);
	}
	return bmp;
}

bool Gfx::saveBitmap(const string &filename, BITMAP *image, RGB *palette) {
	return save_bitmap(filename.c_str(), image, palette) == 0;
}

Gfx::operator bool() {
	return m_initialized;
}
