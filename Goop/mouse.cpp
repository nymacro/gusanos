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
	// Initialize positions from current SDL state.
	float fx, fy;
	SDL_GetMouseState(&fx, &fy);
	posX = static_cast<int>(fx);
	posY = static_cast<int>(fy);
	posZ = 0;
	for (int i = 0; i < 3; ++i)
		buttonStates[i] = false;
}

void MouseHandler::poll()
{
	// Pump events (keyboard already does this, but double-pumping is harmless)
	SDL_PumpEvents();

	// Save old scroll position before processing wheel events
	int oldPosZ = posZ;

	// Process mouse wheel events to accumulate scroll
	SDL_Event event;
	while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_MOUSE_WHEEL, SDL_EVENT_MOUSE_WHEEL) > 0)
	{
		posZ += static_cast<int>(event.wheel.y);
	}

	// Read current button state and position
	float fx, fy;
	Uint32 buttons = SDL_GetMouseState(&fx, &fy);

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

	newPosX /= gfx.getScalingFactor();
	newPosY /= gfx.getScalingFactor();

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