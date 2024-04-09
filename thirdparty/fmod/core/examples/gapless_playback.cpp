/*==============================================================================
Gapless Playback Example
Copyright (c), Firelight Technologies Pty, Ltd 2004-2024.

This example shows how to schedule channel playback into the future with sample
accuracy.  Use several scheduled channels to synchronize 2 or more sounds.

For information on using FMOD example code in your own programs, visit
https://www.fmod.com/legal
==============================================================================*/
#include "fmod.hpp"
#include "common.h"

enum NOTE
{
    NOTE_C,
    NOTE_D,
    NOTE_E,
};

NOTE note[] = 
{
    NOTE_E,   /* Ma-    */
    NOTE_D,   /* ry     */
    NOTE_C,   /* had    */
    NOTE_D,   /* a      */
    NOTE_E,   /* lit-   */
    NOTE_E,   /* tle    */
    NOTE_E,   /* lamb,  */
    NOTE_E,   /* .....  */
    NOTE_D,   /* lit-   */
    NOTE_D,   /* tle    */
    NOTE_D,   /* lamb,  */
    NOTE_D,   /* .....  */
    NOTE_E,   /* lit-   */
    NOTE_E,   /* tle    */
    NOTE_E,   /* lamb,  */
    NOTE_E,   /* .....  */

    NOTE_E,   /* Ma-    */
    NOTE_D,   /* ry     */
    NOTE_C,   /* had    */
    NOTE_D,   /* a      */
    NOTE_E,   /* lit-   */
    NOTE_E,   /* tle    */
    NOTE_E,   /* lamb,  */
    NOTE_E,   /* its    */
    NOTE_D,   /* fleece */
    NOTE_D,   /* was    */
    NOTE_E,   /* white  */
    NOTE_D,   /* as     */
    NOTE_C,   /* snow.  */
    NOTE_C,   /* .....  */
    NOTE_C,   /* .....  */
    NOTE_C,   /* .....  */
};

