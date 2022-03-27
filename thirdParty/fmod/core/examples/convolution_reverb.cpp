/*==============================================================================
Convolution Reverb Example
Copyright (c), Firelight Technologies Pty, Ltd 2004-2024.

This example shows how to set up a convolution reverb DSP as a global
DSP unit that can be routed into by multiple seperate channels.

Convolution reverb uses data from a real world locations called an 
"Impulse Response" to model the reflection of audio waves back
to a listener.

Impulse Response is based on "St Andrew's Church" by

    www.openairlib.net
    Audiolab, University of York
    Damian T. Murphy
    http://www.openairlib.net/auralizationdb/content/st-andrews-church

licensed under Attribution Share Alike Creative Commons license
http://creativecommons.org/licenses/by-sa/3.0/


Anechoic sample "Operatic Voice" by 

    www.openairlib.net
    http://www.openairlib.net/anechoicdb/content/operatic-voice

licensed under Attribution Share Alike Creative Commons license
http://creativecommons.org/licenses/by-sa/3.0/


### Features Demonstrated ###
+ FMOD_DSP_CONVOLUTION_REVERB
+ DSP::addInput

For information on using FMOD example code in your own programs, visit
https://www.fmod.com/legal

==============================================================================*/
#include "fmod.hpp"
#include "common.h"

