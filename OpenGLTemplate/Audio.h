#pragma once
#include <windows.h>									// Header File For The Windows Library
#include "./include/fmod_studio/fmod.hpp"
#include "./include/fmod_studio/fmod_errors.h"

class CAudio
{
public:
	CAudio();
	~CAudio();
	bool Initialise();
	bool LoadEventSound(char *filename);
	bool PlayEventSound();
	bool LoadMusicStream(char *filename);
	bool PlayMusicStream();
	void FilterSwitch();	
	void Update(float dt);
private:

	FMOD_RESULT result;
	FMOD::System *m_FmodSystem;	// the global variable for talking to FMOD
	FMOD::Sound *m_eventSound;

	FMOD::Sound *m_music;
	FMOD::Channel *m_musicChannel;
	FMOD::ChannelGroup* m_mastergroup;
	FMOD::DSP *m_dsp;

	bool bypass;

};
typedef struct
{
	float* circ_buffer;
	float volume_linear;
	float speed_percent;
	int   sample_count;
	int   channels;

	float* b_filter1;
	float* b_filter2;

} mydsp_data_t;