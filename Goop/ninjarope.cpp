#include "ninjarope.h"

#include "util/vec.h"
#include "game.h"
#include "base_object.h"
#include "base_worm.h"
#include "part_type.h"
#ifndef DEDSERV
#include "sprite_set.h"
#include "sprite.h"
#include "base_animator.h"
#include "animators.h"
#include "viewport.h"
#endif
#include "part_type.h"

#include <vector>

using namespace std;

NinjaRope::NinjaRope(PartType *type, BaseObject *worm) : m_worm(worm) {
	justCreated = false;
	active = false;
	attached = false;
	m_type = type;

	m_angle = 0;
	m_angleSpeed = 0;
	// m_animator = NULL;

#ifndef DEDSERV
	m_sprite = m_type->sprite;

	m_animator = m_type->allocateAnimator();
#endif

	// Why this?? :OO // Re: Modders may want to make the rope leave trails or sth :o
	for (auto t : m_type->timer) {
		timer.push_back(t->createState());
	}
}

NinjaRope::~NinjaRope() {
#ifndef DEDSERV
	delete m_animator;
	m_animator = 0;
#endif
}

void NinjaRope::shoot(Vec _pos, Vec _spd) {
	pos = _pos;
	spd = _spd;
	m_length = game.options.ninja_rope_startDistance;

	justCreated = true;
	active = true;
	attached = false;

	m_angle = spd.getAngle();
	m_angleSpeed = 0;

	// for ( vector< TimerEvent::State >::iterator t = timer.begin(); t != timer.end(); t++)
	for (auto &t : timer) {
		t.reset();
	}
}

void NinjaRope::remove() {
	active = false;
	justCreated = false;
	attached = false;
}

void NinjaRope::think() {
	// Only the worm's local simulator integrates rope physics. The owner
	// runs NinjaRope::think() and replicates pos/length/active/attached to
	// the server (OWNER_2_AUTH), which relays them to proxies
	// (AUTH_2_PROXY). Everyone else — the server (relaying a remote-client
	// worm) and proxies (rendering another player's worm) — must NOT
	// integrate here, because rope spd is NOT replicated: a local
	// pos += spd would drift away from the just-snapped replicated pos and
	// get re-snapped next tick, oscillating the hook. Worse, the rope then
	// calls m_worm->addSpeed() with that oscillating pull, propagating the
	// jitter to the worm body (the visible "worm + hook oscillation").
	// Non-simulators get the rope purely from replication; the worm's
	// replicated spd already carries the owner's rope pull, so no local
	// rope force is needed.
	BaseWorm *worm = dynamic_cast<BaseWorm *>(m_worm);
	if (!worm)
		return;
	if (!worm->isLocalAuthority())
		return;

	if (m_length > game.options.ninja_rope_maxLength)
		m_length = game.options.ninja_rope_maxLength;

	if (!active)
		return;

	if (justCreated && m_type->creation) {
		m_type->creation->run(this);
		justCreated = false;
	}

	for (int i = 0; !deleteMe && i < m_type->repeat; ++i) {
		pos += spd;

		BaseVec<long> ipos(pos);

		// TODO: Try to attach to worms/objects

		Vec diff(m_worm->pos, pos);
		float curLen = diff.length();
		Vec force(diff * game.options.ninja_rope_pullForce);

		/*
		if(<attached to object>)
		{
			//Apply force to object
		}
		else
		*/
		if (!game.level.getMaterial(ipos.x, ipos.y).particle_pass) {
			if (!attached) {
				m_length = 450.f / 16.f - 1.0f;
				attached = true;
				spd.zero();
				if (m_type->groundCollision)
					m_type->groundCollision->run(this);
			}
		} else
			attached = false;

		if (attached) {
			if (curLen > m_length) {
				m_worm->addSpeed(force / curLen);
			}
		} else {
			spd.y += m_type->gravity;

			if (curLen > m_length) {
				spd -= force / curLen;
			}
		}

#ifndef DEDSERV
		if (m_animator)
			m_animator->tick();
#endif

		/* OLD CODE
		if ( justCreated && m_type->creation )
		{
			m_type->creation->run(this);
			justCreated = false;
		}

		if( !game.level.getMaterial( (int)(pos+spd).x, (int)(pos+spd).y ).particle_pass )
		{
			if (!attached)
			{
				attached = true;
				pos = pos + spd;
				spd *= 0;
				if ( m_type->groundCollision != NULL )
						m_type->groundCollision->run(this);
			}
		}else attached = false;

		if (!attached)
		{
			spd.y+=m_type->gravity;

			for ( vector< NRTimer >::iterator t = timer.begin(); t != timer.end(); t++)
			{
				(*t).count--;
				if ( (*t).count < 0 )
				{
					(*t).m_tEvent->event->run(this);
					(*t).reset();
				}
			}

			if ( m_type->acceleration )
			{
				if ( spd.dotProduct(angleVec(m_angle,1)) < m_type->maxSpeed || m_type->maxSpeed < 0)
				spd+= angleVec(m_angle,m_type->acceleration);
			}

			spd*=m_type->damping;

			if ( abs(m_angleSpeed) < m_type->angularFriction ) m_angleSpeed = 0;
			else if ( m_angleSpeed < 0 ) m_angleSpeed += m_type->angularFriction;
			else m_angleSpeed -= m_type->angularFriction;

			m_angle += m_angleSpeed;
			while ( m_angle > 360 ) m_angle -= 360;
			while ( m_angle < 0 ) m_angle += 360;


			if ( !deleteMe ) pos = pos + spd;
			else break;
			if ( m_animator ) m_animator->tick();
		}
		*/
	}
}

Angle NinjaRope::getAngle() {
	return m_angle;
}

void NinjaRope::addAngleSpeed(AngleDiff speed) {
	m_angleSpeed += speed;
}

int NinjaRope::getColour() {
	return m_type->colour;
}

Vec &NinjaRope::getPosReference() {
	return pos;
}

#ifndef DEDSERV
void NinjaRope::draw(Viewport *viewport) {
	BITMAP *where = viewport->dest;
	IVec rPos = viewport->convertCoords(IVec(pos));
	if (active) {
		if (!m_sprite)
			putpixel(where, rPos.x, rPos.y, m_type->colour);
		else {
			m_sprite->getSprite(m_animator->getFrame(), m_angle)->draw(where, rPos.x, rPos.y);
		}
		if (m_type->distortion) {
			m_type->distortion->apply(where, rPos.x, rPos.y, m_type->distortMagnitude);
		}
	}
}
#endif
