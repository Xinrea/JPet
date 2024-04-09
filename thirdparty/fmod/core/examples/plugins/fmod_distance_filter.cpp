/*==============================================================================
Distance Filter DSP Plugin Example
Copyright (c), Firelight Technologies Pty, Ltd 2004-2024.

This example shows how to create a distance filter DSP effect.
==============================================================================*/

#ifdef WIN32
    #define _CRT_SECURE_NO_WARNINGS
#endif

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fmod.hpp"

extern "C" 
{
    F_EXPORT FMOD_DSP_DESCRIPTION* F_CALL FMODGetDSPDescription();
}

const float FMOD_DISTANCE_FILTER_PARAM_MAX_DISTANCE_MIN     = 0.0f;
const float FMOD_DISTANCE_FILTER_PARAM_MAX_DISTANCE_MAX     = 10000.0f;
const float FMOD_DISTANCE_FILTER_PARAM_MAX_DISTANCE_DEFAULT = 20.0f;
const float FMOD_DISTANCE_FILTER_PARAM_BANDPASS_FREQUENCY_MIN     = 10.0f;
const float FMOD_DISTANCE_FILTER_PARAM_BANDPASS_FREQUENCY_MAX     = 22000.0f;
const float FMOD_DISTANCE_FILTER_PARAM_BANDPASS_FREQUENCY_DEFAULT = 1500.0f;

enum
{
    FMOD_DISTANCE_FILTER_MAX_DISTANCE,
    FMOD_DISTANCE_FILTER_BANDPASS_FREQUENCY,
    FMOD_DISTANCE_FILTER_3D_ATTRIBUTES,
    FMOD_DISTANCE_FILTER_NUM_PARAMETERS
};

FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspcreate       (FMOD_DSP_STATE *dsp_state);
FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dsprelease      (FMOD_DSP_STATE *dsp_state);
FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspreset        (FMOD_DSP_STATE *dsp_state);
FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspread         (FMOD_DSP_STATE *dsp_state, float *inbuffer, float *outbuffer, unsigned int length, int inchannels, int *outchannels);
FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspprocess      (FMOD_DSP_STATE *dsp_state, unsigned int length, const FMOD_DSP_BUFFER_ARRAY *inbufferarray, FMOD_DSP_BUFFER_ARRAY *outbufferarray, FMOD_BOOL inputsidle, FMOD_DSP_PROCESS_OPERATION op);
FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspsetparamfloat(FMOD_DSP_STATE *dsp_state, int index, float value);
FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspsetparamint  (FMOD_DSP_STATE *dsp_state, int index, int value);
FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspsetparambool (FMOD_DSP_STATE *dsp_state, int index, FMOD_BOOL value);
FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspsetparamdata (FMOD_DSP_STATE *dsp_state, int index, void *data, unsigned int length);
FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspgetparamfloat(FMOD_DSP_STATE *dsp_state, int index, float *value, char *valuestr);
FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspgetparamint  (FMOD_DSP_STATE *dsp_state, int index, int *value, char *valuestr);
FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspgetparambool (FMOD_DSP_STATE *dsp_state, int index, FMOD_BOOL *value, char *valuestr);
FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspgetparamdata (FMOD_DSP_STATE *dsp_state, int index, void **value, unsigned int *length, char *valuestr);
FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_shouldiprocess  (FMOD_DSP_STATE *dsp_state, FMOD_BOOL inputsidle, unsigned int length, FMOD_CHANNELMASK inmask, int inchannels, FMOD_SPEAKERMODE speakermode);

static FMOD_DSP_PARAMETER_DESC p_max_distance;
static FMOD_DSP_PARAMETER_DESC p_bandpass_frequency;
static FMOD_DSP_PARAMETER_DESC p_3d_attributes;

FMOD_DSP_PARAMETER_DESC *FMOD_DistanceFilter_dspparam[FMOD_DISTANCE_FILTER_NUM_PARAMETERS] =
{
    &p_max_distance,
    &p_bandpass_frequency,
    &p_3d_attributes
};

