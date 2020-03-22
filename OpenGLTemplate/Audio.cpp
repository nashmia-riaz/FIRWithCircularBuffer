#include "Audio.h"
#include <math.h>

#pragma comment(lib, "lib/fmod_vc.lib")

/*
I've made these two functions non-member functions
*/

// Check for error
void FmodErrorCheck(FMOD_RESULT result)
{
	if (result != FMOD_OK) {
		const char *errorString = FMOD_ErrorString(result);
		// MessageBox(NULL, errorString, "FMOD Error", MB_OK);
		// Warning: error message commented out -- if headphones not plugged into computer in lab, error occurs
	}
}

float* ApplyZeroPadding(float* data, float* filter)
{
	//p = ceil((f-1) / 2)
	int filterSize = sizeof(filter) / sizeof(float);
	int p = ceil((filterSize - 1) / 2);

	data = new float[p * 2]; //allocate data for zero padding

	//calculate data size
	float dataSize = sizeof(data) / sizeof(float);

	//shift all data ahead for zero padding
	for (int i = p; i < dataSize; i++) {
		data[i - p] = data[i];
	}

	//prepend zeros
	for (int i = 0; i < p; i++)
		data[i] = 0;

	//append zeros
	for (int i = dataSize - p; i < dataSize; i++) {
		data[i] = 0;
	}

	return data;
}


/*
	Callback called when DSP is created.   This implementation creates a structure which is attached to the dsp state's 'plugindata' member.
*/FMOD_RESULT F_CALLBACK myDSPCreateCallback(FMOD_DSP_STATE* dsp_state)
{
	unsigned int blocksize = 256;
	FMOD_RESULT result;

	result = dsp_state->functions->getblocksize(dsp_state, &blocksize);
	FmodErrorCheck(result);

	mydsp_data_t* data = (mydsp_data_t*)calloc(sizeof(mydsp_data_t), 1);
	if (!data)
	{
		return FMOD_ERR_MEMORY;
	}
	dsp_state->plugindata = data;
	data->volume_linear = 1.0f;
	data->sample_count = blocksize;

	data->circ_buffer = (float*)malloc(blocksize * 8 * sizeof(float));      // *8 = maximum size allowing room for 7.1.   Could ask dsp_state->functions->getspeakermode for the right speakermode to get real speaker count.
	if (!data->circ_buffer)
	{
		return FMOD_ERR_MEMORY;
	}

	return FMOD_OK;
}


// DSP callback
FMOD_RESULT F_CALLBACK DSPCallback(FMOD_DSP_STATE *dsp_state, float *inbuffer, float *outbuffer, unsigned int length, int inchannels, int *outchannels)
{
	mydsp_data_t* data = (mydsp_data_t*) dsp_state->plugindata;	//add data into our structure

	auto buffer_size = sizeof(*data->circ_buffer) / sizeof(float); 
	auto mean_length = buffer_size / inchannels;

	float filter[4] = { 0.25, 0.25, 0.25, 0.25 };

	inbuffer = ApplyZeroPadding(inbuffer, filter);

	if (buffer_size <= 0) return FMOD_ERR_MEMORY;

	for (unsigned int samp = 0; samp < length; samp++)	//run through sample length				
	{
		for (int chan = 0; chan < *outchannels; chan++)	//run through out channels length
		{
			// FIR Filter with 4 coefficients
			/*	
			int circ_write_pos = (data->sample_count * inchannels + chan) % buffer_size;
			data->circ_buffer[circ_write_pos] = inbuffer[samp + inchannels + chan];
			outbuffer[samp * *outchannels + chan] = (
				inbuffer[samp * *outchannels + chan] +
				data->circ_buffer[(data->sample_count - 1 * inchannels + chan) % buffer_size] +
				data->circ_buffer[(data->sample_count - 2 * inchannels + chan) % buffer_size] + 
				data->circ_buffer[(data->sample_count - 3 * inchannels + chan) % buffer_size]
				) / 4;
			*/

			// FIR Filter by change buffer size
			int circ_write_pos = (data->sample_count * inchannels + chan) % buffer_size;
			data->circ_buffer[circ_write_pos] = inbuffer[samp * inchannels + chan];
			outbuffer[samp * *outchannels + chan] = 0;
			for (int i = 0; i < sizeof(filter)/sizeof(float); i++) {
				outbuffer[samp * *outchannels + chan] +=
					data->circ_buffer[(data->sample_count - i * inchannels + chan) % buffer_size] * filter[i]; 
			}
		}
		data->sample_count++;
	}

	return FMOD_OK;
}

CAudio::CAudio()
{}

CAudio::~CAudio()
{}

bool CAudio::Initialise()
{
	// Create an FMOD system
	result = FMOD::System_Create(&m_FmodSystem);
	FmodErrorCheck(result);
	if (result != FMOD_OK) 
		return false;

	// Initialise the system
	result = m_FmodSystem->init(32, FMOD_INIT_NORMAL, 0);
	FmodErrorCheck(result);
	if (result != FMOD_OK) 
		return false;



	// Create the DSP effect
	{
		FMOD_DSP_DESCRIPTION dspdesc;
		memset(&dspdesc, 0, sizeof(dspdesc));

		strncpy_s(dspdesc.name, "My first DSP unit", sizeof(dspdesc.name));
		dspdesc.numinputbuffers = 1;
		dspdesc.numoutputbuffers = 1;
		dspdesc.read = DSPCallback;
		dspdesc.create = myDSPCreateCallback;

		result = m_FmodSystem->createDSP(&dspdesc, &m_dsp);
		FmodErrorCheck(result);

		if (result != FMOD_OK)
			return false;
	}

	return true;
	
}

// Load an event sound
bool CAudio::LoadEventSound(char *filename)
{
	result = m_FmodSystem->createSound(filename, FMOD_LOOP_OFF, 0, &m_eventSound);
	FmodErrorCheck(result);
	if (result != FMOD_OK) 
		return false;

	return true;
	

}

// Play an event sound
bool CAudio::PlayEventSound()
{
	result = m_FmodSystem->playSound(m_eventSound, NULL, false, NULL);
	FmodErrorCheck(result);
	if (result != FMOD_OK)
		return false;
	return true;
}


// Load a music stream
bool CAudio::LoadMusicStream(char *filename)
{
	result = m_FmodSystem->createStream(filename, NULL | FMOD_LOOP_NORMAL, 0, &m_music);
	FmodErrorCheck(result);

	if (result != FMOD_OK) 
		return false;

	return true;
	

}

// Play a music stream
bool CAudio::PlayMusicStream()
{
	result = m_FmodSystem->playSound(m_music, NULL, false, &m_musicChannel);
	FmodErrorCheck(result);

	if (result != FMOD_OK)
		return false;

	m_musicChannel->addDSP(0, m_dsp);

	return true;
}

void CAudio::Update()
{
	m_FmodSystem->update();
}
