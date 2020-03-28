#include "Audio.h"
#include <math.h>
#include <cstdio>

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

/*Function that applied zero padding to a buffer, based on filter and it's size*/
float* ApplyZeroPadding(float* data, int filterSize)
{
	//to calculate zero padding: p = ceil((f-1) / 2)
	int dataSize = sizeof(*data) / sizeof(float);
	int p = ceil((filterSize - 1) / 2);
	float* zeroPaddedData = new float[dataSize + p * 2];

	//calculate data size
	int zeroPaddedDataSize = sizeof(*zeroPaddedData) / sizeof(float);

	//prepend zeros
	for (int i = 0; i < p; i++) {
		zeroPaddedData[i] = 0;
	}

	//copy over original data
	for (int i = p; i < zeroPaddedDataSize - p; i++) {
		zeroPaddedData[i] = data[i-p];
	}

	//append zeros
	for (int i = zeroPaddedDataSize - p; i < zeroPaddedDataSize; i++) {
		zeroPaddedData[i] = 0;
	}

	//set pointer to new data array
	data = zeroPaddedData;
	return zeroPaddedData;
}

/*
	Callback called when DSP is created.   
	This implementation creates a structure which is attached to the dsp state's 'plugindata' member.
*/FMOD_RESULT F_CALLBACK myDSPCreateCallback(FMOD_DSP_STATE* dsp_state)
{
	unsigned int blocksize = 256;  //size of sample
	FMOD_RESULT result;

	result = dsp_state->functions->getblocksize(dsp_state, &blocksize);
	FmodErrorCheck(result);

	mydsp_data_t* data = (mydsp_data_t*)calloc(sizeof(mydsp_data_t), 1);
	if (!data)
	{
		return FMOD_ERR_MEMORY;
	}
	//sets initial values to the fields in mydsp_data_t struct
	dsp_state->plugindata = data;
	data->volume_linear = 1.0f;
	data->speed_percent = 1.0f;
	data->sample_count = blocksize;

	/*the two filters and coefficients copied from signal.firls in python*/
	//array to hold coefficients (B1) of static filter1, needed for interpolation. 
	data->b_filter1 = { new float[21]{ -0.00349319,  0.00047716,  0.00459594,  0.00871522,  0.0126823,   0.01634645,
		0.01956573,  0.02221357,  0.02418469,  0.02540006,  0.02581071,  0.02540006,
		0.02418469,  0.02221357,  0.01956573,  0.01634645,  0.0126823,   0.00871522,
		0.00459594,  0.00047716, - 0.00349319} };
	//array to hold coefficients (B2) of static filter2, needed for interpolation
	data->b_filter2 = { new float[21] {-0.01911611, - 0.02526179, - 0.02772793, - 0.02595434, - 0.02006462, - 0.01086989,
		0.0002479,   0.01155558,  0.02125468,  0.02778399,  0.03008517,  0.02778399,
		0.02125468,  0.01155558,  0.0002479, - 0.01086989, - 0.02006462, - 0.02595434,
		- 0.02772793, - 0.02526179, - 0.01911611} };

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

	auto buffer_size = 11 * inchannels; //size of buffer
	auto mean_length = buffer_size / inchannels;

	//Filter coefficient interpolation, using the interpolation equation:
	// B(x) = (1-x)B1 + xB2
	// where 'x' is the real-time controllable value. In the game, it will be
	// controlled via the imposter horse's speed, speed_percent. 

	float mix_filt1[21];		//fraction of B1 depending on 'x'
	float mix_filt2[21];		//fraction of B2 depending on 'x'
	float mixed_filt[21];		//new filter resulting from interpolation of B1 and B2

	//interpolate the filter by multiplying with x and adding, using formula:
	//B(x) = (1-x) B1 + x B2
	for (int i = 0; i < 21; i++)
	{
		mix_filt1[i] = data->b_filter1[i] * (1 - data->speed_percent);
		mix_filt2[i] = data->b_filter2[i] * (data->speed_percent);
		mixed_filt[i] = mix_filt1[i] + mix_filt2[i];
	}

	ApplyZeroPadding(inbuffer, 21);

	if (buffer_size <= 0) return FMOD_ERR_MEMORY;

	for (unsigned int samp = 0; samp < length; samp++)	//run through sample length				
	{
		for (int chan = 0; chan < *outchannels; chan++)	//run through out channels length
		{
			// FIR Filter by change buffer size.
			//Convolution by multiplying filter with data and summing
			int circ_write_pos = (data->sample_count * inchannels + chan) % buffer_size;
			data->circ_buffer[circ_write_pos] = inbuffer[samp * inchannels + chan];
			outbuffer[samp * *outchannels + chan] = 0;
			for (int i = 0; i < 21; i++) {
				outbuffer[samp * *outchannels + chan] +=
					data->circ_buffer[(data->sample_count - i * inchannels + chan) % buffer_size] * mixed_filt[i]; 
			}
		}
		data->sample_count++;
	}

	return FMOD_OK;
}

