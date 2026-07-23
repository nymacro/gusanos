// SDL3_mixer backend for Gusanos audio
// Replaces FMOD 3.74 stubs with real SDL3_mixer v3 calls.

#include "fmod_compat.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cmath>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static MIX_Mixer *s_mixer = nullptr;

// Track pool: track_pool[i] is the MIX_Track for "channel i".
// Channels are the integer handles returned by FSOUND_PlaySoundEx.
static constexpr int MAX_CHANNELS = 64;
static MIX_Track *track_pool[MAX_CHANNELS] = {};

// Per-channel "original frequency" cache so FSOUND_GetFrequency can return
// something sensible.  Stored as the sample rate of the audio assigned to
// the track.
static int chan_orig_freq[MAX_CHANNELS] = {};

// Per-channel loop-mode tracking (set by FSOUND_SetLoopMode, used in
// FSOUND_PlaySoundEx to set loops).
static int chan_loop_mode[MAX_CHANNELS] = {};

// Per-channel gain stored as 0-255 FMOD-style volume, cached so
// FSOUND_GetVolume works without round-tripping through float.
static int chan_volume[MAX_CHANNELS] = {255};

// Listener position in world space.  Sound positions passed to
// FSOUND_3D_SetAttributes are transformed to listener-relative coords
// before being passed to SDL3_mixer (which has no listener concept).
static float listener_pos[3] = {0, 0, 0};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static MIX_Track *get_or_create_track(int channel)
{
	if (channel < 0 || channel >= MAX_CHANNELS)
		return nullptr;

	if (!track_pool[channel]) {
		track_pool[channel] = MIX_CreateTrack(s_mixer);
		if (!track_pool[channel]) {
			SDL_Log("FSOUND compat: failed to create track %d: %s",
				channel, SDL_GetError());
			return nullptr;
		}
		chan_volume[channel] = 255;
		chan_loop_mode[channel] = FSOUND_LOOP_OFF;
	}
	return track_pool[channel];
}

static int find_free_channel()
{
	for (int i = 0; i < MAX_CHANNELS; ++i) {
		if (!track_pool[i] || !MIX_TrackPlaying(track_pool[i]))
			return i;
	}
	// All channels busy — return first one anyway (will stop it).
	return 0;
}

// Convert FMOD 0-255 volume to SDL3_mixer linear gain.
static float vol_to_gain(int vol)
{
	return vol / 255.0f;
}

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------

int FSOUND_Init(int mixrate, int maxsoftwarechannels, unsigned int flags)
{
	(void)maxsoftwarechannels;
	(void)flags;

	// Ensure SDL audio subsystem is initialised before creating the mixer.
	// SDL_Init is ref-counted; safe to call again if already initialised.
	if (!SDL_Init(SDL_INIT_AUDIO)) {
		SDL_Log("FSOUND compat: SDL_Init(SDL_INIT_AUDIO) failed: %s", SDL_GetError());
		return 0;
	}

	if (!MIX_Init()) {
		SDL_Log("FSOUND compat: MIX_Init() failed: %s", SDL_GetError());
		return 0;
	}

	SDL_AudioSpec spec;
	std::memset(&spec, 0, sizeof(spec));
	spec.freq = (mixrate > 0) ? mixrate : 44100;
	spec.format = SDL_AUDIO_S16;
	spec.channels = 2;

	s_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
	if (!s_mixer) {
		SDL_Log("FSOUND compat: MIX_CreateMixerDevice() failed: %s", SDL_GetError());
		MIX_Quit();
		return 0;
	}

	// Ensure default gain is unchanged.
	MIX_SetMixerGain(s_mixer, 1.0f);

	SDL_Log("FSOUND compat: SDL3_mixer initialised (%d Hz, %d ch)", spec.freq, spec.channels);
	return 1;
}

int FSOUND_Close()
{
	// Stop and destroy all tracks.
	for (int i = 0; i < MAX_CHANNELS; ++i) {
		if (track_pool[i]) {
			MIX_StopTrack(track_pool[i], 0);
			MIX_DestroyTrack(track_pool[i]);
			track_pool[i] = nullptr;
		}
		chan_orig_freq[i] = 0;
		chan_loop_mode[i] = FSOUND_LOOP_OFF;
		chan_volume[i] = 255;
	}

	if (s_mixer) {
		MIX_DestroyMixer(s_mixer);
		s_mixer = nullptr;
	}

	MIX_Quit();
	return 1;
}