FMOD_DSP_DESCRIPTION FMOD_DistanceFilter_Desc =
{
    FMOD_PLUGIN_SDK_VERSION,
    "FMOD Distance Filter",                 // name
    0x00010000,                             // plugin version
    1,                                      // number of input buffers to process
    1,                                      // number of output buffers to process
    FMOD_DistanceFilter_dspcreate,
    FMOD_DistanceFilter_dsprelease,
    FMOD_DistanceFilter_dspreset,
    FMOD_DistanceFilter_dspread,
    0,  // FMOD_DistanceFilter_dspprocess,  // *** declare this callback instead of FMOD_DistanceFilter_dspread if the plugin sets the output channel count ***
    0,
    FMOD_DISTANCE_FILTER_NUM_PARAMETERS,
    FMOD_DistanceFilter_dspparam,
    FMOD_DistanceFilter_dspsetparamfloat,
    0, // FMOD_DistanceFilter_dspsetparamint,
    0, // FMOD_DistanceFilter_dspsetparambool,
    FMOD_DistanceFilter_dspsetparamdata,
    FMOD_DistanceFilter_dspgetparamfloat,
    0, // FMOD_DistanceFilter_dspgetparamint,
    0, // FMOD_DistanceFilter_dspgetparambool,
    FMOD_DistanceFilter_dspgetparamdata,
    FMOD_DistanceFilter_shouldiprocess,
    0,                                      // userdata
    0,                                      // sys_register
    0,                                      // sys_deregister
    0                                       // sys_mix
};

extern "C"
{
    F_EXPORT FMOD_DSP_DESCRIPTION* F_CALL FMODGetDSPDescription()
    {
	    static float distance_mapping_values[] = { 0, 1, 5, 20, 100, 500, 10000 };
	    static float distance_mapping_scale[] = { 0, 1, 2, 3, 4, 4.5, 5 };

        FMOD_DSP_INIT_PARAMDESC_FLOAT_WITH_MAPPING(p_max_distance,       "Max Dist",      "",    "Distance at which bandpass stops narrowing. 0 to 1000000000. Default = 100", FMOD_DISTANCE_FILTER_PARAM_MAX_DISTANCE_DEFAULT, distance_mapping_values, distance_mapping_scale);
        FMOD_DSP_INIT_PARAMDESC_FLOAT(p_bandpass_frequency, "Frequency",     "Hz",  "Bandpass target frequency. 100 to 10,000Hz. Default = 2000Hz",               FMOD_DISTANCE_FILTER_PARAM_BANDPASS_FREQUENCY_MIN, FMOD_DISTANCE_FILTER_PARAM_BANDPASS_FREQUENCY_MAX, FMOD_DISTANCE_FILTER_PARAM_BANDPASS_FREQUENCY_DEFAULT);
        FMOD_DSP_INIT_PARAMDESC_DATA(p_3d_attributes,       "3D Attributes", "",    "",                                                                           FMOD_DSP_PARAMETER_DATA_TYPE_3DATTRIBUTES);

        return &FMOD_DistanceFilter_Desc;
    }
}

class FMODDistanceFilterState
{
  public:
    FMODDistanceFilterState() { }

    void        init                (FMOD_DSP_STATE *dsp_state);
    void        release             (FMOD_DSP_STATE *dsp_state);
    FMOD_RESULT process             (float *inbuffer, float *outbuffer, unsigned int length, int channels);
    FMOD_RESULT process             (unsigned int length, const FMOD_DSP_BUFFER_ARRAY *inbufferarray, FMOD_DSP_BUFFER_ARRAY *outbufferarray, FMOD_BOOL inputsidle, FMOD_DSP_PROCESS_OPERATION op);
    void        reset               ();
    void        setMaxDistance      (float);
    void        setBandpassFrequency(float);
    void        setDistance         (float);
    float       maxDistance         () const { return m_max_distance; }
    float       bandpassFrequency   () const { return m_bandpass_frequency; }

  private:
    void        updateTimeConstants ();

    float       m_max_distance;
    float       m_bandpass_frequency;
    float       m_distance;
    float       m_target_highpass_time_const;
    float       m_current_highpass_time_const;
    float       m_target_lowpass_time_const;
    float       m_current_lowpass_time_const;
    int         m_ramp_samples_left;
    float      *m_previous_lp1_out;
    float      *m_previous_lp2_out;
    float      *m_previous_hp_out;
    int         m_sample_rate;
    int         m_max_channels;
};

