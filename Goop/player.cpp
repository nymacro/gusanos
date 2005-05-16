#include "player.h"
#include "player_options.h"
#include "worm.h"
#include "viewport.h"
#include "ninjarope.h"

#include <allegro.h>

using namespace std;

Player::Player(PlayerOptions* options) : BasePlayer()
{
	aimingUp = false;
	aimingDown = false;
	changing = false;
	jumping = false;
	
	m_options = options;

	m_viewport = NULL;
}

Player::~Player()
{
	if ( m_viewport )
	{
		delete m_viewport;
	}
}

void Player::assignViewport(Viewport* viewport)
{
	m_viewport = viewport;
}

void Player::subThink()
{
	if ( m_worm )
	{
		if ( m_viewport ) m_viewport->interpolateTo(m_worm->getRenderPos(), m_options->viewportFollowFactor);
		
		if(changing && m_worm->getNinjaRopeObj()->active)
		{
			if(aimingUp)
			{
				m_worm->addRopeLength(-m_options->ropeAdjustSpeed);
			}
			if(aimingDown)
			{
				m_worm->addRopeLength(m_options->ropeAdjustSpeed);
			}
		}
		else
		{
			
			if (aimingUp && m_worm->aimSpeed > -m_options->aimMaxSpeed) 
			{
				m_worm->addAimSpeed(-m_options->aimAcceleration);
			}
			// No "else if" since we want to support precision aiming
			if (aimingDown && m_worm->aimSpeed < m_options->aimMaxSpeed)
			{
				m_worm->addAimSpeed(m_options->aimAcceleration);
			}
		}
		
		if(!aimingDown && !aimingUp)
		{
			// I placed this here since BaseWorm doesn't have access to aiming flags
			m_worm->aimSpeed *= m_options->aimFriction;
		}
	}
}

void Player::render()
{
	if ( m_viewport ) m_viewport->render();
}

void Player::actionStart ( Actions action )
{
	switch (action)
	{
		case LEFT:
		{
			if ( m_worm )
			{
				if(changing)
				{
					m_worm -> actionStart(Worm::CHANGELEFT);
				}
				else
					BasePlayer::baseActionStart(BasePlayer::LEFT);
			}
		}
		break;
		
		case RIGHT:
		{
			if ( m_worm )
			{
				if(changing)
				{
					m_worm -> actionStart(Worm::CHANGERIGHT);
				}
				else
					BasePlayer::baseActionStart(BasePlayer::RIGHT);
			}
		}
		break;
		
		case FIRE:
		{
			if ( m_worm )
			{
				if(!changing)
					m_worm -> actionStart(Worm::FIRE);
			}
		}
		break;
		
		case JUMP:
		{
			if ( m_worm )
			{
				if (changing)
				{
					BasePlayer::baseActionStart(BasePlayer::NINJAROPE);
				}
				else
				{
					BasePlayer::baseActionStart(BasePlayer::JUMP);
					BasePlayer::baseActionStop(BasePlayer::NINJAROPE);
				}
				
				jumping = true;
			}
		}
		break;
		
		case UP:
		{
			if ( m_worm )
			{
				aimingUp = true;
			}
		}
		break;
		
		case DOWN:
		{
			if ( m_worm )
			{
				aimingDown = true;
			}
		}
		break;

		case CHANGE:
		{
			if ( m_worm )
			{
				if (jumping)
				{
					BasePlayer::baseActionStart(BasePlayer::NINJAROPE);
					jumping = false;
				}
				else
				{
					m_worm->actionStart(Worm::CHANGEWEAPON);
					m_worm->actionStop(Worm::FIRE); //TODO: Stop secondary fire also
					
					// Stop any movement
					m_worm->actionStop(Worm::MOVELEFT);
					m_worm->actionStop(Worm::MOVERIGHT);
					
				}
				
				changing = true;
			}
		}
		break;
	}
}

void Player::actionStop ( Actions action )
{
	switch (action)
	{
		case LEFT:
		{
			if ( m_worm )
			{
				BasePlayer::baseActionStop(BasePlayer::LEFT);
			}
		}
		break;
		
		case RIGHT:
		{
			if ( m_worm )
			{
				BasePlayer::baseActionStop(BasePlayer::RIGHT);
			}
		}
		break;
		
		case FIRE:
		{
			if ( m_worm )
			{
				m_worm -> actionStop(Worm::FIRE);
			}
		}
		break;
		
		case JUMP:
		{
			if ( m_worm )
			{
				BasePlayer::baseActionStop(BasePlayer::JUMP);
				jumping = false;
			}
		}
		break;
		
		case UP:
		{
			if ( m_worm )
			{
				aimingUp = false;
			}
		}
		break;
		
		case DOWN:
		{
			if ( m_worm )
			{
				aimingDown = false;
			}
		}
		break;
		
		case CHANGE:
		{
			if ( m_worm )
			{
				m_worm->actionStop(Worm::CHANGEWEAPON);

				changing = false;
			}
		}
		break;

	}
}




