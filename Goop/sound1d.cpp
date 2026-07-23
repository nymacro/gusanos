#ifndef DEDSERV

#include "sound1d.h"

#include "sfx.h"
#include "resource_list.h"
#include "util/math_func.h"

#include <string>
#include <iostream>
#include <algorithm>
#include <boost/filesystem/path.hpp>
namespace fs = boost::filesystem;

#include "allegro_compat.h"
//#include "text.h"

#include "fmod_compat.h"

using namespace std;

ResourceList<Sound1D> sound1DList;

Sound1D::Sound1D()
{
	m_sound = NULL;
}

Sound1D::~Sound1D()
{
	if ( m_sound )  FSOUND_Sample_Free( m_sound );
}

bool Sound1D::load(fs::path const& filename)
{	
	//cerr << "Loading sound: " << filename.string() << endl;
	m_sound = FSOUND_Sample_Load( FSOUND_FREE, filename.string().c_str(), FSOUND_2D | FSOUND_FORCEMONO, 0, 0 );
	if ( m_sound )
	{
		return true;
	}
	return false;
}

void Sound1D::play(float volume,float pitch, float volumeVariation, float pitchVariation)
{
	if( m_sound ) 
	{
		int chan = FSOUND_PlaySoundEx(FSOUND_FREE, m_sound, 0, 1);
		if ( chan != -1 )
		{
			float rndPitch = pitch + midrnd()*pitchVariation;
			FSOUND_SetFrequency(chan, static_cast<int>(FSOUND_GetFrequency(chan) * rndPitch) );
			
			float rndVolume = volume + midrnd()*volumeVariation;
			rndVolume = std::clamp(rndVolume, 0.0f, 1.0f);
			FSOUND_SetVolume(chan, static_cast<int>(FSOUND_GetVolume(chan)*rndVolume) );
			
			FSOUND_SetLoopMode( chan, FSOUND_LOOP_OFF );
			
			FSOUND_SetPaused(chan, 0);
		}
	}
}

#endif
