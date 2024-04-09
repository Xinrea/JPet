/*==============================================================================
Play Stream Example
Copyright (c), Firelight Technologies Pty, Ltd 2004-2024.

This example shows how to simply play a stream such as an MP3 or WAV. The stream
behaviour is achieved by specifying FMOD_CREATESTREAM in the call to 
System::createSound. This makes FMOD decode the file in realtime as it plays,
instead of loading it all at once which uses far less memory in exchange for a
small runtime CPU hit.

For information on using FMOD example code in your own programs, visit
https://www.fmod.com/legal
==============================================================================*/
#include "fmod.hpp"
#include "common.h"

int FMOD_Main()
{
    FMOD::System     *system;
    FMOD::Sound      *sound, *sound_to_play;
    FMOD::Channel    *channel = 0;
    FMOD_RESULT       result;
    void             *extradriverdata = 0;
    int               numsubsounds;
    
    Common_Init(&extradriverdata);

    /*
        Create a System object and initialize.
    */
    result = FMOD::System_Create(&system);
    ERRCHECK(result);

    result = system->init(32, FMOD_INIT_NORMAL, extradriverdata);
    ERRCHECK(result);

    /*
        This example uses an FSB file, which is a preferred pack format for fmod containing multiple sounds.
        This could just as easily be exchanged with a wav/mp3/ogg file for example, but in this case you wouldnt need to call getSubSound.
        Because getNumSubSounds is called here the example would work with both types of sound file (packed vs single).
    */
    result = system->createStream(Common_MediaPath("wave_vorbis.fsb"), FMOD_LOOP_NORMAL | FMOD_2D, 0, &sound);
    ERRCHECK(result);

    result = sound->getNumSubSounds(&numsubsounds);
    ERRCHECK(result);

    if (numsubsounds)
    {
        sound->getSubSound(0, &sound_to_play);
        ERRCHECK(result);
    }
    else
    {
        sound_to_play = sound;
    }

    /*
        Play the sound.
    */
    result = system->playSound(sound_to_play, 0, false, &channel);
    ERRCHECK(result);

    /*
        Main loop.
    */
    do
    {
        Common_Update();

        if (Common_BtnPress(BTN_ACTION1))
        {
            bool paused;
            result = channel->getPaused(&paused);
            ERRCHECK(result);
            result = channel->setPaused(!paused);
            ERRCHECK(result);
        }

        result = system->update();
        ERRCHECK(result);

        {
            unsigned int ms = 0;
            unsigned int lenms = 0;
            bool         playing = false;
            bool         paused = false;

            if (channel)
            {
                result = channel->isPlaying(&playing);
                if ((result != FMOD_OK) && (result != FMOD_ERR_INVALID_HANDLE))
                {
                    ERRCHECK(result);
                }

                result = channel->getPaused(&paused);
                if ((result != FMOD_OK) && (result != FMOD_ERR_INVALID_HANDLE))
                {
                    ERRCHECK(result);
                }

                result = channel->getPosition(&ms, FMOD_TIMEUNIT_MS);
                if ((result != FMOD_OK) && (result != FMOD_ERR_INVALID_HANDLE))
                {
                    ERRCHECK(result);
                }
               
                result = sound_to_play->getLength(&lenms, FMOD_TIMEUNIT_MS);
                if ((result != FMOD_OK) && (result != FMOD_ERR_INVALID_HANDLE))
                {
                    ERRCHECK(result);
                }
            }

            Common_Draw("==================================================");
            Common_Draw("Play Stream Example.");
            Common_Draw("Copyright (c) Firelight Technologies 2004-2024.");
            Common_Draw("==================================================");
            Common_Draw("");
            Common_Draw("Press %s to toggle pause", Common_BtnStr(BTN_ACTION1));
            Common_Draw("Press %s to quit", Common_BtnStr(BTN_QUIT));
            Common_Draw("");
            Common_Draw("Time %02d:%02d:%02d/%02d:%02d:%02d : %s", ms / 1000 / 60, ms / 1000 % 60, ms / 10 % 100, lenms / 1000 / 60, lenms / 1000 % 60, lenms / 10 % 100, paused ? "Paused " : playing ? "Playing" : "Stopped");
        }

        Common_Sleep(50);
    } while (!Common_BtnPress(BTN_QUIT));

    /*
        Shut down
    */
    result = sound->release();  /* Release the parent, not the sound that was retrieved with getSubSound. */
    ERRCHECK(result);
    result = system->close();
    ERRCHECK(result);
    result = system->release();
    ERRCHECK(result);

    Common_Close();

    return 0;
}
