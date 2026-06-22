#include "fmod_compat.h"

int FSOUND_Init(int mixrate, int maxsoftwarechannels, unsigned int flags) { return 1; }
int FSOUND_Close() { return 0; }
int FSOUND_SetOutput(int output) { return 1; }
int FSOUND_SetDriver(int driver) { return 1; }
int FSOUND_GetNumDrivers() { return 1; }
int FSOUND_GetDriver() { return 0; }
const char* FSOUND_GetDriverName(int driver) { return "stub"; }
int FSOUND_SetSFXMasterVolume(int volume) { return 1; }
int FSOUND_Update() { return 1; }

FSOUND_SAMPLE* FSOUND_Sample_Load(int index, const char* name, unsigned int mode, int memlength, int offset) { return (FSOUND_SAMPLE*)1; }
int FSOUND_Sample_Free(FSOUND_SAMPLE* s) { return 0; }

int FSOUND_PlaySoundEx(int channel, FSOUND_SAMPLE* s, void* dsp, int startpaused) { return 1; }
int FSOUND_IsPlaying(int channel) { return 0; }
int FSOUND_SetPaused(int channel, int paused) { return 1; }
int FSOUND_SetFrequency(int channel, int freq) { return 1; }
int FSOUND_GetFrequency(int channel) { return 44100; }
int FSOUND_SetVolume(int channel, int vol) { return 1; }
int FSOUND_GetVolume(int channel) { return 255; }
int FSOUND_SetLoopMode(int channel, int mode) { return 1; }
int FSOUND_StopSound(int channel) { return 0; }

int FSOUND_3D_SetDistanceFactor(float factor) { return 1; }
int FSOUND_3D_SetRolloffFactor(float factor) { return 1; }
int FSOUND_3D_SetAttributes(int channel, const float* pos, const float* vel) { return 1; }
int FSOUND_3D_SetMinMaxDistance(int channel, float min, float max) { return 1; }
int FSOUND_3D_Listener_SetCurrent(int index, int num) { return 1; }
int FSOUND_3D_Listener_SetAttributes(const float* pos, const float* vel, float fx, float fy, float fz, float ux, float uy, float uz) { return 1; }