int FSOUND_SetOutput(int output)
{
	(void)output;
	// SDL3_mixer uses SDL audio devices directly — no output mode switching.
	return 1;
}

int FSOUND_SetDriver(int driver)
{
	(void)driver;
	return 1;
}

int FSOUND_GetNumDrivers()
{
	return 1;
}

int FSOUND_GetDriver()
{
	return 0;
}

const char* FSOUND_GetDriverName(int driver)
{
	(void)driver;
	return "SDL3_mixer";
}

int FSOUND_SetSFXMasterVolume(int volume)
{
	if (!s_mixer) return 0;
	int v = std::clamp(volume, 0, 255);
	MIX_SetMixerGain(s_mixer, vol_to_gain(v));
	return 1;
}

int FSOUND_Update()
{
	// SDL3_mixer manages mixing in its own audio thread — no per-frame
	// update needed.  Listener position updates (previously done here)
	// are handled by FSOUND_3D_Listener_SetAttributes which is now a
	// no-op (SDL3_mixer does 3D per-track).
	return 1;
}

// ---------------------------------------------------------------------------
// Sample management
// ---------------------------------------------------------------------------

FSOUND_SAMPLE* FSOUND_Sample_Load(int index, const char* name, unsigned int mode, int memlength, int offset)
{
	(void)index;
	(void)mode;
	(void)memlength;
	(void)offset;

	if (!s_mixer || !name) return nullptr;

	// Allocate host struct.
	FSOUND_SAMPLE *s = (FSOUND_SAMPLE*)std::malloc(sizeof(FSOUND_SAMPLE));
	if (!s) return nullptr;
	s->audio = nullptr;

	// predecode=true — decode fully into RAM (matches FSOUND_Sample_Load
	// semantics where the whole sample is kept in memory).
	s->audio = MIX_LoadAudio(s_mixer, name, true);
	if (!s->audio) {
		std::free(s);
		return nullptr;
	}

	return s;
}

