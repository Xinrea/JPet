/*==============================================================================
User Created Sound Example
Copyright (c), Firelight Technologies Pty, Ltd 2004-2024.

This example shows how create a sound with data filled by the user. It shows a
user created static sample, followed by a user created stream. The former
allocates all memory needed for the sound and is played back as a static sample, 
while the latter streams the data in chunks as it plays, using far less memory.

For information on using FMOD example code in your own programs, visit
https://www.fmod.com/legal
==============================================================================*/
#include "fmod.hpp"
#include "common.h"
#include <math.h>

FMOD_RESULT F_CALLBACK pcmreadcallback(FMOD_SOUND* /*sound*/, void *data, unsigned int datalen)
{
    static float  t1 = 0, t2 = 0;        // time
    static float  v1 = 0, v2 = 0;        // velocity
    signed short *stereo16bitbuffer = (signed short *)data;

    for (unsigned int count = 0; count < (datalen >> 2); count++)     // >>2 = 16bit stereo (4 bytes per sample)
    {
        *stereo16bitbuffer++ = (signed short)(Common_Sin(t1) * 32767.0f);    // left channel
        *stereo16bitbuffer++ = (signed short)(Common_Sin(t2) * 32767.0f);    // right channel

        t1 += 0.01f   + v1;
        t2 += 0.0142f + v2;
        v1 += (float)(Common_Sin(t1) * 0.002f);
        v2 += (float)(Common_Sin(t2) * 0.002f);
    }

    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK pcmsetposcallback(FMOD_SOUND* /*sound*/, int /*subsound*/, unsigned int /*position*/, FMOD_TIMEUNIT /*postype*/)
{
    /*
        This is useful if the user calls Channel::setPosition and you want to seek your data accordingly.
    */
    return FMOD_OK;
}

int FMOD_Main()
{
    FMOD::System           *system;
    FMOD::Sound            *sound;
    FMOD::Channel          *channel = 0;
    FMOD_RESULT             result;
    FMOD_MODE               mode = FMOD_OPENUSER | FMOD_LOOP_NORMAL;
    FMOD_CREATESOUNDEXINFO  exinfo;
    void                   *extradriverdata = 0;
    
    Common_Init(&extradriverdata);

    /*
        Create a System object and initialize.
    */
    result = FMOD::System_Create(&system);
    ERRCHECK(result);

    result = system->init(32, FMOD_INIT_NORMAL, extradriverdata);
    ERRCHECK(result);
        
    do
    {
        Common_Update();

        if (Common_BtnPress(BTN_ACTION1))
        {
            mode |= FMOD_CREATESTREAM;
        }

        result = system->update();
        ERRCHECK(result);

        Common_Draw("==================================================");
        Common_Draw("User Created Sound Example.");
        Common_Draw("Copyright (c) Firelight Technologies 2004-2024.");
        Common_Draw("==================================================");
        Common_Draw("");
        Common_Draw("Sound played here is generated in realtime. It will either play as a stream which means it is continually filled as it is playing, or it will play as a static sample, which means it is filled once as the sound is created, then when played it will just play that short loop of data.");
        Common_Draw("");
        Common_Draw("Press %s to play an infinite generated stream", Common_BtnStr(BTN_ACTION1));
        Common_Draw("Press %s to play a static looping sample", Common_BtnStr(BTN_ACTION2));
        Common_Draw("");

        Common_Sleep(50);
    } while (!Common_BtnPress(BTN_ACTION1) && !Common_BtnPress(BTN_ACTION2) && !Common_BtnPress(BTN_QUIT));

    /*
        Create and play the sound.
    */
    memset(&exinfo, 0, sizeof(FMOD_CREATESOUNDEXINFO));
    exinfo.cbsize            = sizeof(FMOD_CREATESOUNDEXINFO);  /* Required. */
    exinfo.numchannels       = 2;                               /* Number of channels in the sound. */
    exinfo.defaultfrequency  = 44100;                           /* Default playback rate of sound. */
    exinfo.decodebuffersize  = 44100;                           /* Chunk size of stream update in samples. This will be the amount of data passed to the user callback. */
    exinfo.length            = exinfo.defaultfrequency * exinfo.numchannels * sizeof(signed short) * 5; /* Length of PCM data in bytes of whole song (for Sound::getLength) */
    exinfo.format            = FMOD_SOUND_FORMAT_PCM16;         /* Data format of sound. */
    exinfo.pcmreadcallback   = pcmreadcallback;                 /* User callback for reading. */
    exinfo.pcmsetposcallback = pcmsetposcallback;               /* User callback for seeking. */

    result = system->createSound(0, mode, &exinfo, &sound);
    ERRCHECK(result);

    result = system->playSound(sound, 0, 0, &channel);
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
               
                result = sound->getLength(&lenms, FMOD_TIMEUNIT_MS);
                if ((result != FMOD_OK) && (result != FMOD_ERR_INVALID_HANDLE))
                {
                    ERRCHECK(result);
                }
            }

            Common_Draw("==================================================");
            Common_Draw("User Created Sound Example.");
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
    result = sound->release();
    ERRCHECK(result);
    result = system->close();
    ERRCHECK(result);
    result = system->release();
    ERRCHECK(result);

    Common_Close();

    return 0;
}
