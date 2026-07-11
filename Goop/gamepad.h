#ifndef GAMEPAD_h
#define GAMEPAD_h

#ifdef DEDSERV
#error "Can't use this in dedicated server"
#endif // DEDSERV

#include <SDL3/SDL.h>
#include <SDL3/SDL_gamepad.h>
#include <boost/signals2/signal.hpp>
#include <string>
#include <vector>

/* Gamepad button/axis enumerations
 * Names follow the naming convention from the plan:
 * - D-pad: DPAD_UP/DOWN/LEFT/RIGHT
 * - Face: A (SOUTH), B (EAST), X (WEST), Y (NORTH)
 * - Shoulder: LB (LEFT_SHOULDER), RB (RIGHT_SHOULDER)
 * - Trigger (axis): LT (LEFT_TRIGGER), RT (RIGHT_TRIGGER)
 * - Stick: LEFT_STICK/L3, RIGHT_STICK/R3
 * - Stick dirs: LSTICK_LEFT/RIGHT/UP/DOWN, RSTICK_LEFT/RIGHT/UP/DOWN
 */
enum GamepadInput
{
	GP_DPAD_UP = 0,   GP_DPAD_DOWN,  GP_DPAD_LEFT,   GP_DPAD_RIGHT,
	GP_A,             GP_B,           GP_X,           GP_Y,           /* Face buttons */
	GP_BACK,          GP_GUIDE,       GP_START,
	GP_LEFT_STICK,    GP_RIGHT_STICK, /* Stick buttons */
	GP_LB,            GP_RB,          /* Shoulders */
	GP_LT,            GP_RT,          /* Triggers via axis */
	GP_LSTICK_LEFT,   GP_LSTICK_RIGHT, GP_LSTICK_UP,   GP_LSTICK_DOWN,
	GP_RSTICK_LEFT,   GP_RSTICK_RIGHT, GP_RSTICK_UP,   GP_RSTICK_DOWN,
	/* End marker */
	GP_INPUT_COUNT
};

constexpr int GAMEPAD_KEY_BASE = 128;
constexpr int MAX_GAMEPADS = 4;
constexpr int GP_INPUT_COUNT_PER_PAD = GP_INPUT_COUNT;
constexpr int GP_MAX_CODES = GAMEPAD_KEY_BASE + MAX_GAMEPADS * GP_INPUT_COUNT_PER_PAD;

/* Resolution helpers */
int gamepadName2Int(const std::string& name);
void appendGamepadBindNames(std::vector<std::string>& names);
std::string gamepadBindName(int slot, GamepadInput input);

class GamepadHandler
{
public:
	struct StopEarly
	{
		typedef bool result_type;

		template<typename InputIterator>
		bool operator()(InputIterator first, InputIterator last) const
		{
			// Stop at the first slot returning false
			for(; first != last; ++first)
			{
				if(!*first)
					return false;
			}

			return true;
		}
	};

	GamepadHandler();
	~GamepadHandler();

	void init();
	void shutDown();
	void poll();

	/* Info command - returns string listing connected gamepads */
	std::string info();

	void registerInConsole();

	bool isPressed(int slot, GamepadInput input) const;
	bool isConnected(int slot) const;
	float getAxis(int slot, int axis) const;

	// Emits an analogInput for a stick direction and updates m_old.
	void emitStickAnalog(int slot, GamepadInput input, float value);

	boost::signals2::signal<bool(int), StopEarly> buttonDown;
	boost::signals2::signal<bool(int), StopEarly> buttonUp;

	// Carries a normalized (0..1) magnitude for analog stick directions.
	boost::signals2::signal<void(int, float)> analogInput;

private:
	/* SDL handles */
	SDL_Gamepad* m_pad[MAX_GAMEPADS];
	SDL_JoystickID m_instance[MAX_GAMEPADS]; /* Instance IDs from SDL_GetGamepads */

	/* Previous state storage for diffing */
	bool m_old[MAX_GAMEPADS][GP_INPUT_COUNT];

	/* Axis values storage for cvars (raw -1..1 for sticks, 0..1 for triggers) */
	float m_axis[MAX_GAMEPADS][6]; /* leftx, lefty, rightx, righty, lt, rt */
	int m_deadzone;
};

extern GamepadHandler gamepadHandler;

#endif // GAMEPAD_h