int FSOUND_Sample_Free(FSOUND_SAMPLE* s)
{
	if (!s) return 0;

	if (s->audio) {
		// Stop any track still using this audio before freeing.
		if (s_mixer) {
			for (int i = 0; i < MAX_CHANNELS; ++i) {
				if (track_pool[i] && MIX_GetTrackAudio(track_pool[i]) == s->audio) {
					MIX_StopTrack(track_pool[i], 0);
				}
			}
		}
		MIX_DestroyAudio(s->audio);
	}

	std::memset(s, 0, sizeof(*s));
	std::free(s);
	return 1;
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

int FSOUND_PlaySoundEx(int channel, FSOUND_SAMPLE* s, void* dsp, int startpaused)
{
	(void)dsp;

	if (!s_mixer || !s || !s->audio) return -1;

	if (channel == FSOUND_FREE)
		channel = find_free_channel();

	MIX_Track *track = get_or_create_track(channel);
	if (!track) return -1;

	// Each new playback starts at full channel volume; callers set it afterwards.
	chan_volume[channel] = 255;

	// Stop anything already on this channel.
	if (MIX_TrackPlaying(track))
		MIX_StopTrack(track, 0);

	// Assign audio.
	if (!MIX_SetTrackAudio(track, s->audio))
		return -1;

	// Get original frequency from the audio spec.
	{
		SDL_AudioSpec spec;
		std::memset(&spec, 0, sizeof(spec));
		MIX_GetAudioFormat(s->audio, &spec);
		chan_orig_freq[channel] = spec.freq;
	}

	// Restore previously-set loop mode.
	int loops = (chan_loop_mode[channel] == FSOUND_LOOP_NORMAL) ? -1 : 0;
	if (loops == -1)
		MIX_SetTrackLoops(track, -1);

	// Restore volume.
	MIX_SetTrackGain(track, vol_to_gain(chan_volume[channel]));

	// Build play options.
	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_SetBooleanProperty(props, "start_paused", startpaused ? true : false);

	bool ok = MIX_PlayTrack(track, props);
	SDL_DestroyProperties(props);

	if (!ok) return -1;

	return channel;
}

int FSOUND_IsPlaying(int channel)
{
	if (channel < 0 || channel >= MAX_CHANNELS) return 0;
	if (!track_pool[channel]) return 0;
	return MIX_TrackPlaying(track_pool[channel]) ? 1 : 0;
}

int FSOUND_SetPaused(int channel, int paused)
{
	if (channel < 0 || channel >= MAX_CHANNELS) return 0;
	MIX_Track *track = track_pool[channel];
	if (!track) return 0;
	if (paused)
		MIX_PauseTrack(track);
	else
		MIX_ResumeTrack(track);
	return 1;
}

int FSOUND_SetFrequency(int channel, int freq)
{
	if (channel < 0 || channel >= MAX_CHANNELS) return 0;
	MIX_Track *track = track_pool[channel];
	if (!track) return 0;
	float ratio = (chan_orig_freq[channel] > 0)
		? (float)freq / (float)chan_orig_freq[channel]
		: 1.0f;
	MIX_SetTrackFrequencyRatio(track, ratio);
	return 1;
}

int FSOUND_GetFrequency(int channel)
{
	if (channel < 0 || channel >= MAX_CHANNELS) return 44100;
	MIX_Track *track = track_pool[channel];
	if (!track || chan_orig_freq[channel] <= 0) return 44100;
	float ratio = MIX_GetTrackFrequencyRatio(track);
	return (int)(chan_orig_freq[channel] * ratio);
}

int FSOUND_SetVolume(int channel, int vol)
{
	if (channel < 0 || channel >= MAX_CHANNELS) return 0;
	int v = std::clamp(vol, 0, 255);
	chan_volume[channel] = v;
	MIX_Track *track = track_pool[channel];
	if (track)
		MIX_SetTrackGain(track, vol_to_gain(v));
	return 1;
}

int FSOUND_GetVolume(int channel)
{
	if (channel < 0 || channel >= MAX_CHANNELS) return 0;
	return chan_volume[channel];
}

int FSOUND_SetLoopMode(int channel, int mode)
{
	if (channel < 0 || channel >= MAX_CHANNELS) return 0;
	chan_loop_mode[channel] = mode;
	MIX_Track *track = track_pool[channel];
	if (track) {
		// FSOUND_LOOP_OFF = 0 → 0 loops (play once).
		// FSOUND_LOOP_NORMAL = 1 → loop forever (-1 in SDL3_mixer).
		MIX_SetTrackLoops(track, (mode == FSOUND_LOOP_NORMAL) ? -1 : 0);
	}
	return 1;
}

int FSOUND_StopSound(int channel)
{
	if (channel < 0 || channel >= MAX_CHANNELS) return 0;
	MIX_Track *track = track_pool[channel];
	if (track)
		MIX_StopTrack(track, 0);
	return 1;
}

// ---------------------------------------------------------------------------
// 3D audio
// ---------------------------------------------------------------------------
// SDL3_mixer does 3D positioning per-track via MIX_SetTrack3DPosition.
// There is no global distance/rolloff factor nor a listener concept; these
// are no-ops.  Min/max distance is also not supported — SDL3_mixer uses a
// fixed distance model.

int FSOUND_3D_SetDistanceFactor(float factor)
{
	(void)factor;
	return 1;
}

int FSOUND_3D_SetRolloffFactor(float factor)
{
	(void)factor;
	return 1;
}

int FSOUND_3D_SetAttributes(int channel, const float* pos, const float* vel)
{
	(void)vel;
	if (channel < 0 || channel >= MAX_CHANNELS) return 0;
	MIX_Track *track = track_pool[channel];
	if (!track) return 0;

	if (pos) {
		MIX_Point3D p;
		p.x = pos[0] - listener_pos[0];
		p.y = pos[1] - listener_pos[1];
		p.z = pos[2] - listener_pos[2];
		MIX_SetTrack3DPosition(track, &p);
	} else {
		MIX_SetTrack3DPosition(track, nullptr);
	}
	return 1;
}

int FSOUND_3D_SetMinMaxDistance(int channel, float min, float max)
{
	(void)channel;
	(void)min;
	(void)max;
	// SDL3_mixer has a built-in distance model with no configurable
	// min/max — ignored.
	return 1;
}

int FSOUND_3D_Listener_SetCurrent(int index, int num)
{
	(void)index;
	(void)num;
	// No listener concept in SDL3_mixer.
	return 1;
}

int FSOUND_3D_Listener_SetAttributes(const float* pos, const float* vel,
	float fx, float fy, float fz, float ux, float uy, float uz)
{
	(void)vel;
	(void)fx; (void)fy; (void)fz;
	(void)ux; (void)uy; (void)uz;
	if (pos) {
		listener_pos[0] = pos[0];
		listener_pos[1] = pos[1];
		listener_pos[2] = pos[2];
	}
	return 1;
}