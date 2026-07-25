#ifndef DEDSERV

#include "keyboard.h"
#include "keys.h"
#include "console.h"
#include "gusanos.h"
#include "gfx.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <list>
#include <iostream>
#include <cctype>

using namespace std;

// SDL3 scancode -> Gusanos KEY_* mapping table
// Indexed by SDL scancode value, returns the Gusanos virtual key code.
static int sdlToGusKey[SDL_SCANCODE_COUNT] = {0};

// Gusanos KEY_* -> SDL scancode mapping table
static SDL_Scancode gusKeyToSDL[KEY_MAX] = {SDL_SCANCODE_UNKNOWN};

namespace {

void buildKeyMappings() {
	// Letters
	gusKeyToSDL[KEY_A] = SDL_SCANCODE_A;
	gusKeyToSDL[KEY_B] = SDL_SCANCODE_B;
	gusKeyToSDL[KEY_C] = SDL_SCANCODE_C;
	gusKeyToSDL[KEY_D] = SDL_SCANCODE_D;
	gusKeyToSDL[KEY_E] = SDL_SCANCODE_E;
	gusKeyToSDL[KEY_F] = SDL_SCANCODE_F;
	gusKeyToSDL[KEY_G] = SDL_SCANCODE_G;
	gusKeyToSDL[KEY_H] = SDL_SCANCODE_H;
	gusKeyToSDL[KEY_I] = SDL_SCANCODE_I;
	gusKeyToSDL[KEY_J] = SDL_SCANCODE_J;
	gusKeyToSDL[KEY_K] = SDL_SCANCODE_K;
	gusKeyToSDL[KEY_L] = SDL_SCANCODE_L;
	gusKeyToSDL[KEY_M] = SDL_SCANCODE_M;
	gusKeyToSDL[KEY_N] = SDL_SCANCODE_N;
	gusKeyToSDL[KEY_O] = SDL_SCANCODE_O;
	gusKeyToSDL[KEY_P] = SDL_SCANCODE_P;
	gusKeyToSDL[KEY_Q] = SDL_SCANCODE_Q;
	gusKeyToSDL[KEY_R] = SDL_SCANCODE_R;
	gusKeyToSDL[KEY_S] = SDL_SCANCODE_S;
	gusKeyToSDL[KEY_T] = SDL_SCANCODE_T;
	gusKeyToSDL[KEY_U] = SDL_SCANCODE_U;
	gusKeyToSDL[KEY_V] = SDL_SCANCODE_V;
	gusKeyToSDL[KEY_W] = SDL_SCANCODE_W;
	gusKeyToSDL[KEY_X] = SDL_SCANCODE_X;
	gusKeyToSDL[KEY_Y] = SDL_SCANCODE_Y;
	gusKeyToSDL[KEY_Z] = SDL_SCANCODE_Z;

	// Numbers
	gusKeyToSDL[KEY_0] = SDL_SCANCODE_0;
	gusKeyToSDL[KEY_1] = SDL_SCANCODE_1;
	gusKeyToSDL[KEY_2] = SDL_SCANCODE_2;
	gusKeyToSDL[KEY_3] = SDL_SCANCODE_3;
	gusKeyToSDL[KEY_4] = SDL_SCANCODE_4;
	gusKeyToSDL[KEY_5] = SDL_SCANCODE_5;
	gusKeyToSDL[KEY_6] = SDL_SCANCODE_6;
	gusKeyToSDL[KEY_7] = SDL_SCANCODE_7;
	gusKeyToSDL[KEY_8] = SDL_SCANCODE_8;
	gusKeyToSDL[KEY_9] = SDL_SCANCODE_9;

	// Numpad
	gusKeyToSDL[KEY_0_PAD] = SDL_SCANCODE_KP_0;
	gusKeyToSDL[KEY_1_PAD] = SDL_SCANCODE_KP_1;
	gusKeyToSDL[KEY_2_PAD] = SDL_SCANCODE_KP_2;
	gusKeyToSDL[KEY_3_PAD] = SDL_SCANCODE_KP_3;
	gusKeyToSDL[KEY_4_PAD] = SDL_SCANCODE_KP_4;
	gusKeyToSDL[KEY_5_PAD] = SDL_SCANCODE_KP_5;
	gusKeyToSDL[KEY_6_PAD] = SDL_SCANCODE_KP_6;
	gusKeyToSDL[KEY_7_PAD] = SDL_SCANCODE_KP_7;
	gusKeyToSDL[KEY_8_PAD] = SDL_SCANCODE_KP_8;
	gusKeyToSDL[KEY_9_PAD] = SDL_SCANCODE_KP_9;

	// Function keys
	gusKeyToSDL[KEY_F1] = SDL_SCANCODE_F1;
	gusKeyToSDL[KEY_F2] = SDL_SCANCODE_F2;
	gusKeyToSDL[KEY_F3] = SDL_SCANCODE_F3;
	gusKeyToSDL[KEY_F4] = SDL_SCANCODE_F4;
	gusKeyToSDL[KEY_F5] = SDL_SCANCODE_F5;
	gusKeyToSDL[KEY_F6] = SDL_SCANCODE_F6;
	gusKeyToSDL[KEY_F7] = SDL_SCANCODE_F7;
	gusKeyToSDL[KEY_F8] = SDL_SCANCODE_F8;
	gusKeyToSDL[KEY_F9] = SDL_SCANCODE_F9;
	gusKeyToSDL[KEY_F10] = SDL_SCANCODE_F10;
	gusKeyToSDL[KEY_F11] = SDL_SCANCODE_F11;
	gusKeyToSDL[KEY_F12] = SDL_SCANCODE_F12;

	// Cursor and navigation
	gusKeyToSDL[KEY_ESC] = SDL_SCANCODE_ESCAPE;
	gusKeyToSDL[KEY_TILDE] = SDL_SCANCODE_GRAVE;
	gusKeyToSDL[KEY_MINUS] = SDL_SCANCODE_MINUS;
	gusKeyToSDL[KEY_EQUALS] = SDL_SCANCODE_EQUALS;
	gusKeyToSDL[KEY_BACKSPACE] = SDL_SCANCODE_BACKSPACE;
	gusKeyToSDL[KEY_TAB] = SDL_SCANCODE_TAB;
	gusKeyToSDL[KEY_OPENBRACE] = SDL_SCANCODE_LEFTBRACKET;
	gusKeyToSDL[KEY_CLOSEBRACE] = SDL_SCANCODE_RIGHTBRACKET;
	gusKeyToSDL[KEY_ENTER] = SDL_SCANCODE_RETURN;
	gusKeyToSDL[KEY_COLON] = SDL_SCANCODE_SEMICOLON;
	gusKeyToSDL[KEY_QUOTE] = SDL_SCANCODE_APOSTROPHE;
	gusKeyToSDL[KEY_BACKSLASH] = SDL_SCANCODE_BACKSLASH;
	gusKeyToSDL[KEY_BACKSLASH2] = SDL_SCANCODE_NONUSHASH;
	gusKeyToSDL[KEY_COMMA] = SDL_SCANCODE_COMMA;
	gusKeyToSDL[KEY_STOP] = SDL_SCANCODE_PERIOD;
	gusKeyToSDL[KEY_SLASH] = SDL_SCANCODE_SLASH;
	gusKeyToSDL[KEY_SPACE] = SDL_SCANCODE_SPACE;
	gusKeyToSDL[KEY_INSERT] = SDL_SCANCODE_INSERT;
	gusKeyToSDL[KEY_DEL] = SDL_SCANCODE_DELETE;
	gusKeyToSDL[KEY_HOME] = SDL_SCANCODE_HOME;
	gusKeyToSDL[KEY_END] = SDL_SCANCODE_END;
	gusKeyToSDL[KEY_PGUP] = SDL_SCANCODE_PAGEUP;
	gusKeyToSDL[KEY_PGDN] = SDL_SCANCODE_PAGEDOWN;
	gusKeyToSDL[KEY_LEFT] = SDL_SCANCODE_LEFT;
	gusKeyToSDL[KEY_RIGHT] = SDL_SCANCODE_RIGHT;
	gusKeyToSDL[KEY_UP] = SDL_SCANCODE_UP;
	gusKeyToSDL[KEY_DOWN] = SDL_SCANCODE_DOWN;

	// Keypad symbols
	gusKeyToSDL[KEY_SLASH_PAD] = SDL_SCANCODE_KP_DIVIDE;
	gusKeyToSDL[KEY_ASTERISK] = SDL_SCANCODE_KP_MULTIPLY;
	gusKeyToSDL[KEY_MINUS_PAD] = SDL_SCANCODE_KP_MINUS;
	gusKeyToSDL[KEY_PLUS_PAD] = SDL_SCANCODE_KP_PLUS;
	gusKeyToSDL[KEY_DEL_PAD] = SDL_SCANCODE_KP_PERIOD;
	gusKeyToSDL[KEY_ENTER_PAD] = SDL_SCANCODE_KP_ENTER;
	gusKeyToSDL[KEY_EQUALS_PAD] = SDL_SCANCODE_KP_EQUALS;

	// Misc keys
	gusKeyToSDL[KEY_PRTSCR] = SDL_SCANCODE_PRINTSCREEN;
	gusKeyToSDL[KEY_PAUSE] = SDL_SCANCODE_PAUSE;
	gusKeyToSDL[KEY_ABNT_C1] = SDL_SCANCODE_INTERNATIONAL1;
	gusKeyToSDL[KEY_YEN] = SDL_SCANCODE_NONUSBACKSLASH;
	gusKeyToSDL[KEY_KANA] = SDL_SCANCODE_INTERNATIONAL2;
	gusKeyToSDL[KEY_CONVERT] = SDL_SCANCODE_INTERNATIONAL4;
	gusKeyToSDL[KEY_NOCONVERT] = SDL_SCANCODE_INTERNATIONAL5;
	gusKeyToSDL[KEY_AT] = SDL_SCANCODE_INTERNATIONAL3;
	gusKeyToSDL[KEY_CIRCUMFLEX] = SDL_SCANCODE_NONUSBACKSLASH;
	gusKeyToSDL[KEY_COLON2] = SDL_SCANCODE_SEMICOLON; // Near-dup of colon on some layouts
	gusKeyToSDL[KEY_KANJI] = SDL_SCANCODE_INTERNATIONAL6;
	gusKeyToSDL[KEY_BACKQUOTE] = SDL_SCANCODE_GRAVE;
	gusKeyToSDL[KEY_SEMICOLON] = SDL_SCANCODE_SEMICOLON;
	gusKeyToSDL[KEY_COMMAND] = SDL_SCANCODE_LGUI;

	// Modifiers
	gusKeyToSDL[KEY_LSHIFT] = SDL_SCANCODE_LSHIFT;
	gusKeyToSDL[KEY_RSHIFT] = SDL_SCANCODE_RSHIFT;
	gusKeyToSDL[KEY_LCONTROL] = SDL_SCANCODE_LCTRL;
	gusKeyToSDL[KEY_RCONTROL] = SDL_SCANCODE_RCTRL;
	gusKeyToSDL[KEY_ALT] = SDL_SCANCODE_LALT;
	gusKeyToSDL[KEY_ALTGR] = SDL_SCANCODE_RALT;
	gusKeyToSDL[KEY_LWIN] = SDL_SCANCODE_LGUI;
	gusKeyToSDL[KEY_RWIN] = SDL_SCANCODE_RGUI;
	gusKeyToSDL[KEY_MENU] = SDL_SCANCODE_MENU;

	// Lock keys
	gusKeyToSDL[KEY_SCRLOCK] = SDL_SCANCODE_SCROLLLOCK;
	gusKeyToSDL[KEY_NUMLOCK] = SDL_SCANCODE_NUMLOCKCLEAR;
	gusKeyToSDL[KEY_CAPSLOCK] = SDL_SCANCODE_CAPSLOCK;

	// Build reverse mapping (SDL -> Gusanos)
	for (int i = 1; i < KEY_MAX; ++i) {
		SDL_Scancode sdl = gusKeyToSDL[i];
		if (sdl != SDL_SCANCODE_UNKNOWN) {
			sdlToGusKey[sdl] = i;
		}
	}
}

} // anonymous namespace

