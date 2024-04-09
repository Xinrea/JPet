/*==============================================================================
Generate Tone Example
Copyright (c), Firelight Technologies Pty, Ltd 2004-2024.

This example shows how to play generated tones using System::playDSP
instead of manually connecting and disconnecting DSP units.

For information on using FMOD example code in your own programs, visit
https://www.fmod.com/legal
==============================================================================*/
#include "fmod.hpp"
#include "common.h"

int FMOD_Main()
{
    FMOD::System    *system;
    FMOD::Channel   *channel = 0;
    FMOD::DSP       *dsp;
    FMOD_RESULT      result;
    void            *extradriverdata = 0;

    Common_Init(&extradriverdata);
    
    /*
        Create a System object and initialize.
    */
    result = FMOD::System_Create(&system);
    ERRCHECK(result);

    result = system->init(32, FMOD_INIT_NORMAL, extradriverdata);
    ERRCHECK(result);

    /*
        Create an oscillator DSP units for the tone.
    */
    result = system->createDSPByType(FMOD_DSP_TYPE_OSCILLATOR, &dsp);
    ERRCHECK(result);
    result = dsp->setParameterFloat(FMOD_DSP_OSCILLATOR_RATE, 440.0f); /* Musical note 'A' */
    ERRCHECK(result);

    /*
        Main loop
    */
    do
    {
        Common_Update();

        if (Common_BtnPress(BTN_ACTION1))
        {
            if (channel)
            {
                result = channel->stop();
                ERRCHECK(result);
            }

            result = system->playDSP(dsp, 0, true, &channel);
            ERRCHECK(result);
            result = channel->setVolume(0.5f);
            ERRCHECK(result);
            result = dsp->setParameterInt(FMOD_DSP_OSCILLATOR_TYPE, 0);
            ERRCHECK(result);
            result = channel->setPaused(false);
            ERRCHECK(result);
        }

        if (Common_BtnPress(BTN_ACTION2))
        {
            if (channel)
            {
                result = channel->stop();
                ERRCHECK(result);
            }

            result = system->playDSP(dsp, 0, true, &channel);
            ERRCHECK(result);
            result = channel->setVolume(0.125f);
            ERRCHECK(result);
            result = dsp->setParameterInt(FMOD_DSP_OSCILLATOR_TYPE, 1);
            ERRCHECK(result);
            result = channel->setPaused(false);
            ERRCHECK(result);
        }

        if (Common_BtnPress(BTN_ACTION3))
        {
            if (channel)
            {
                result = channel->stop();
                ERRCHECK(result);
            }

            result = system->playDSP(dsp, 0, true, &channel);
            ERRCHECK(result);
            result = channel->setVolume(0.125f);
            ERRCHECK(result);
            result = dsp->setParameterInt(FMOD_DSP_OSCILLATOR_TYPE, 2);
            ERRCHECK(result);
            result = channel->setPaused(false);
            ERRCHECK(result);
        }

        if (Common_BtnPress(BTN_ACTION4))
        {
            if (channel)
            {
                result = channel->stop();
                ERRCHECK(result);
            }

            result = system->playDSP(dsp, 0, true, &channel);
            ERRCHECK(result);
            result = channel->setVolume(0.5f);
            ERRCHECK(result);
            result = dsp->setParameterInt(FMOD_DSP_OSCILLATOR_TYPE, 4);
            ERRCHECK(result);
            result = channel->setPaused(false);
            ERRCHECK(result);
        }

        if (Common_BtnPress(BTN_MORE))
        {
            if (channel)
            {
                result = channel->stop();
                ERRCHECK(result);
                channel = 0;
            }
        }

        if (channel)
        {
            if (Common_BtnDown(BTN_UP) || Common_BtnDown(BTN_DOWN))
            {
                float volume;
            
                result = channel->getVolume(&volume);
                ERRCHECK(result);
            
                volume += (Common_BtnDown(BTN_UP) ? +0.1f : -0.1f);
                volume = (volume > 1.0f) ? 1.0f : volume;
                volume = (volume < 0.0f) ? 0.0f : volume;
           
                result = channel->setVolume(volume);
                ERRCHECK(result);
            }

            if (Common_BtnDown(BTN_LEFT) || Common_BtnDown(BTN_RIGHT))
            {
                float frequency;
            
                result = channel->getFrequency(&frequency);
                ERRCHECK(result);
            
                frequency += (Common_BtnDown(BTN_RIGHT) ? +500.0f : -500.0f);
            
                result = channel->setFrequency(frequency);
                ERRCHECK(result);
            }
        }

        result = system->update();
        ERRCHECK(result);

        {
            float frequency = 0.0f, volume = 0.0f;
            bool playing = false;

            if (channel)
            {
                result = channel->getFrequency(&frequency);
                ERRCHECK(result);
                result = channel->getVolume(&volume);
                ERRCHECK(result);
                result = channel->isPlaying(&playing);
                ERRCHECK(result);
            }

            Common_Draw("==================================================");
            Common_Draw("Generate Tone Example.");
            Common_Draw("Copyright (c) Firelight Technologies 2004-2024.");
            Common_Draw("==================================================");
            Common_Draw("");
            Common_Draw("Press %s to play a sine wave", Common_BtnStr(BTN_ACTION1));
            Common_Draw("Press %s to play a square wave", Common_BtnStr(BTN_ACTION2));
            Common_Draw("Press %s to play a saw wave", Common_BtnStr(BTN_ACTION3));
            Common_Draw("Press %s to play a triangle wave", Common_BtnStr(BTN_ACTION4));
            Common_Draw("Press %s to stop the channel", Common_BtnStr(BTN_MORE));
            Common_Draw("Press %s and %s to change volume", Common_BtnStr(BTN_UP), Common_BtnStr(BTN_DOWN));
            Common_Draw("Press %s and %s to change frequency", Common_BtnStr(BTN_LEFT), Common_BtnStr(BTN_RIGHT));
            Common_Draw("Press %s to quit", Common_BtnStr(BTN_QUIT));
            Common_Draw("");
            Common_Draw("Channel is %s", playing ? "playing" : "stopped");
            Common_Draw("Volume %0.2f", volume);
            Common_Draw("Frequency %0.2f", frequency);
        }

        Common_Sleep(50);
    } while (!Common_BtnPress(BTN_QUIT));

    /*
        Shut down
    */
    result = dsp->release();
    ERRCHECK(result);
    result = system->close();
    ERRCHECK(result);
    result = system->release();
    ERRCHECK(result);

    Common_Close();

    return 0;
}