int FMOD_Main()
{
    void *extradriverdata = 0;    
    Common_Init(&extradriverdata);

    /*
        Create a System object and initialize
    */
    FMOD_RESULT result;
    FMOD::System* system;
    result = FMOD::System_Create(&system);
    ERRCHECK(result);
    
    result = system->init(32, FMOD_INIT_NORMAL, extradriverdata);
    ERRCHECK(result);
        

    /*
        Create a new channel group to hold the convolution DSP unit
    */
    FMOD::ChannelGroup* reverbGroup;
    result = system->createChannelGroup("reverb", &reverbGroup);
    ERRCHECK(result);

    
    /*
        Create a new channel group to hold all the channels and process the dry path
    */
    FMOD::ChannelGroup* mainGroup;
    result = system->createChannelGroup("main", &mainGroup);
    ERRCHECK(result);

    /*
        Create the convultion DSP unit and set it as the tail of the channel group
    */
    FMOD::DSP* reverbUnit;    
    result = system->createDSPByType(FMOD_DSP_TYPE_CONVOLUTIONREVERB, &reverbUnit);
    ERRCHECK(result);
    result = reverbGroup->addDSP(FMOD_CHANNELCONTROL_DSP_TAIL, reverbUnit);
    ERRCHECK(result);

    /*
        Open the impulse response wav file, but use FMOD_OPENONLY as we want
        to read the data into a seperate buffer
    */
    FMOD::Sound* irSound;
    result = system->createSound(Common_MediaPath("standrews.wav"), FMOD_DEFAULT | FMOD_OPENONLY, NULL, &irSound);
    ERRCHECK(result);

    /*
        Retrieve the sound information for the Impulse Response input file
    */
    FMOD_SOUND_FORMAT irSoundFormat;
    FMOD_SOUND_TYPE irSoundType;
    int irSoundBits, irSoundChannels;
    result = irSound->getFormat(&irSoundType, &irSoundFormat, &irSoundChannels, &irSoundBits);
    ERRCHECK(result);
    unsigned int irSoundLength;
    result = irSound->getLength(&irSoundLength, FMOD_TIMEUNIT_PCM);
    ERRCHECK(result);

    
    if (irSoundFormat != FMOD_SOUND_FORMAT_PCM16)
    {
        /*
            For simplicity of the example, if the impulse response is the wrong format just display an error
        */        
        Common_Fatal("Impulse Response file is the wrong audio format");
    }

    /*
        The reverb unit expects a block of data containing a single 16 bit int containing
        the number of channels in the impulse response, followed by PCM 16 data
    */
    unsigned int irDataLength = sizeof(short) * (irSoundLength * irSoundChannels + 1);
    short* irData = (short*)malloc(irDataLength);
    irData[0] = (short)irSoundChannels;
    unsigned int irDataRead;
    result = irSound->readData(&irData[1], irDataLength - sizeof(short), &irDataRead);
    ERRCHECK(result);
    result = reverbUnit->setParameterData(FMOD_DSP_CONVOLUTION_REVERB_PARAM_IR, irData, irDataLength);
    ERRCHECK(result);

    /*
        Don't pass any dry signal from the reverb unit, instead take the dry part
        of the mix from the main signal path
    */
    result = reverbUnit->setParameterFloat(FMOD_DSP_CONVOLUTION_REVERB_PARAM_DRY, -80.0f);    
    ERRCHECK(result);

    /*
        We can now free our copy of the IR data and release the sound object, the reverb unit 
        has created it's internal data
    */
    free(irData);
    result = irSound->release();
    ERRCHECK(result);
    
    /*
        Load up and play a sample clip recorded in an anechoic chamber
    */
    FMOD::Sound* sound;
    system->createSound(Common_MediaPath("singing.wav"), FMOD_3D | FMOD_LOOP_NORMAL, NULL, &sound);
    ERRCHECK(result);

    FMOD::Channel* channel;
    system->playSound(sound, mainGroup, true, &channel);
    ERRCHECK(result);
    
    /*
        Create a send connection between the channel head and the reverb unit
    */
    FMOD::DSP* channelHead;
    channel->getDSP(FMOD_CHANNELCONTROL_DSP_HEAD, &channelHead);
    ERRCHECK(result);
    FMOD::DSPConnection* reverbConnection;
    result = reverbUnit->addInput(channelHead, &reverbConnection, FMOD_DSPCONNECTION_TYPE_SEND);
    ERRCHECK(result);

    result = channel->setPaused(false);
    ERRCHECK(result);


    float wetVolume = 1.0;
    float dryVolume = 1.0;

    /*
        Main loop
    */
    do
    {
        Common_Update();

        if (Common_BtnPress(BTN_LEFT))
        {
            wetVolume = (wetVolume <= 0.0f) ? wetVolume : wetVolume - 0.05f;
        }
        if (Common_BtnPress(BTN_RIGHT))
        {
            wetVolume = (wetVolume >= 1.0f) ? wetVolume : wetVolume + 0.05f;
        }        
        if (Common_BtnPress(BTN_DOWN))
        {
            dryVolume = (dryVolume <= 0.0f) ? dryVolume : dryVolume - 0.05f;
        }
        if (Common_BtnPress(BTN_UP))
        {
            dryVolume = (dryVolume >= 1.0f) ? dryVolume : dryVolume + 0.05f;
        }
        

        result = system->update();
        ERRCHECK(result);

        result = reverbConnection->setMix(wetVolume);
        ERRCHECK(result);
        result = mainGroup->setVolume(dryVolume);
        ERRCHECK(result);


        Common_Draw("==================================================");
        Common_Draw("Convolution Example.");
        Common_Draw("Copyright (c) Firelight Technologies 2004-2024.");
        Common_Draw("==================================================");
        Common_Draw("Press %s and %s to change dry mix", Common_BtnStr(BTN_UP), Common_BtnStr(BTN_DOWN));
        Common_Draw("Press %s and %s to change wet mix", Common_BtnStr(BTN_LEFT), Common_BtnStr(BTN_RIGHT));
        Common_Draw("wet mix [%.2f] dry mix [%.2f]", wetVolume, dryVolume);
        Common_Draw("Press %s to quit", Common_BtnStr(BTN_QUIT));
        Common_Draw("");

        Common_Sleep(50);
    } while (!Common_BtnPress(BTN_QUIT));

    /*
        Shut down
    */
    result = sound->release();
    ERRCHECK(result);
    result = mainGroup->release();
    ERRCHECK(result);
    result = reverbGroup->removeDSP(reverbUnit);
    ERRCHECK(result);
    result = reverbUnit->disconnectAll(true, true);
    ERRCHECK(result);
    result = reverbUnit->release();
    ERRCHECK(result);
    result = reverbGroup->release();
    ERRCHECK(result);
    result = system->close();
    ERRCHECK(result);
    result = system->release();
    ERRCHECK(result);

    Common_Close();

    return 0;
}
