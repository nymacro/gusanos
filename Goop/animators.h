#ifndef ANIMATORS_H
#define ANIMATORS_H

#ifdef DEDSERV
#error "Can't use this in dedicated server"
#endif // DEDSERV

#include "base_animator.h"

class SpriteSet;

class AnimPingPong : public BaseAnimator {
  public:
	AnimPingPong(SpriteSet *sprite, int duration);

	virtual void tick();
	virtual void reset();

  private:
	int m_totalFrames;
	int m_animPos;
	int m_duration;
	char m_currentDir;
};

class AnimLoopRight : public BaseAnimator {
  public:
	AnimLoopRight(SpriteSet *sprite, int duration);

	virtual void tick();
	virtual void reset();

  private:
	int m_totalFrames;
	int m_animPos;
	int m_duration;
};

class AnimRightOnce : public BaseAnimator {
  public:
	AnimRightOnce(SpriteSet *sprite, int duration);

	virtual void tick();
	virtual void reset();

  private:
	int m_totalFrames;
	int m_animPos;
	int m_duration;
};

#endif // _ANIMATORS_