void FMODDistanceFilterState::init(FMOD_DSP_STATE *dsp_state)
{
    FMOD_DSP_GETSAMPLERATE(dsp_state, &m_sample_rate);

    m_max_channels = 8;
    m_max_distance = FMOD_DISTANCE_FILTER_PARAM_MAX_DISTANCE_DEFAULT;
    m_bandpass_frequency = FMOD_DISTANCE_FILTER_PARAM_BANDPASS_FREQUENCY_DEFAULT;
    m_distance = 0;

    m_previous_lp1_out = (float*)FMOD_DSP_ALLOC(dsp_state, m_max_channels * sizeof(float));
    m_previous_lp2_out = (float*)FMOD_DSP_ALLOC(dsp_state, m_max_channels * sizeof(float));
    m_previous_hp_out = (float*)FMOD_DSP_ALLOC(dsp_state, m_max_channels * sizeof(float));

    updateTimeConstants();
    reset();
}

void FMODDistanceFilterState::release(FMOD_DSP_STATE *dsp_state)
{
    FMOD_DSP_FREE(dsp_state, m_previous_lp1_out);
    FMOD_DSP_FREE(dsp_state, m_previous_lp2_out);
    FMOD_DSP_FREE(dsp_state, m_previous_hp_out);
}

FMOD_RESULT FMODDistanceFilterState::process(float *inbuffer, float *outbuffer, unsigned int length, int channels)
{
    if(channels > m_max_channels)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    // Note: buffers are interleaved
    static float jitter = (float)1E-20;
    float lp1_out, lp2_out;
    int ch;

    float lp_tc = m_current_lowpass_time_const;
    float hp_tc = m_current_highpass_time_const;

    if (m_ramp_samples_left)
    {
        float lp_delta = (m_target_lowpass_time_const - m_current_lowpass_time_const) / m_ramp_samples_left;
        float hp_delta = (m_target_highpass_time_const - m_current_highpass_time_const) / m_ramp_samples_left;
        while (length)
        {
            if (--m_ramp_samples_left)
            {
                lp_tc += lp_delta;
                hp_tc += hp_delta;
                for (ch = 0; ch < channels; ++ch)
                {
                    lp1_out = m_previous_lp1_out[ch] + lp_tc * (*inbuffer++ + jitter - m_previous_lp1_out[ch]);
                    lp2_out = m_previous_lp2_out[ch] + lp_tc * (lp1_out - m_previous_lp2_out[ch]);
                    *outbuffer = hp_tc * (m_previous_hp_out[ch] + lp2_out - m_previous_lp2_out[ch]);

                    m_previous_lp1_out[ch] = lp1_out;
                    m_previous_lp2_out[ch] = lp2_out;
                    m_previous_hp_out[ch] = *outbuffer++;
                }
                jitter = -jitter;
            }
            else
            {
                lp_tc = m_target_lowpass_time_const;
                hp_tc = m_target_highpass_time_const;
                break;
            }
            --length;
        }
    }

    while (length--)
    {
        for (ch = 0; ch < channels; ++ch)
        {
            lp1_out = m_previous_lp1_out[ch] + lp_tc * (*inbuffer++ + jitter - m_previous_lp1_out[ch]);
            lp2_out = m_previous_lp2_out[ch] + lp_tc * (lp1_out - m_previous_lp2_out[ch]);
            *outbuffer = hp_tc * (m_previous_hp_out[ch] + lp2_out - m_previous_lp2_out[ch]);

            m_previous_lp1_out[ch] = lp1_out;
            m_previous_lp2_out[ch] = lp2_out;
            m_previous_hp_out[ch] = *outbuffer++;
        }
        jitter = -jitter;
    }

    m_current_lowpass_time_const = lp_tc;
    m_current_highpass_time_const = hp_tc;

    return FMOD_OK;
}

