#ifndef DEDSERV
#include "mouse.h"
#include "gfx.h"
#include <SDL3/SDL.h>

MouseHandler mouseHandler;

namespace
{
	bool buttonStates[3] = {false, false, false};
	int  posX = 0;
	int  posY = 0;
	int  posZ = 0;
}

void MouseHandler::init()
{
	// SDL3 initializes mouse with SDL_Init; nothing extra needed.
	// The system cursor is hidden while the mouse is inside the game window so
	// that Gusanos can draw its own cursor sprite on the back buffer.
	if (gfx.window && (SDL_GetWindowFlags(gfx.window) & SDL_WINDOW_MOUSE_FOCUS)) {
		SDL_HideCursor();
	} else {
		SDL_ShowCursor();
	}

	// Initialize positions from current SDL state, converting to logical coords.
	float fx, fy;
	SDL_GetMouseState(&fx, &fy);
	if (gfx.renderer) {
		SDL_RenderCoordinatesFromWindow(gfx.renderer, fx, fy, &fx, &fy);
	}
	posX = static_cast<int>(fx);
	posY = static_cast<int>(fy);
	if (posX < 0) posX = 0;
	if (posX >= 320) posX = 319;
	if (posY < 0) posY = 0;
	if (posY >= 240) posY = 239;
	posZ = 0;
	for (int i = 0; i < 3; ++i)
		buttonStates[i] = false;
}

void MouseHandler::poll()
{
	// Pump events (keyboard already does this, but double-pumping is harmless)
	SDL_PumpEvents();

	// Hide the OS cursor while the mouse is inside the game window and restore
	// it when the pointer leaves, so the in-game rendered cursor is visible.
	SDL_Event windowEvent;
	while (SDL_PeepEvents(&windowEvent, 1, SDL_GETEVENT, SDL_EVENT_WINDOW_MOUSE_ENTER, SDL_EVENT_WINDOW_MOUSE_LEAVE) > 0)
	{
		if (windowEvent.type == SDL_EVENT_WINDOW_MOUSE_ENTER) {
			SDL_HideCursor();
		} else if (windowEvent.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
			SDL_ShowCursor();
		}
	}

	// Save old scroll position before processing wheel events
	int oldPosZ = posZ;

	// Process mouse wheel events to accumulate scroll
	SDL_Event event;
	while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_MOUSE_WHEEL, SDL_EVENT_MOUSE_WHEEL) > 0)
	{
		posZ += static_cast<int>(event.wheel.y);
	}

	// Read current button state and position, convert to logical coords
	float fx, fy;
	Uint32 buttons = SDL_GetMouseState(&fx, &fy);
	if (gfx.renderer) {
		SDL_RenderCoordinatesFromWindow(gfx.renderer, fx, fy, &fx, &fy);
	}

	// Button state diff
	for (int i = 0; i < 3; ++i)
	{
		bool state = (buttons & SDL_BUTTON_MASK(i + 1)) != 0;
		if (state != buttonStates[i])
		{
			buttonStates[i] = state;
			if (state)
				buttonDown(i);
			else
				buttonUp(i);
		}
	}

// oldPosZ was saved before wheel processing above

	int newPosX = static_cast<int>(fx);
	int newPosY = static_cast<int>(fy);

	if (newPosX < 0) newPosX = 0;
	if (newPosX >= 320) newPosX = 319;
	if (newPosY < 0) newPosY = 0;
	if (newPosY >= 240) newPosY = 239;

	if (newPosX != posX || newPosY != posY)
	{
		posX = newPosX;
		posY = newPosY;
		move(posX, posY);
	}

	if (posZ != oldPosZ)
	{
		scroll(posZ - oldPosZ);
		posZ = oldPosZ;
	}
}

void MouseHandler::shutDown()
{
	// Nothing to clean up — SDL3 handles mouse lifecycle
}

int MouseHandler::getX()
{
	return posX;
}

int MouseHandler::getY()
{
	return posY;
}

#endif