/* Callback for release of DSP in FMOD */
FMOD_RESULT F_CALLBACK myDSPReleaseCallback(FMOD_DSP_STATE* dsp_state)
{
	if (dsp_state->plugindata)
	{
		mydsp_data_t* data = (mydsp_data_t*)dsp_state->plugindata;

		if (data->circ_buffer)
		{
			free(data->circ_buffer);
		}

		free(data);
	}

	return FMOD_OK;
}

/*DSP callback for setting parameters upon initialisation */
FMOD_RESULT F_CALLBACK myDSPGetParameterDataCallback(FMOD_DSP_STATE* dsp_state, int index, void** data, unsigned int* length, char*)
{
	if (index == 0)
	{
		unsigned int blocksize;
		FMOD_RESULT result;
		mydsp_data_t* mydata = (mydsp_data_t*)dsp_state->plugindata;

		result = dsp_state->functions->getblocksize(dsp_state, &blocksize);
		FmodErrorCheck(result);
		
		*data = (void*)mydata;
		*length = blocksize * 2 * sizeof(float);

		return FMOD_OK;
	}

	return FMOD_ERR_INVALID_PARAM;
}

//set the float parameter for 'speed_percent' from the mydsp_data_t struct
FMOD_RESULT F_CALLBACK myDSPSetParameterFloatCallback(FMOD_DSP_STATE* dsp_state, int index, float value)
{
	if (index == 1)
	{
		mydsp_data_t* mydata = (mydsp_data_t*)dsp_state->plugindata;

		mydata->speed_percent = value;

		return FMOD_OK;
	}

	return FMOD_ERR_INVALID_PARAM;
}

//get the float parameter for 'speed_percent' from the mydsp_data_t struct
FMOD_RESULT F_CALLBACK myDSPGetParameterFloatCallback(FMOD_DSP_STATE* dsp_state, int index, float* value, char* valstr)
{
	if (index == 1)
	{
		mydsp_data_t* mydata = (mydsp_data_t*)dsp_state->plugindata;

		*value = mydata->speed_percent;
		if (valstr)
		{
			sprintf(valstr, "%d", (int)((*value * 100.0f) + 0.5f));
		}

		return FMOD_OK;
	}

	return FMOD_ERR_INVALID_PARAM;
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
		FMOD_DSP_PARAMETER_DESC wavedata_desc;
		FMOD_DSP_PARAMETER_DESC speed_desc;
		FMOD_DSP_PARAMETER_DESC* paramdesc[2] =
		{
			&wavedata_desc,
			&speed_desc		//speed parameter that will be triggered by an in-game event, namely the player's speed
							//which will be directly correlated with the dynamically controlled FIR filter as the 'x' in B(x)
		};

		FMOD_DSP_INIT_PARAMDESC_DATA(wavedata_desc, "wave data", "", "wave data", FMOD_DSP_PARAMETER_DATA_TYPE_USER);
		FMOD_DSP_INIT_PARAMDESC_FLOAT(speed_desc, "speed", "%", "speed in percent", 0, 1, 1);

		//Setting up our custome DSP and it's callbacks
		strncpy_s(dspdesc.name, "My first DSP unit", sizeof(dspdesc.name));
		dspdesc.numinputbuffers = 1;
		dspdesc.numoutputbuffers = 1;
		dspdesc.read = DSPCallback;
		dspdesc.create = myDSPCreateCallback;
		dspdesc.release = myDSPReleaseCallback;
		dspdesc.getparameterdata = myDSPGetParameterDataCallback;
		dspdesc.setparameterfloat = myDSPSetParameterFloatCallback;
		dspdesc.getparameterfloat = myDSPGetParameterFloatCallback;
		dspdesc.numparameters = 2;
		dspdesc.paramdesc = paramdesc;

		result = m_FmodSystem->createDSP(&dspdesc, &m_dsp);
		FmodErrorCheck(result);

		if (result != FMOD_OK) return false;
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

void CAudio::Update(float dt)
{
	m_FmodSystem->update();

	result = m_dsp->getBypass(&bypass);

	FmodErrorCheck(result);

}

/*turns the filter on or off*/
void CAudio::FilterSwitch()
{
	if (bypass == true)
		result = m_dsp->setBypass(0);
	else if (bypass == false)
		result = m_dsp->setBypass(1);
	FmodErrorCheck(result);

}

//change speed percentage parameter. Taking input from Game.cpp
void CAudio::SpeedDown(float &speedpercent)
{
	result = m_dsp->getParameterFloat(1, &speedpercent, 0, 0);
	FmodErrorCheck(result);

	if (speedpercent > 0.0f)
	{
		speedpercent -= 0.05f;
	}

	result = m_dsp->setParameterFloat(1, speedpercent);
	FmodErrorCheck(result);

}

//change speed percentage parameter. Taking input from Game.cpp
void CAudio::SpeedUp(float &speedpercent)
{
	result = m_dsp->getParameterFloat(1, &speedpercent, 0, 0);
	FmodErrorCheck(result);

	if (speedpercent < 1.0f)
	{
		speedpercent += 0.05f;
	}

	result = m_dsp->setParameterFloat(1, speedpercent);
	FmodErrorCheck(result);

}