FMOD_RESULT FMODDistanceFilterState::process(unsigned int length, const FMOD_DSP_BUFFER_ARRAY *inbufferarray, FMOD_DSP_BUFFER_ARRAY *outbufferarray, FMOD_BOOL inputsidle, FMOD_DSP_PROCESS_OPERATION op)
{
    if (op == FMOD_DSP_PROCESS_QUERY)
    {
        FMOD_SPEAKERMODE outmode;
        int outchannels;

        #if 1 // For stereo output
        {
            outmode = FMOD_SPEAKERMODE_STEREO;
            outchannels = 2;
        }
        #else // For 5.1 output
        {
            outmode = FMOD_SPEAKERMODE_5POINT1;
            outchannels = 6;
        }
        #endif

        if (outbufferarray)
        {
            outbufferarray->speakermode = outmode;
            outbufferarray->buffernumchannels[0] = outchannels;
        }

        if (inputsidle)
        {
            return FMOD_ERR_DSP_DONTPROCESS;
        }

        return FMOD_OK;
    }

    // Do processing here
    float *inbuffer    = inbufferarray->buffers[0];
    float *outbuffer   = outbufferarray->buffers[0];
    int    inchannels  = inbufferarray->buffernumchannels[0];
    int    outchannels = outbufferarray->buffernumchannels[0];
    
    while(length--)
    {
        // MAIN DSP LOOP...
    }

    return FMOD_OK;
}

void FMODDistanceFilterState::reset()
{
    m_current_lowpass_time_const = m_target_lowpass_time_const;
    m_current_highpass_time_const = m_target_highpass_time_const;
    m_ramp_samples_left = 0;

    memset(m_previous_lp1_out, 0, m_max_channels * sizeof(float));
    memset(m_previous_lp2_out, 0, m_max_channels * sizeof(float));
    memset(m_previous_hp_out, 0, m_max_channels * sizeof(float));
}

void FMODDistanceFilterState::setMaxDistance(float distance)
{
    m_max_distance = distance;
    updateTimeConstants();
}

void FMODDistanceFilterState::setBandpassFrequency(float frequency)
{
    m_bandpass_frequency = frequency;
    updateTimeConstants();
}

void FMODDistanceFilterState::setDistance(float distance)
{
    m_distance = distance;
    updateTimeConstants();
}

void FMODDistanceFilterState::updateTimeConstants()
{
    #define PI (3.14159265358979323846f)
    #define MIN_CUTOFF (10.0f)
    #define MAX_CUTOFF (22000.0f)

    float dist_factor = m_distance >= m_max_distance ? 1.0f : m_distance / m_max_distance;
    float lp_cutoff = m_bandpass_frequency + (1.0f - dist_factor) * (1.0f - dist_factor) * (MAX_CUTOFF - m_bandpass_frequency);
    float hp_cutoff = MIN_CUTOFF + dist_factor * dist_factor * (m_bandpass_frequency - MIN_CUTOFF);

    float dt = 1.0f / m_sample_rate;
    float threshold = m_sample_rate / PI;

    if (lp_cutoff >= MAX_CUTOFF)
    {
        m_target_lowpass_time_const = 1.0f;
    }
    else if (lp_cutoff <= threshold)
    {
        float RC = 1.0f / (2.0f * PI * lp_cutoff);
        m_target_lowpass_time_const = dt / (RC + dt);
    }
    else
    {
        m_target_lowpass_time_const = 0.666666667f + (lp_cutoff - threshold) / (3.0f * (MAX_CUTOFF - threshold));
    }

    if (hp_cutoff >= MAX_CUTOFF)
    {
        m_target_highpass_time_const = 0.0f;
    }
    else if (hp_cutoff <= threshold)
    {
        float RC = 1.0f / (2.0f * PI * hp_cutoff);
        m_target_highpass_time_const = RC / (RC + dt);
    }
    else
    {
        m_target_highpass_time_const = (MAX_CUTOFF - hp_cutoff) / (3.0f * (MAX_CUTOFF - threshold));
    }

    m_ramp_samples_left = 256;
}

FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspcreate(FMOD_DSP_STATE *dsp_state)
{
    FMODDistanceFilterState* state = (FMODDistanceFilterState *)FMOD_DSP_ALLOC(dsp_state, sizeof(FMODDistanceFilterState));
    state->init(dsp_state);
    dsp_state->plugindata = state;
    if (!state)
    {
        return FMOD_ERR_MEMORY;
    }
    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dsprelease(FMOD_DSP_STATE *dsp_state)
{
    FMODDistanceFilterState *state = (FMODDistanceFilterState *)dsp_state->plugindata;
    state->release(dsp_state);
    FMOD_DSP_FREE(dsp_state, state);
    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspread(FMOD_DSP_STATE *dsp_state, float *inbuffer, float *outbuffer, unsigned int length, int inchannels, int * /*outchannels*/)
{
    FMODDistanceFilterState *state = (FMODDistanceFilterState *)dsp_state->plugindata;
    return state->process(inbuffer, outbuffer, length, inchannels); // input and output channels count match for this effect
}

FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspprocess(FMOD_DSP_STATE *dsp_state, unsigned int length, const FMOD_DSP_BUFFER_ARRAY *inbufferarray, FMOD_DSP_BUFFER_ARRAY *outbufferarray, FMOD_BOOL inputsidle, FMOD_DSP_PROCESS_OPERATION op)
{
    FMODDistanceFilterState *state = (FMODDistanceFilterState *)dsp_state->plugindata;
    return state->process(length, inbufferarray, outbufferarray, inputsidle, op); // as an example for plugins which set the output channel count
}

FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspreset(FMOD_DSP_STATE *dsp_state)
{
    FMODDistanceFilterState *state = (FMODDistanceFilterState *)dsp_state->plugindata;
    state->reset();
    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspsetparamfloat(FMOD_DSP_STATE *dsp_state, int index, float value)
{
    FMODDistanceFilterState *state = (FMODDistanceFilterState *)dsp_state->plugindata;

    switch (index)
    {
    case FMOD_DISTANCE_FILTER_MAX_DISTANCE:
        state->setMaxDistance(value);
        return FMOD_OK;

    case FMOD_DISTANCE_FILTER_BANDPASS_FREQUENCY:
        state->setBandpassFrequency(value);
        return FMOD_OK;
    }

    return FMOD_ERR_INVALID_PARAM;
}

FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspgetparamfloat(FMOD_DSP_STATE *dsp_state, int index, float *value, char *valuestr)
{
    FMODDistanceFilterState *state = (FMODDistanceFilterState *)dsp_state->plugindata;

    switch (index)
    {
    case FMOD_DISTANCE_FILTER_MAX_DISTANCE:
        *value = state->maxDistance();
        if (valuestr) snprintf(valuestr, FMOD_DSP_GETPARAM_VALUESTR_LENGTH, "%.1f", state->maxDistance());
        return FMOD_OK;

    case FMOD_DISTANCE_FILTER_BANDPASS_FREQUENCY:
        *value = state->bandpassFrequency();
        if (valuestr) snprintf(valuestr, FMOD_DSP_GETPARAM_VALUESTR_LENGTH, "%.1f Hz", state->bandpassFrequency());
        return FMOD_OK;
    }

    return FMOD_ERR_INVALID_PARAM;
}

FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspsetparamdata(FMOD_DSP_STATE *dsp_state, int index, void *data, unsigned int /*length*/)
{
    FMODDistanceFilterState *state = (FMODDistanceFilterState *)dsp_state->plugindata;

    switch (index)
    {
    case FMOD_DISTANCE_FILTER_3D_ATTRIBUTES:
        FMOD_DSP_PARAMETER_3DATTRIBUTES* param = (FMOD_DSP_PARAMETER_3DATTRIBUTES*)data;
        state->setDistance(sqrtf(param->relative.position.x * param->relative.position.x + param->relative.position.y * param->relative.position.y + param->relative.position.z * param->relative.position.z));
        return FMOD_OK;
    }

    return FMOD_ERR_INVALID_PARAM;
}

FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_dspgetparamdata(FMOD_DSP_STATE * /*dsp_state*/, int index, void ** /*value*/, unsigned int * /*length*/, char * /*valuestr*/)
{
    switch (index)
    {
      case FMOD_DISTANCE_FILTER_3D_ATTRIBUTES:
        return FMOD_ERR_INVALID_PARAM;
    }

    return FMOD_ERR_INVALID_PARAM;
}

FMOD_RESULT F_CALLBACK FMOD_DistanceFilter_shouldiprocess(FMOD_DSP_STATE * /*dsp_state*/, FMOD_BOOL inputsidle, unsigned int /*length*/, FMOD_CHANNELMASK /*inmask*/, int /*inchannels*/, FMOD_SPEAKERMODE /*speakermode*/)
{
    if (inputsidle)
    {
        return FMOD_ERR_DSP_DONTPROCESS;
    }

    return FMOD_OK;
}