KeyHandler keyHandler;

//=========================LIFECYCLE==========================//

KeyHandler::KeyHandler() {
	for (int i = 0; i < KEY_MAX; ++i) {
		oldKeys[i] = false;
	}
}

KeyHandler::~KeyHandler() {}

//=========================INTERFACE==========================//

void KeyHandler::init() {
	buildKeyMappings();
	SDL_StartTextInput(gfx.window);

	// The engine reads mouse position via SDL_GetMouseState and gamepad axes
	// via SDL_GetGamepadAxis, so motion events serve no consumer here. Without
	// this, motion events pile up in the SDL event queue and eventually cause
	// TEXT_INPUT events to be silently dropped, breaking console character
	// input after prolonged play.
	SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION, false);
	SDL_SetEventEnabled(SDL_EVENT_GAMEPAD_AXIS_MOTION, false);
}

void KeyHandler::shutDown() {
	SDL_StopTextInput(gfx.window);
}

void KeyHandler::pollKeyboard() {
	// Pump SDL events to get current keyboard state
	SDL_PumpEvents();

	// Process text input events and quit
	SDL_Event event;
	while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_TEXT_INPUT, SDL_EVENT_TEXT_INPUT) > 0) {
		// Fire printableChar for each character in the text
		for (const char *p = event.text.text; *p; ++p) {
			unsigned char c = static_cast<unsigned char>(*p);
			// Determine the scancode from modifier state
			// We use 0 as scancode placeholder since SDL3 text input doesn't
			// directly provide the scancode that generated the character
			// We'll try to look it up from current key state
			int sc = 0;
			// Check if shift is held - approximate by scanning our key state
			// This is a simplification; the previous Allegro readkey() returned
			// (scancode << 8) | ascii, but for printableChar consumers the
			// character is what matters most
			printableChar(c, sc);
		}
	}

	// Also process quit events
	while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_QUIT, SDL_EVENT_QUIT) > 0) {
		// Signal quit
		quit = true;
	}

	// Drain every remaining queued event. SDL3's event queue is bounded; if it
	// fills up, new events (including the TEXT_INPUT events the console relies
	// on) are silently dropped. The state-polling APIs (SDL_GetKeyboardState /
	// SDL_GetMouseState / SDL_GetGamepadAxis / ...) are unaffected by this drain,
	// so the rest of the engine keeps working exactly as before.
	{
		SDL_Event discard;
		while (SDL_PollEvent(&discard) > 0) {
			// intentionally empty — relevant event types are already handled
			// by the targeted SDL_PeepEvents loops above.
		}
	}

	// Diff keyboard state and fire signals
	for (int i = 0; i < KEY_MAX; ++i) {
		bool state = getKey(i);

		if (state != oldKeys[i]) {
			if (state)
				keyDown(i);
			else
				keyUp(i);

			oldKeys[i] = state;
		}
	}
}

bool KeyHandler::getKey(int k) {
	if (k < 0 || k >= KEY_MAX)
		return false;
	SDL_Scancode sc = gusKeyToSDL[k];
	if (sc == SDL_SCANCODE_UNKNOWN)
		return false;
	const bool *keyState = SDL_GetKeyboardState(NULL);
	return keyState[sc] != 0;
}

int KeyHandler::mapKey(int k) {
	return k;
}

#endif