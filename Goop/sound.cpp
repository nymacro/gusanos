#ifndef DEDSERV

#include "sound.h"

#include "sfx.h"
#include "resource_list.h"
#include "util/math_func.h"
#include "base_object.h"

#include <string>
#include <iostream>
#include <algorithm>
#include <boost/filesystem/path.hpp>
namespace fs = boost::filesystem;

#include "allegro_compat.h"
//#include "text.h"

#include "fmod_compat.h"

using namespace std;

ResourceList<Sound> soundList;

Sound::Sound()
{
	m_sound = NULL;
}

Sound::~Sound()
{
	if ( m_sound )  FSOUND_Sample_Free( m_sound );
}

bool Sound::load(fs::path const& filename)
{	
	//cerr << "Loading sound: " << filename.string() << endl;
	m_sound = FSOUND_Sample_Load( FSOUND_FREE, filename.string().c_str(), FSOUND_HW3D | FSOUND_FORCEMONO, 0, 0 );
	if ( m_sound )
	{
		return true;
	}
	return false;
}

void Sound::play(float volume,float pitch, float volumeVariation, float pitchVariation)
{
	if( m_sound ) 
	{
		int chan = FSOUND_PlaySoundEx(FSOUND_FREE, m_sound, 0, 1);
		if ( chan != -1 )
		{
			float rndPitch = pitch + rnd()*pitchVariation - pitchVariation / 2;
			FSOUND_SetFrequency(chan, static_cast<int>(FSOUND_GetFrequency(chan) * rndPitch) );
			
			float rndVolume = volume + rnd()*volumeVariation - volumeVariation / 2;
			rndVolume = std::clamp(rndVolume, 0.0f, 1.0f);
			FSOUND_SetVolume(chan, static_cast<int>(FSOUND_GetVolume(chan)*rndVolume) );
			
			FSOUND_SetLoopMode( chan, FSOUND_LOOP_OFF );
			
			FSOUND_SetPaused(chan, 0);
		}
	}
}

void Sound::play2D(const Vec& pos, float loudness, float pitch, float pitchVariation)
{
	if( m_sound ) 
	{
		int chan = FSOUND_PlaySoundEx(FSOUND_FREE, m_sound, NULL, 1);
		if ( chan != -1 )
		{
			float _pos[3] = { pos.x, pos.y, 0 };
			FSOUND_3D_SetAttributes(chan, _pos, NULL);
			
			float rndPitch = pitch + rnd()*pitchVariation - pitchVariation / 2;
			FSOUND_SetFrequency(chan, static_cast<int>(FSOUND_GetFrequency(chan) * rndPitch) );
			
			FSOUND_3D_SetMinMaxDistance(chan, loudness, 10000.0f);
			
			FSOUND_SetLoopMode( chan, FSOUND_LOOP_OFF );
			
			FSOUND_SetPaused(chan, 0);
		}
	}
}

void Sound::play2D(BaseObject* obj, float loudness, float pitch, float pitchVariation)
{
	if( m_sound ) 
	{
		int chan = FSOUND_PlaySoundEx(FSOUND_FREE, m_sound, NULL, 1);
		if ( chan != -1 )
		{
			float pos[3] = { obj->pos.x, obj->pos.y, 0 };

			FSOUND_3D_SetAttributes(chan, pos, NULL);
			
			sfx.setChanObject( chan, obj );
			
			float rndPitch = pitch + rnd()*pitchVariation - pitchVariation / 2;
			FSOUND_SetFrequency(chan, static_cast<int>(FSOUND_GetFrequency(chan) * rndPitch) );
			
			FSOUND_3D_SetMinMaxDistance(chan, loudness, 10000.0f);
			
			FSOUND_SetLoopMode( chan, FSOUND_LOOP_OFF );
			
			FSOUND_SetPaused(chan, 0);
		}
	}
}

#endif
