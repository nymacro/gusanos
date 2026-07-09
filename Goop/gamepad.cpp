#ifndef DEDSERV

#include "gamepad.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_gamepad.h>
#include <console.h>
#include "gconsole.h"
#include <list>
#include <string>
#include <boost/array.hpp>
using boost::array;

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <iomanip>

static struct GamepadInputInfo
{
	const char* primary;
	const char* aliases[10];
} g_inputInfo[] =
{
	{ "dpad_up",   { "dpad_up",   nullptr } },
	{ "dpad_down",  { "dpad_down",  nullptr } },
	{ "dpad_left",  { "dpad_left",  nullptr } },
	{ "dpad_right", { "dpad_right", nullptr } },
	{ "a",         { "a", "south",              nullptr } },
	{ "b",         { "b", "east",               nullptr } },
	{ "x",         { "x", "west",               nullptr } },
	{ "y",         { "y", "north",              nullptr } },
	{ "back",      { "back", nullptr } },
	{ "guide",     { "guide", nullptr } },
	{ "start",     { "start", nullptr } },
	{ "l3",        { "l3", "left_stick" } },
	{ "r3",        { "r3", "right_stick" } },
	{ "lb",        { "lb", "left_shoulder", "l1", "l Shoulder" } },
	{ "rb",        { "rb", "right_shoulder", "r1", "r Shoulder" } },
	{ "lt",        { "lt", "left_trigger", "l2", "l Trigger" } },
	{ "rt",        { "rt", "right_trigger", "r2", "r Trigger" } },
	{ "lstick_left",  { "lstick_left",  nullptr } },
	{ "lstick_right", { "lstick_right", nullptr } },
	{ "lstick_up",    { "lstick_up",    nullptr } },
	{ "lstick_down",  { "lstick_down",  nullptr } },
	{ "rstick_left",  { "rstick_left",  nullptr } },
	{ "rstick_right", { "rstick_right", nullptr } },
	{ "rstick_up",    { "rstick_up",    nullptr } },
	{ "rstick_down",  { "rstick_down",  nullptr } },
};

constexpr int TRIGGER_THRESHOLD = 1000;

namespace {
	static bool gamepadInited = false;
	constexpr Uint32 SDL_INVALID_JOYSTICK_ID = UINT32_MAX;
}

GamepadHandler::GamepadHandler()
{
	for(int i = 0; i < MAX_GAMEPADS; ++i)
	{
		m_pad[i] = nullptr;
		m_instance[i] = SDL_INVALID_JOYSTICK_ID;
		for(int j = 0; j < GP_INPUT_COUNT; ++j)
		{
			m_old[i][j] = false;
		}
		for(int j = 0; j < 6; ++j)
		{
			m_axis[i][j] = 0.0f;
		}
		m_deadzone = 8000;
	}
}

GamepadHandler::~GamepadHandler()
{
	shutDown();
}

