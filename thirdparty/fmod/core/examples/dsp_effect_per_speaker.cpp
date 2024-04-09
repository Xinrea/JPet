/*==============================================================================
DSP Effect Per Speaker Example
Copyright (c), Firelight Technologies Pty, Ltd 2004-2024.

This example shows how to manipulate a DSP network and as an example, creates 2
DSP effects, splitting a single sound into 2 audio paths, which it then filters
seperately.

To only have each audio path come out of one speaker each,
DSPConnection::setMixMatrix is used just before the 2 branches merge back together
again.

For more speakers:

 * Use System::setSoftwareFormat
 * Create more effects, currently 2 for stereo (lowpass and highpass), create one
   per speaker.
 * Under the 'Now connect the 2 effects to channeldsp head.' section, connect
   the extra effects by duplicating the code more times.
 * Filter each effect to each speaker by calling DSPConnection::setMixMatrix.  
   Expand the existing code by extending the matrices from 2 in and 2 out, to the 
   number of speakers you require.

For information on using FMOD example code in your own programs, visit
https://www.fmod.com/legal
==============================================================================*/
#include "fmod.hpp"
#include "common.h"

int FMOD_Main()
{
    FMOD::System        *system;
    FMOD::Sound         *sound;
    FMOD::Channel       *channel;
    FMOD::ChannelGroup  *mastergroup;
    FMOD::DSP           *dsplowpass, *dsphighpass, *dsphead, *dspchannelmixer;
    FMOD::DSPConnection *dsplowpassconnection, *dsphighpassconnection;
    FMOD_RESULT          result;
    float                pan = 0.0f;
    void                *extradriverdata = 0;

    Common_Init(&extradriverdata);

    /*
        Create a System object and initialize.
    */
    result = FMOD::System_Create(&system);
    ERRCHECK(result);

    /*
        In this special case we want to use stereo output and not worry about varying matrix sizes depending on user speaker mode.
    */
    system->setSoftwareFormat(48000, FMOD_SPEAKERMODE_STEREO, 0);
    ERRCHECK(result);

    /*
        Initialize FMOD
    */
    result = system->init(32, FMOD_INIT_NORMAL, extradriverdata);
    ERRCHECK(result);

    result = system->createSound(Common_MediaPath("drumloop.wav"), FMOD_LOOP_NORMAL, 0, &sound);
    ERRCHECK(result);

    result = system->playSound(sound, 0, false, &channel);
    ERRCHECK(result);

    /*
        Create the DSP effects.
    */  
    result = system->createDSPByType(FMOD_DSP_TYPE_LOWPASS, &dsplowpass);
    ERRCHECK(result);

    result = dsplowpass->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, 1000.0f);
    ERRCHECK(result);
    result = dsplowpass->setParameterFloat(FMOD_DSP_LOWPASS_RESONANCE, 4.0f);
    ERRCHECK(result);

    result = system->createDSPByType(FMOD_DSP_TYPE_HIGHPASS, &dsphighpass);
    ERRCHECK(result);

    result = dsphighpass->setParameterFloat(FMOD_DSP_HIGHPASS_CUTOFF, 4000.0f);
    ERRCHECK(result);
    result = dsphighpass->setParameterFloat(FMOD_DSP_HIGHPASS_RESONANCE, 4.0f);
    ERRCHECK(result);

    /*
        Connect up the DSP network
    */

    /*
        When a sound is played, a subnetwork is set up in the DSP network which looks like this.
        Wavetable is the drumloop sound, and it feeds its data from right to left.

        [DSPHEAD]<------------[DSPCHANNELMIXER]<------------[CHANNEL HEAD]<------------[WAVETABLE - DRUMLOOP.WAV]
    */  
    result = system->getMasterChannelGroup(&mastergroup);
    ERRCHECK(result);

    result = mastergroup->getDSP(FMOD_CHANNELCONTROL_DSP_HEAD, &dsphead);
    ERRCHECK(result);

    result = dsphead->getInput(0, &dspchannelmixer, 0);
    ERRCHECK(result);

    /*
        Now disconnect channeldsp head from wavetable to look like this.

        [DSPHEAD]             [DSPCHANNELMIXER]<------------[CHANNEL HEAD]<------------[WAVETABLE - DRUMLOOP.WAV]
    */
    result = dsphead->disconnectFrom(dspchannelmixer);
    ERRCHECK(result);

    /*
        Now connect the 2 effects to channeldsp head.  
        Store the 2 connections this makes so we can set their matrix later.

                  [DSPLOWPASS]
                 /x           
        [DSPHEAD]             [DSPCHANNELMIXER]<------------[CHANNEL HEAD]<------------[WAVETABLE - DRUMLOOP.WAV]
                 \y           
                  [DSPHIGHPASS]
    */
    result = dsphead->addInput(dsplowpass, &dsplowpassconnection);      /* x = dsplowpassconnection */
    ERRCHECK(result);
    result = dsphead->addInput(dsphighpass, &dsphighpassconnection);    /* y = dsphighpassconnection */
    ERRCHECK(result);
    
    /*
        Now connect the channelmixer to the 2 effects

                  [DSPLOWPASS]
                 /x          \
        [DSPHEAD]             [DSPCHANNELMIXER]<------------[CHANNEL HEAD]<------------[WAVETABLE - DRUMLOOP.WAV]
                 \y          /
                  [DSPHIGHPASS]
    */
    result = dsplowpass->addInput(dspchannelmixer);     /* Ignore connection - we dont care about it. */
    ERRCHECK(result);

    result = dsphighpass->addInput(dspchannelmixer);    /* Ignore connection - we dont care about it. */
    ERRCHECK(result);

    /*
        Now the drumloop will be twice as loud, because it is being split into 2, then recombined at the end.
        What we really want is to only feed the dsphead<-dsplowpass through the left speaker for that effect, and 
        dsphead<-dsphighpass to the right speaker for that effect.
        We can do that simply by setting the pan, or speaker matrix of the connections.

                  [DSPLOWPASS]
                 /x=1,0      \
        [DSPHEAD]             [DSPCHANNELMIXER]<------------[CHANNEL HEAD]<------------[WAVETABLE - DRUMLOOP.WAV]
                 \y=0,1      /
                  [DSPHIGHPASS]
    */    
    {
        float lowpassmatrix[2][2]  = { 
                                        { 1.0f, 0.0f },     // <- output to front left.  Take front left input signal at 1.0.
                                        { 0.0f, 0.0f }      // <- output to front right.  Silence
                                     };
        float highpassmatrix[2][2] = { 
                                        { 0.0f, 0.0f },     // <- output to front left.  Silence
                                        { 0.0f, 1.0f }      // <- output to front right.  Take front right input signal at 1.0
                                     };

        /* 
            Upgrade the signal coming from the channel mixer from mono to stereo.  Otherwise the lowpass and highpass will get mono signals 
        */
        result = dspchannelmixer->setChannelFormat(0, 0, FMOD_SPEAKERMODE_STEREO);
        ERRCHECK(result);

        /*
            Now set the above matrices.
        */
        result = dsplowpassconnection->setMixMatrix(&lowpassmatrix[0][0], 2, 2);
        ERRCHECK(result);
        result = dsphighpassconnection->setMixMatrix(&highpassmatrix[0][0], 2, 2);
        ERRCHECK(result);
    }

    result = dsplowpass->setBypass(true);
    ERRCHECK(result);
    result = dsphighpass->setBypass(true);
    ERRCHECK(result);

    result = dsplowpass->setActive(true);
    ERRCHECK(result);
    result = dsphighpass->setActive(true);
    ERRCHECK(result);

    /*
        Main loop.
    */
    do
    {
        bool lowpassbypass, highpassbypass;

        Common_Update();

        result = dsplowpass->getBypass(&lowpassbypass);
        ERRCHECK(result);
        result = dsphighpass->getBypass(&highpassbypass);
        ERRCHECK(result);

        if (Common_BtnPress(BTN_ACTION1))
        {
            lowpassbypass = !lowpassbypass;

            result = dsplowpass->setBypass(lowpassbypass);
            ERRCHECK(result);
        }

        if (Common_BtnPress(BTN_ACTION2))
        {
            highpassbypass = !highpassbypass;

            result = dsphighpass->setBypass(highpassbypass);
            ERRCHECK(result);
        }

        if (Common_BtnDown(BTN_LEFT))
        {
            pan = (pan <= -0.9f) ? -1.0f : pan - 0.1f; 
            
            result = channel->setPan(pan);
            ERRCHECK(result);
        }

        if (Common_BtnDown(BTN_RIGHT))
        {
            pan = (pan >= 0.9f) ? 1.0f : pan + 0.1f; 
            
            result = channel->setPan(pan);
            ERRCHECK(result);
        }

        result = system->update();
        ERRCHECK(result);

        Common_Draw("==================================================");
        Common_Draw("DSP Effect Per Speaker Example.");
        Common_Draw("Copyright (c) Firelight Technologies 2004-2024.");
        Common_Draw("==================================================");
        Common_Draw("");
        Common_Draw("Press %s to toggle lowpass (left speaker)", Common_BtnStr(BTN_ACTION1));
        Common_Draw("Press %s to toggle highpass (right speaker)", Common_BtnStr(BTN_ACTION2));
        Common_Draw("Press %s or %s to pan sound", Common_BtnStr(BTN_LEFT), Common_BtnStr(BTN_RIGHT));
        Common_Draw("Press %s to quit", Common_BtnStr(BTN_QUIT));
        Common_Draw("");
        Common_Draw("Lowpass (left) is %s", lowpassbypass ? "inactive" : "active");
        Common_Draw("Highpass (right) is %s", highpassbypass ? "inactive" : "active");
        Common_Draw("Pan is %0.2f", pan);

        Common_Sleep(50);
    } while (!Common_BtnPress(BTN_QUIT));

    /*
        Shut down
    */
    result = sound->release();
    ERRCHECK(result);

    result = dsplowpass->release();
    ERRCHECK(result);
    result = dsphighpass->release();
    ERRCHECK(result);

    result = system->close();
    ERRCHECK(result);
    result = system->release();
    ERRCHECK(result);

    Common_Close();

    return 0;
}
