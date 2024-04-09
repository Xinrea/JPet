/*==============================================================================
Load From Memory Example
Copyright (c), Firelight Technologies Pty, Ltd 2004-2024.

This example is simply a variant of the [Play Sound Example](play_sound.html), 
but it loads the data into memory then uses the 'load from memory' feature of 
System::createSound.

For information on using FMOD example code in your own programs, visit
https://www.fmod.com/legal
==============================================================================*/
#include "fmod.hpp"
#include "common.h"

int FMOD_Main()
{
    FMOD::System     *system;
    FMOD::Sound      *sound1, *sound2, *sound3;
    FMOD::Channel    *channel = 0;
    FMOD_RESULT       result;
    void             *extradriverdata = 0;
    void             *buff = 0;
    int               length = 0;
    FMOD_CREATESOUNDEXINFO exinfo;
    
    Common_Init(&extradriverdata);

    /*
        Create a System object and initialize
    */
    result = FMOD::System_Create(&system);
    ERRCHECK(result);

    result = system->init(32, FMOD_INIT_NORMAL, extradriverdata);
    ERRCHECK(result);
    
    Common_LoadFileMemory(Common_MediaPath("drumloop.wav"), &buff, &length);
    memset(&exinfo, 0, sizeof(FMOD_CREATESOUNDEXINFO));
    exinfo.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);
    exinfo.length = length;

    result = system->createSound((const char *)buff, FMOD_OPENMEMORY | FMOD_LOOP_OFF, &exinfo, &sound1);
    ERRCHECK(result);
    Common_UnloadFileMemory(buff); // don't need the original memory any more.  Note!  If loading as a stream, the memory must stay active so do not free it!

    Common_LoadFileMemory(Common_MediaPath("jaguar.wav"), &buff, &length);
    memset(&exinfo, 0, sizeof(FMOD_CREATESOUNDEXINFO));
    exinfo.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);
    exinfo.length = length;    
   
    result = system->createSound((const char *)buff, FMOD_OPENMEMORY, &exinfo, &sound2);
    ERRCHECK(result);
    Common_UnloadFileMemory(buff); // don't need the original memory any more.  Note!  If loading as a stream, the memory must stay active so do not free it!
    
    Common_LoadFileMemory(Common_MediaPath("swish.wav"), &buff, &length);
    memset(&exinfo, 0, sizeof(FMOD_CREATESOUNDEXINFO));
    exinfo.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);
    exinfo.length = length;    

    result = system->createSound((const char *)buff, FMOD_OPENMEMORY, &exinfo, &sound3);
    ERRCHECK(result);
    Common_UnloadFileMemory(buff); // don't need the original memory any more.  Note!  If loading as a stream, the memory must stay active so do not free it!

    /*
        Main loop
    */
    do
    {
        Common_Update();

        if (Common_BtnPress(BTN_ACTION1))
        {
            result = system->playSound(sound1, 0, false, &channel);
            ERRCHECK(result);
        }

        if (Common_BtnPress(BTN_ACTION2))
        {
            result = system->playSound(sound2, 0, false, &channel);
            ERRCHECK(result);
        }

        if (Common_BtnPress(BTN_ACTION3))
        {
            result = system->playSound(sound3, 0, false, &channel);
            ERRCHECK(result);
        }

        result = system->update();
        ERRCHECK(result);

        {
            unsigned int ms = 0;
            unsigned int lenms = 0;
            bool         playing = 0;
            bool         paused = 0;
            int          channelsplaying = 0;

            if (channel)
            {
                FMOD::Sound *currentsound = 0;

                result = channel->isPlaying(&playing);
                if ((result != FMOD_OK) && (result != FMOD_ERR_INVALID_HANDLE) && (result != FMOD_ERR_CHANNEL_STOLEN))
                {
                    ERRCHECK(result);
                }

                result = channel->getPaused(&paused);
                if ((result != FMOD_OK) && (result != FMOD_ERR_INVALID_HANDLE) && (result != FMOD_ERR_CHANNEL_STOLEN))
                {
                    ERRCHECK(result);
                }

                result = channel->getPosition(&ms, FMOD_TIMEUNIT_MS);
                if ((result != FMOD_OK) && (result != FMOD_ERR_INVALID_HANDLE) && (result != FMOD_ERR_CHANNEL_STOLEN))
                {
                    ERRCHECK(result);
                }
               
                channel->getCurrentSound(&currentsound);
                if (currentsound)
                {
                    result = currentsound->getLength(&lenms, FMOD_TIMEUNIT_MS);
                    if ((result != FMOD_OK) && (result != FMOD_ERR_INVALID_HANDLE) && (result != FMOD_ERR_CHANNEL_STOLEN))
                    {
                        ERRCHECK(result);
                    }
                }
            }

            system->getChannelsPlaying(&channelsplaying, NULL);

            Common_Draw("==================================================");
            Common_Draw("Load From Memory Example.");
            Common_Draw("Copyright (c) Firelight Technologies 2004-2024.");
            Common_Draw("==================================================");
            Common_Draw("");
            Common_Draw("Press %s to play a mono sound (drumloop)", Common_BtnStr(BTN_ACTION1));
            Common_Draw("Press %s to play a mono sound (jaguar)", Common_BtnStr(BTN_ACTION2));
            Common_Draw("Press %s to play a stereo sound (swish)", Common_BtnStr(BTN_ACTION3));
            Common_Draw("Press %s to quit", Common_BtnStr(BTN_QUIT));
            Common_Draw("");
            Common_Draw("Time %02d:%02d:%02d/%02d:%02d:%02d : %s", ms / 1000 / 60, ms / 1000 % 60, ms / 10 % 100, lenms / 1000 / 60, lenms / 1000 % 60, lenms / 10 % 100, paused ? "Paused " : playing ? "Playing" : "Stopped");
            Common_Draw("Channels Playing %2d", channelsplaying);
        }

        Common_Sleep(50);
    } while (!Common_BtnPress(BTN_QUIT));

    /*
        Shut down
    */
    result = sound1->release();
    ERRCHECK(result);
    result = sound2->release();
    ERRCHECK(result);
    result = sound3->release();
    ERRCHECK(result);
    result = system->close();
    ERRCHECK(result);
    result = system->release();
    ERRCHECK(result);

    Common_Close();

    return 0;
}
