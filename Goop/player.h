#ifndef PLAYER_H
#define PLAYER_H

#include "base_player.h"
#include <memory>
#include <string>

#ifndef DEDSERV
class Viewport;
struct BITMAP;
#endif
class Worm;
class PlayerOptions;

class Player : public BasePlayer {
  public:
	enum Actions {
		LEFT = 0,
		RIGHT,
		UP,
		DOWN,
		FIRE,
		JUMP,
		CHANGE,
		WEAPON_NEXT,
		WEAPON_PREV,
		NINJAROPE,
		ACTION_COUNT,
	};

	Player(boost::shared_ptr<PlayerOptions> options, BaseWorm *worm);
	~Player();

	void subThink();
#ifndef DEDSERV
	void render();

	void assignViewport(Viewport *Viewport);
#endif
	void actionStart(Actions action, float intensity = 1.0f);
	void actionStop(Actions action);

  private:
	bool aimingUp;
	bool aimingDown;
	bool changing;
	bool jumping;
	bool walkingLeft;
	bool walkingRight;
	// Per-action state for analog/one-shot handling
	bool m_actionActive[ACTION_COUNT];
	float m_actionIntensity[ACTION_COUNT];
#ifndef DEDSERV
	Viewport *m_viewport;
#endif
};

#endif // _WORM_H_
