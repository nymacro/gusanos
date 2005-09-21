#ifndef NINJAROPE_H
#define NINJAROPE_H

#include "base_object.h"
#include "events.h"
//#include "part_type.h"
#include "vec.h"
#include "angle.h"
//#include "animators.h"
#include <vector>
//#include <boost/variant.hpp>

#ifndef DEDSERV
class SpriteSet;
class BaseAnimator;
#endif
class PartType;
class TimerEvent;
//class BaseWorm;

struct NRTimer
{
	NRTimer( TimerEvent* tEvent);
	
	void reset();
	
	int count;
	TimerEvent* m_tEvent;
};

class NinjaRope : public BaseObject
{
public:

	NinjaRope(PartType* type, BaseObject* worm);
	
	void shoot(Vec _pos, Vec _spd);
	void remove();

#ifndef DEDSERV
	void draw(BITMAP* where, int xOff, int yOff);
#endif
	void think();
	Angle getAngle();
	void addAngleSpeed(AngleDiff);
	
	void addLength(float length_)
	{
		m_length += length_;
		if ( m_length < 0.f )
			m_length = 0.f;
	}
	
	int getColour();
	Vec& getPosReference();
	
	bool active;
	bool attached;
	
private:
	
	std::vector< NRTimer > timer;
	PartType* m_type;
	Angle m_angle;
	AngleDiff m_angleSpeed;
	float m_length;
	BaseObject* m_worm;
#ifndef DEDSERV
	SpriteSet* m_sprite;
	BaseAnimator* m_animator;
#endif
	
	bool justCreated;
};

#endif  // _PARTICLE_H_