void GamepadHandler::init()
{
	if(gamepadInited)
		return;

	if(!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
	{
		SDL_Log("GamepadHandler: Failed to init SDL gamepad: %s", SDL_GetError());
		return;
	}
	gamepadInited = true;

	int count;
	SDL_JoystickID* joysticks = SDL_GetGamepads(&count);
	if(joysticks && count > 0)
	{
		for(int i = 0; i < std::min(count, MAX_GAMEPADS); ++i)
		{
			SDL_JoystickID id = joysticks[i];
			SDL_Gamepad* pad = SDL_OpenGamepad(id);
			if(pad)
			{
				m_pad[i] = pad;
				m_instance[i] = id;
			}
		}
		SDL_free(joysticks);
	}
}

void GamepadHandler::shutDown()
{
	if(!gamepadInited)
		return;
	
	for(int i = 0; i < MAX_GAMEPADS; ++i)
	{
		if(m_pad[i])
		{
			SDL_CloseGamepad(m_pad[i]);
			m_pad[i] = nullptr;
			m_instance[i] = SDL_INVALID_JOYSTICK_ID;
		}
	}
	
	SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
	gamepadInited = false;
}

std::string GamepadHandler::info()
{
	std::ostringstream oss;

	int count;
	SDL_JoystickID* joysticks = SDL_GetGamepads(&count);

	oss << "Gamepads connected: " << count << "\n";
	if(!joysticks || count == 0)
	{
		oss << "No gamepads connected.\n";
		if(joysticks)
			SDL_free(joysticks);
		return oss.str();
	}

	for(int i = 0; i < std::min(count, MAX_GAMEPADS); ++i)
	{
		SDL_JoystickID id = joysticks[i];
		SDL_Gamepad* pad = m_pad[i];
		if(pad)
		{
			const char* name = SDL_GetGamepadName(pad);
			int playerIdx = SDL_GetGamepadPlayerIndexForID(id);

			oss << "  GP" << i << ": \"" << (name ? name : "Unknown") << "\"\n";
			if(playerIdx >= 0 && playerIdx <= 3)
			{
				oss << "      Player index: " << playerIdx << "\n";
			}

			oss << "      Buttons: South(A), East(B), West(X), North(Y), Plus(Guide), Minus(Back), Start, L3/L2, R3/R2, L/R Shoulders, L/R Triggers (LT/RT)\n";
		}
	}

	if(joysticks)
		SDL_free(joysticks);

	return oss.str();
}

void GamepadHandler::registerInConsole()
{
	console.registerVariables()
		("gp_deadzone", &m_deadzone, 8000);

	for(int slot = 0; slot < MAX_GAMEPADS; ++slot)
	{
		char prefix[16];
		std::snprintf(prefix, sizeof(prefix), "gp%1d_", slot);

		console.registerVariables()
			((std::string(prefix) + "leftx").c_str(), &m_axis[slot][0], 0.0f)
			((std::string(prefix) + "lefty").c_str(), &m_axis[slot][1], 0.0f)
			((std::string(prefix) + "rightx").c_str(), &m_axis[slot][2], 0.0f)
			((std::string(prefix) + "righty").c_str(), &m_axis[slot][3], 0.0f)
			((std::string(prefix) + "lt").c_str(),    &m_axis[slot][4], 0.0f)
			((std::string(prefix) + "rt").c_str(),    &m_axis[slot][5], 0.0f);
	}
}

void GamepadHandler::poll()
{
	SDL_PumpEvents();

	int count;
	SDL_JoystickID* joysticks = SDL_GetGamepads(&count);

	for(int i = 0; i < MAX_GAMEPADS; ++i)
	{
		if(m_pad[i] && (m_instance[i] == SDL_INVALID_JOYSTICK_ID || !SDL_IsGamepad(m_instance[i])))
		{
			SDL_CloseGamepad(m_pad[i]);
			m_pad[i] = nullptr;
			m_instance[i] = SDL_INVALID_JOYSTICK_ID;
		}
	}

	if(joysticks && count > 0)
	{
		for(int i = 0; i < std::min(count, MAX_GAMEPADS); ++i)
		{
			SDL_JoystickID id = joysticks[i];
			int slot = -1;

			for(int j = 0; j < MAX_GAMEPADS; ++j)
			{
				if(m_pad[j] && m_instance[j] == id)
				{
					slot = j;
					break;
				}
			}

			if(slot == -1)
			{
				for(int j = 0; j < MAX_GAMEPADS; ++j)
				{
					if(!m_pad[j])
					{
						slot = j;
						SDL_Gamepad* pad = SDL_OpenGamepad(id);
						if(pad)
						{
							m_pad[slot] = pad;
							m_instance[slot] = id;
						}
						break;
					}
				}
			}

			if(slot >= 0 && m_pad[slot])
			{
				m_axis[slot][0] = (float)SDL_GetGamepadAxis(m_pad[slot], SDL_GAMEPAD_AXIS_LEFTX);
				m_axis[slot][0] = m_axis[slot][0] < -1.0f ? -1.0f : (m_axis[slot][0] > 1.0f ? 1.0f : m_axis[slot][0]);
				m_axis[slot][1] = (float)SDL_GetGamepadAxis(m_pad[slot], SDL_GAMEPAD_AXIS_LEFTY);
				m_axis[slot][1] = m_axis[slot][1] < -1.0f ? -1.0f : (m_axis[slot][1] > 1.0f ? 1.0f : m_axis[slot][1]);
				m_axis[slot][2] = (float)SDL_GetGamepadAxis(m_pad[slot], SDL_GAMEPAD_AXIS_RIGHTX);
				m_axis[slot][2] = m_axis[slot][2] < -1.0f ? -1.0f : (m_axis[slot][2] > 1.0f ? 1.0f : m_axis[slot][2]);
				m_axis[slot][3] = (float)SDL_GetGamepadAxis(m_pad[slot], SDL_GAMEPAD_AXIS_RIGHTY);
				m_axis[slot][3] = m_axis[slot][3] < -1.0f ? -1.0f : (m_axis[slot][3] > 1.0f ? 1.0f : m_axis[slot][3]);
				m_axis[slot][4] = (float)SDL_GetGamepadAxis(m_pad[slot], SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
				m_axis[slot][4] = m_axis[slot][4] < 0.0f ? 0.0f : (m_axis[slot][4] > 1.0f ? 1.0f : m_axis[slot][4]);
				m_axis[slot][5] = (float)SDL_GetGamepadAxis(m_pad[slot], SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
				m_axis[slot][5] = m_axis[slot][5] < 0.0f ? 0.0f : (m_axis[slot][5] > 1.0f ? 1.0f : m_axis[slot][5]);
			}
		}
	}

	if(joysticks)
		SDL_free(joysticks);

	for(int slot = 0; slot < MAX_GAMEPADS; ++slot)
	{
		if(!m_pad[slot])
			continue;

		bool a = SDL_GetGamepadButton(m_pad[slot], SDL_GAMEPAD_BUTTON_SOUTH);
		if(a != m_old[slot][GP_A])
		{
			m_old[slot][GP_A] = a;
			if(a)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_A);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_A);
		}

		bool b = SDL_GetGamepadButton(m_pad[slot], SDL_GAMEPAD_BUTTON_EAST);
		if(b != m_old[slot][GP_B])
		{
			m_old[slot][GP_B] = b;
			if(b)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_B);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_B);
		}

		bool x = SDL_GetGamepadButton(m_pad[slot], SDL_GAMEPAD_BUTTON_WEST);
		if(x != m_old[slot][GP_X])
		{
			m_old[slot][GP_X] = x;
			if(x)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_X);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_X);
		}

		bool y = SDL_GetGamepadButton(m_pad[slot], SDL_GAMEPAD_BUTTON_NORTH);
		if(y != m_old[slot][GP_Y])
		{
			m_old[slot][GP_Y] = y;
			if(y)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_Y);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_Y);
		}

		bool back = SDL_GetGamepadButton(m_pad[slot], SDL_GAMEPAD_BUTTON_BACK);
		if(back != m_old[slot][GP_BACK])
		{
			m_old[slot][GP_BACK] = back;
			if(back)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_BACK);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_BACK);
		}

		bool guide = SDL_GetGamepadButton(m_pad[slot], SDL_GAMEPAD_BUTTON_GUIDE);
		if(guide != m_old[slot][GP_GUIDE])
		{
			m_old[slot][GP_GUIDE] = guide;
			if(guide)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_GUIDE);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_GUIDE);
		}

		bool start = SDL_GetGamepadButton(m_pad[slot], SDL_GAMEPAD_BUTTON_START);
		if(start != m_old[slot][GP_START])
		{
			m_old[slot][GP_START] = start;
			if(start)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_START);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_START);
		}

		bool l3 = SDL_GetGamepadButton(m_pad[slot], SDL_GAMEPAD_BUTTON_LEFT_STICK);
		if(l3 != m_old[slot][GP_LEFT_STICK])
		{
			m_old[slot][GP_LEFT_STICK] = l3;
			if(l3)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_LEFT_STICK);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_LEFT_STICK);
		}

		bool r3 = SDL_GetGamepadButton(m_pad[slot], SDL_GAMEPAD_BUTTON_RIGHT_STICK);
		if(r3 != m_old[slot][GP_RIGHT_STICK])
		{
			m_old[slot][GP_RIGHT_STICK] = r3;
			if(r3)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_RIGHT_STICK);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_RIGHT_STICK);
		}

		bool lb = SDL_GetGamepadButton(m_pad[slot], SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
		if(lb != m_old[slot][GP_LB])
		{
			m_old[slot][GP_LB] = lb;
			if(lb)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_LB);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_LB);
		}

		bool rb = SDL_GetGamepadButton(m_pad[slot], SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
		if(rb != m_old[slot][GP_RB])
		{
			m_old[slot][GP_RB] = rb;
			if(rb)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_RB);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_RB);
		}

		float ltAxis = m_axis[slot][4];
		bool lt = ltAxis * 32767 > TRIGGER_THRESHOLD;
		if(lt != m_old[slot][GP_LT])
		{
			m_old[slot][GP_LT] = lt;
			if(lt)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_LT);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_LT);
		}

		float rtAxis = m_axis[slot][5];
		bool rt = rtAxis * 32767 > TRIGGER_THRESHOLD;
		if(rt != m_old[slot][GP_RT])
		{
			m_old[slot][GP_RT] = rt;
			if(rt)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_RT);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_RT);
		}

		bool d_up = false, d_down = false, d_left = false, d_right = false;

		bool dpad_up_btn = SDL_GetGamepadButton(m_pad[slot], SDL_GAMEPAD_BUTTON_DPAD_UP);
		bool dpad_down_btn = SDL_GetGamepadButton(m_pad[slot], SDL_GAMEPAD_BUTTON_DPAD_DOWN);
		bool dpad_left_btn = SDL_GetGamepadButton(m_pad[slot], SDL_GAMEPAD_BUTTON_DPAD_LEFT);
		bool dpad_right_btn = SDL_GetGamepadButton(m_pad[slot], SDL_GAMEPAD_BUTTON_DPAD_RIGHT);

		if(dpad_up_btn || dpad_down_btn)
		{
			int up = SDL_GetGamepadAxis(m_pad[slot], SDL_GAMEPAD_AXIS_LEFTY);
			int down = SDL_GetGamepadAxis(m_pad[slot], SDL_GAMEPAD_AXIS_RIGHTY);
			int left = SDL_GetGamepadAxis(m_pad[slot], SDL_GAMEPAD_AXIS_LEFTX);
			int right = SDL_GetGamepadAxis(m_pad[slot], SDL_GAMEPAD_AXIS_RIGHTX);

			d_up = up < -m_deadzone;
			d_down = down < -m_deadzone;
			d_left = left < -m_deadzone;
			d_right = right < -m_deadzone;
		}
		else
		{
			d_up = dpad_up_btn;
			d_down = dpad_down_btn;
			d_left = dpad_left_btn;
			d_right = dpad_right_btn;
		}

		if(d_up != m_old[slot][GP_DPAD_UP])
		{
			m_old[slot][GP_DPAD_UP] = d_up;
			if(d_up)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_DPAD_UP);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_DPAD_UP);
		}
		if(d_down != m_old[slot][GP_DPAD_DOWN])
		{
			m_old[slot][GP_DPAD_DOWN] = d_down;
			if(d_down)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_DPAD_DOWN);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_DPAD_DOWN);
		}
		if(d_left != m_old[slot][GP_DPAD_LEFT])
		{
			m_old[slot][GP_DPAD_LEFT] = d_left;
			if(d_left)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_DPAD_LEFT);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_DPAD_LEFT);
		}
		if(d_right != m_old[slot][GP_DPAD_RIGHT])
		{
			m_old[slot][GP_DPAD_RIGHT] = d_right;
			if(d_right)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_DPAD_RIGHT);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_DPAD_RIGHT);
		}

		float l_x = m_axis[slot][0];
		float l_y = m_axis[slot][1];

		bool lst_left = l_x < -m_deadzone;
		bool lst_right = l_x > m_deadzone;
		bool lst_up = l_y < -m_deadzone;
		bool lst_down = l_y > m_deadzone;

		if(lst_left != m_old[slot][GP_LSTICK_LEFT])
		{
			m_old[slot][GP_LSTICK_LEFT] = lst_left;
			if(lst_left)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_LSTICK_LEFT);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_LSTICK_LEFT);
		}
		if(lst_right != m_old[slot][GP_LSTICK_RIGHT])
		{
			m_old[slot][GP_LSTICK_RIGHT] = lst_right;
			if(lst_right)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_LSTICK_RIGHT);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_LSTICK_RIGHT);
		}
		if(lst_up != m_old[slot][GP_LSTICK_UP])
		{
			m_old[slot][GP_LSTICK_UP] = lst_up;
			if(lst_up)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_LSTICK_UP);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_LSTICK_UP);
		}
		if(lst_down != m_old[slot][GP_LSTICK_DOWN])
		{
			m_old[slot][GP_LSTICK_DOWN] = lst_down;
			if(lst_down)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_LSTICK_DOWN);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_LSTICK_DOWN);
		}

		float r_x = m_axis[slot][2];
		float r_y = m_axis[slot][3];

		bool rst_left = r_x < -m_deadzone;
		bool rst_right = r_x > m_deadzone;
		bool rst_up = r_y < -m_deadzone;
		bool rst_down = r_y > m_deadzone;

		if(rst_left != m_old[slot][GP_RSTICK_LEFT])
		{
			m_old[slot][GP_RSTICK_LEFT] = rst_left;
			if(rst_left)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_RSTICK_LEFT);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_RSTICK_LEFT);
		}
		if(rst_right != m_old[slot][GP_RSTICK_RIGHT])
		{
			m_old[slot][GP_RSTICK_RIGHT] = rst_right;
			if(rst_right)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_RSTICK_RIGHT);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_RSTICK_RIGHT);
		}
		if(rst_up != m_old[slot][GP_RSTICK_UP])
		{
			m_old[slot][GP_RSTICK_UP] = rst_up;
			if(rst_up)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_RSTICK_UP);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_RSTICK_UP);
		}
		if(rst_down != m_old[slot][GP_RSTICK_DOWN])
		{
			m_old[slot][GP_RSTICK_DOWN] = rst_down;
			if(rst_down)
				buttonDown(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_RSTICK_DOWN);
			else
				buttonUp(GAMEPAD_KEY_BASE + slot * GP_INPUT_COUNT + GP_RSTICK_DOWN);
		}
	}
}

GamepadHandler gamepadHandler;

#endif
