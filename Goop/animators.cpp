#include "animators.h"

#include "base_animator.h"
#include "sprite.h"

#include <allegro.h>

AnimPingPong::AnimPingPong( Sprite* sprite, int duration )
{
	m_totalFrames = sprite->getFramesWidth();
	m_duration = duration;
	m_animPos = 0;
	m_currentDir = 1;
}

AnimPingPong::~AnimPingPong()
{
}

int AnimPingPong::getFrame()
{
	return (m_animPos * m_totalFrames) / m_duration;
}

void AnimPingPong::tick()
{
	if (m_currentDir == 1)
	{
		m_animPos++;
		if( m_animPos >= m_duration )
		{
			m_currentDir = -1;
			m_animPos = m_duration - 1;
		}
	}
	else
	{
		m_animPos--;
		if ( m_animPos <= 0 )
		{
			m_currentDir = 1;
			m_animPos = 0;
		}
	}
}

void AnimPingPong::reset()
{
	m_animPos = 0;
	m_currentDir = 1;
}

AnimLoopRight::AnimLoopRight( Sprite* sprite, int duration )
{
	m_totalFrames = sprite->getFramesWidth();
	m_duration = duration;
	m_animPos = 0;
}

AnimLoopRight::~AnimLoopRight()
{
}

int AnimLoopRight::getFrame()
{
	return (m_animPos * m_totalFrames) / m_duration;
}

void AnimLoopRight::tick()
{
	m_animPos++;
	if( m_animPos >= m_duration )
	{
		m_animPos = 0;
	}
}

void AnimLoopRight::reset()
{
	m_animPos = 0;
}