int FMOD_Main()
{
    FMOD::System           *system;
    FMOD::Sound            *sound[3];
    FMOD::Channel          *channel = 0;
    FMOD::ChannelGroup     *channelgroup = 0;
    FMOD_RESULT             result;
    unsigned int            dsp_block_len, count;
    int                     outputrate = 0;
    void                   *extradriverdata = 0;
    
    Common_Init(&extradriverdata);

    /*
        Create a System object and initialize.
    */
    result = FMOD::System_Create(&system);
    ERRCHECK(result);
    
    result = system->init(100, FMOD_INIT_NORMAL, extradriverdata);
    ERRCHECK(result);

    /*
        Get information needed later for scheduling.  The mixer block size, and the output rate of the mixer.
    */
    result = system->getDSPBufferSize(&dsp_block_len, 0);
    ERRCHECK(result);

    result = system->getSoftwareFormat(&outputrate, 0, 0);
    ERRCHECK(result);

    /*
        Load 3 sounds - these are just sine wave tones at different frequencies.  C, D and E on the musical scale.
    */
    result = system->createSound(Common_MediaPath("c.ogg"), FMOD_DEFAULT, 0, &sound[NOTE_C]);
    ERRCHECK(result);
    result = system->createSound(Common_MediaPath("d.ogg"), FMOD_DEFAULT, 0, &sound[NOTE_D]);
    ERRCHECK(result);
    result = system->createSound(Common_MediaPath("e.ogg"), FMOD_DEFAULT, 0, &sound[NOTE_E]);
    ERRCHECK(result);

    /* 
        Create a channelgroup that the channels will play on.  We can use this channelgroup as our clock reference. 
        It also means we can pause and pitch bend the channelgroup, without affecting the offsets of the delays, because the channelgroup clock
        which the channels feed off, will be pausing and speeding up/slowing down and still keeping the children in sync.
    */
    result = system->createChannelGroup("Parent", &channelgroup);
    ERRCHECK(result);

    unsigned int numsounds = sizeof(note) / sizeof(note[0]);

    /*
        Play all the sounds at once! Space them apart with set delay though so that they sound like they play in order.
    */
    for (count = 0; count < numsounds; count++)
    {
        static unsigned long long clock_start = 0;
        unsigned int slen;
        FMOD::Sound *s = sound[note[count]];                            /* Pick a note from our tune. */

        result = system->playSound(s, channelgroup, true, &channel);    /* Play the sound on the channelgroup we want to use as the parent clock reference (for setDelay further down) */
        ERRCHECK(result);

        if (!clock_start)
        {
            result = channel->getDSPClock(0, &clock_start);
            ERRCHECK(result);

            clock_start += (dsp_block_len * 2);                         /* Start the sound into the future, by 2 mixer blocks worth. */
                                                                        /* Should be enough to avoid the mixer catching up and hitting the clock value before we've finished setting up everything. */
                                                                        /* Alternatively the channelgroup we're basing the clock on could be paused to stop it ticking. */
        }
        else
        {
            float freq;

            result = s->getLength(&slen, FMOD_TIMEUNIT_PCM);            /* Get the length of the sound in samples. */
            ERRCHECK(result);

            result = s->getDefaults(&freq, 0);                          /* Get the default frequency that the sound was recorded at. */
            ERRCHECK(result);

            slen = (unsigned int)((float)slen / freq * outputrate);     /* Convert the length of the sound to 'output samples' for the output timeline. */
            
            clock_start += slen;                                        /* Place the sound clock start time to this value after the last one. */
        }

        result = channel->setDelay(clock_start, 0, false);              /* Schedule the channel to start in the future at the newly calculated channelgroup clock value. */
        ERRCHECK(result);

        result = channel->setPaused(false);                             /* Unpause the sound.  Note that you won't hear the sounds, they are scheduled into the future. */
        ERRCHECK(result);
    }

    /*
        Main loop.
    */
    do
    {
        Common_Update();

        if (Common_BtnPress(BTN_ACTION1))                               /* Pausing the channelgroup as the clock parent, will pause any scheduled sounds from continuing */
        {                                                               /* If you paused the channel, this would not stop the clock it is delayed against from ticking,  */
            bool paused;                                                /* and you'd have to recalculate the delay for the channel into the future again before it was unpaused. */
            result = channelgroup->getPaused(&paused);
            ERRCHECK(result);
            result = channelgroup->setPaused(!paused);
            ERRCHECK(result);
        }
        if (Common_BtnPress(BTN_ACTION2)) 
        {                                 
            for (count = 0; count < 50; count++)
            {
                float pitch;
                result = channelgroup->getPitch(&pitch);
                ERRCHECK(result);
                pitch += 0.01f;
                result = channelgroup->setPitch(pitch);
                ERRCHECK(result);

                result = system->update();
                ERRCHECK(result);

                Common_Sleep(10);
            }
        }
        if (Common_BtnPress(BTN_ACTION3)) 
        {                                 
            for (count = 0; count < 50; count++)
            {
                float pitch;
                result = channelgroup->getPitch(&pitch);
                ERRCHECK(result);

                if (pitch > 0.1f)
                {
                    pitch -= 0.01f;
                }
                result = channelgroup->setPitch(pitch);
                ERRCHECK(result);
                
                result = system->update();
                ERRCHECK(result);

                Common_Sleep(10);
            }
        }

        result = system->update();
        ERRCHECK(result);

        /*
            Print some information
        */
        {
            bool    playing = false;
            bool    paused = false;
            int     chansplaying;

            if (channelgroup)
            {
                result = channelgroup->isPlaying(&playing);
                if ((result != FMOD_OK) && (result != FMOD_ERR_INVALID_HANDLE))
                {
                    ERRCHECK(result);
                }

                result = channelgroup->getPaused(&paused);
                if ((result != FMOD_OK) && (result != FMOD_ERR_INVALID_HANDLE))
                {
                    ERRCHECK(result);
                }
            }

            result = system->getChannelsPlaying(&chansplaying, NULL);
            ERRCHECK(result);

            Common_Draw("==================================================");
            Common_Draw("Gapless Playback example.");
            Common_Draw("Copyright (c) Firelight Technologies 2004-2024.");
            Common_Draw("==================================================");
            Common_Draw("");
            Common_Draw("Press %s to toggle pause", Common_BtnStr(BTN_ACTION1));
            Common_Draw("Press %s to increase pitch", Common_BtnStr(BTN_ACTION2));
            Common_Draw("Press %s to decrease pitch", Common_BtnStr(BTN_ACTION3));
            Common_Draw("Press %s to quit", Common_BtnStr(BTN_QUIT));
            Common_Draw("");
            Common_Draw("Channels Playing %d : %s", chansplaying, paused ? "Paused " : playing ? "Playing" : "Stopped");
        }

        Common_Sleep(50);
    } while (!Common_BtnPress(BTN_QUIT));

    /*
        Shut down
    */
    result = sound[NOTE_C]->release();
    ERRCHECK(result);
    result = sound[NOTE_D]->release();
    ERRCHECK(result);
    result = sound[NOTE_E]->release();
    ERRCHECK(result);

    result = system->close();
    ERRCHECK(result);
    result = system->release();
    ERRCHECK(result);

    Common_Close();

    return 0;
}
