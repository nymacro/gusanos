// FMOD compatibility stubs for Gusanos SDL3 migration
// Replaces the missing FMOD 3.74 library with no-op stubs so the code compiles.
// Full SDL3_mixer migration will happen in a later step.

#ifndef FMOD_COMPAT_H
#define FMOD_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

// Basic types
typedef struct FSOUND_SAMPLE {} FSOUND_SAMPLE;

// Constants
#define FSOUND_FREE       (-1)
#define FSOUND_UNMANAGED   (-2)
#define FSOUND_LOOP_OFF    0
#define FSOUND_LOOP_NORMAL 1
#define FSOUND_2D          0
#define FSOUND_NORMAL      0
#define FSOUND_HW3D        (1 << 2)
#define FSOUND_FORCEMONO   (1 << 8)
#define FSOUND_ALLPCM      0

// Output types
#define FSOUND_OUTPUT_NOSOUND 0
#define FSOUND_OUTPUT_WINMM   1
#define FSOUND_OUTPUT_DSOUND  2
#define FSOUND_OUTPUT_A3D     3
#define FSOUND_OUTPUT_OSS     4
#define FSOUND_OUTPUT_ESD     5
#define FSOUND_OUTPUT_ALSA    6
#define FSOUND_OUTPUT_ASIO    7
#define FSOUND_OUTPUT_XBOX    8
#define FSOUND_OUTPUT_PS2     9
#define FSOUND_OUTPUT_MAC     10
#define FSOUND_OUTPUT_GC      11

// Function stubs — all return 0 or similar no-op values
int FSOUND_Init(int mixrate, int maxsoftwarechannels, unsigned int flags);
int FSOUND_Close();
int FSOUND_SetOutput(int output);
int FSOUND_SetDriver(int driver);
int FSOUND_GetNumDrivers();
int FSOUND_GetDriver();
const char* FSOUND_GetDriverName(int driver);
int FSOUND_SetSFXMasterVolume(int volume);
int FSOUND_Update();

// Sample management
FSOUND_SAMPLE* FSOUND_Sample_Load(int index, const char* name, unsigned int mode, int memlength, int offset);
int FSOUND_Sample_Free(FSOUND_SAMPLE* s);

// Playback
int FSOUND_PlaySoundEx(int channel, FSOUND_SAMPLE* s, void* dsp, int startpaused);
int FSOUND_IsPlaying(int channel);
int FSOUND_SetPaused(int channel, int paused);
int FSOUND_SetFrequency(int channel, int freq);
int FSOUND_GetFrequency(int channel);
int FSOUND_SetVolume(int channel, int vol);
int FSOUND_GetVolume(int channel);
int FSOUND_SetLoopMode(int channel, int mode);
int FSOUND_StopSound(int channel);

// 3D audio
int FSOUND_3D_SetDistanceFactor(float factor);
int FSOUND_3D_SetRolloffFactor(float factor);
int FSOUND_3D_SetAttributes(int channel, const float* pos, const float* vel);
int FSOUND_3D_SetMinMaxDistance(int channel, float min, float max);
int FSOUND_3D_Listener_SetCurrent(int index, int num);
int FSOUND_3D_Listener_SetAttributes(const float* pos, const float* vel, float fx, float fy, float fz, float ux, float uy, float uz);

#ifdef __cplusplus
}
#endif

#endif // FMOD_COMPAT_H