/*==============================================================================
Granular Synthesis Example
Copyright (c), Firelight Technologies Pty, Ltd 2004-2024.

This example shows how you can play a string of sounds together without gaps,
using the setDelay command, to produce a granular synthesis style truck engine 
effect.

The basic operation is:

 * Play 2 sounds initially at the same time, the first sound immediately, and
   the 2nd sound with a delay calculated by the length of the first sound.
 * Call setDelay to initiate the delayed playback. setDelay is sample accurate
   and uses -output- samples as the time frame, not source samples. These
   samples are a fixed amount per second regardless of the source sound format,
   for example, 48000 samples per second if FMOD is initialized to 48khz output.
 * Output samples are calculated from source samples with a simple
   source->output sample rate conversion. i.e.
        sound_length *= output_rate
        sound_length /= sound_frequency
 * When the first sound finishes, the second one should have automatically
   started. This is a good oppurtunity to queue up the next sound. Repeat
   step 2.
 * Make sure the framerate is high enough to queue up a new sound before the
   other one finishes otherwise you will get gaps.

These sounds are not limited by format, channel count or bit depth like the 
realtimestitching example is, and can also be modified to allow for overlap,
by reducing the delay from the first sound playing to the second by the overlap
amount.

    #define USE_STREAMS = Use 2 stream instances, created while they play.
    #define USE_STREAMS = Use 6 static wavs, all loaded into memory.

For information on using FMOD example code in your own programs, visit
https://www.fmod.com/legal
==============================================================================*/
#include "fmod.hpp"
#include "common.h"

//#define USE_STREAMS

FMOD::System *gSystem;

#ifdef USE_STREAMS
#define NUMSOUNDS 3               /* Use some longer sounds, free and load them on the fly. */
FMOD::Sound *sound[2] = { 0, 0 }; /* 2 streams active, double buffer them. */
const char  *soundname[NUMSOUNDS] = { Common_MediaPath("c.ogg"),
                                      Common_MediaPath("d.ogg"),
                                      Common_MediaPath("e.ogg") };
#else
#define NUMSOUNDS 6                                     /* These sounds will be loaded into memory statically. */
FMOD::Sound *sound[NUMSOUNDS] = { 0, 0, 0, 0, 0, 0 };   /* 6 sounds active, one for each wav. */
const char  *soundname[NUMSOUNDS] = { Common_MediaPath("granular/truck_idle_off_01.wav"),
                                      Common_MediaPath("granular/truck_idle_off_02.wav"),
                                      Common_MediaPath("granular/truck_idle_off_03.wav"),
                                      Common_MediaPath("granular/truck_idle_off_04.wav"),
                                      Common_MediaPath("granular/truck_idle_off_05.wav"),
                                      Common_MediaPath("granular/truck_idle_off_06.wav") };
#endif

FMOD::Channel *queue_next_sound(int outputrate, FMOD::Channel *playingchannel, int newindex, int slot)
{
    FMOD_RESULT result;
    FMOD::Channel *newchannel;
    FMOD::Sound *newsound;
    
#ifdef USE_STREAMS  /* Create a new stream */
    FMOD_CREATESOUNDEXINFO info;
    memset(&info, 0, sizeof(FMOD_CREATESOUNDEXINFO));
    info.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);
    info.suggestedsoundtype = FMOD_SOUND_TYPE_OGGVORBIS;
    result = gSystem->createStream(soundname[newindex], FMOD_IGNORETAGS | FMOD_LOWMEM, &info, &sound[slot]);
    ERRCHECK(result);
    newsound = sound[slot];
#else   /* Use an existing sound that was passed into us */
    (void)slot;
    newsound = sound[newindex];
#endif
    
    result = gSystem->playSound(newsound, 0, true, &newchannel);
    ERRCHECK(result);
      
    if (playingchannel)
    {    
        unsigned long long startdelay = 0;
        unsigned int soundlength = 0;
        float soundfrequency;
        FMOD::Sound *playingsound;
        
        /*
            Get the start time of the playing channel.
        */
        result = playingchannel->getDelay(&startdelay, 0);
        ERRCHECK(result);
        
        /*
            Grab the length of the playing sound, and its frequency, so we can caluate where to place the new sound on the time line.
        */
        result = playingchannel->getCurrentSound(&playingsound);
        ERRCHECK(result);
        result = playingsound->getLength(&soundlength, FMOD_TIMEUNIT_PCM);
        ERRCHECK(result);
        result = playingchannel->getFrequency(&soundfrequency);
        ERRCHECK(result);
        
        /* 
            Now calculate the length of the sound in 'output samples'.  
            Ie if a 44khz sound is 22050 samples long, and the output rate is 48khz, then we want to delay by 24000 output samples.
        */
        soundlength *= outputrate;   
        soundlength /= (int)soundfrequency;
        
        startdelay += soundlength; /* Add output rate adjusted sound length, to the clock value of the sound that is currently playing */

        result = newchannel->setDelay(startdelay, 0); /* Set the delay of the new sound to the end of the old sound */
        ERRCHECK(result);
    }
    else
    {
        unsigned int bufferlength;
        unsigned long long startdelay;

        result = gSystem->getDSPBufferSize(&bufferlength, 0);
        ERRCHECK(result);

        result = newchannel->getDSPClock(0, &startdelay);
        ERRCHECK(result);

        startdelay += (2 * bufferlength);
        result = newchannel->setDelay(startdelay, 0);
        ERRCHECK(result);
    }
    
    {
        float val, variation;
        
        /*
            Randomize pitch/volume to make it sound more realistic / random.
        */
        result = newchannel->getFrequency(&val);
        ERRCHECK(result);
        variation = (((float)(rand()%10000) / 5000.0f) - 1.0f); /* -1.0 to +1.0 */
        val *= (1.0f + (variation * 0.02f));                    /* @22khz, range fluctuates from 21509 to 22491 */
        result = newchannel->setFrequency(val);
        ERRCHECK(result);

        result = newchannel->getVolume(&val);
        ERRCHECK(result);
        variation = ((float)(rand()%10000) / 10000.0f);         /*  0.0 to 1.0 */
        val *= (1.0f - (variation * 0.2f));                     /*  0.8 to 1.0 */
        result = newchannel->setVolume(val);
        ERRCHECK(result);
    }   
        
    result = newchannel->setPaused(false);
    ERRCHECK(result);
       
    return newchannel;
}

int FMOD_Main()
{
    FMOD::Channel    *channel[2] = { 0,0 };
    FMOD_RESULT       result;
    int               outputrate, slot = 0;
    void             *extradriverdata = 0;
    bool              paused = false;

    Common_Init(&extradriverdata);
    
    result = FMOD::System_Create(&gSystem);
    ERRCHECK(result);
    
    result = gSystem->init(100, FMOD_INIT_NORMAL, extradriverdata);
    ERRCHECK(result);
       
    result = gSystem->getSoftwareFormat(&outputrate, 0, 0);
    ERRCHECK(result);   
   
#if !defined(USE_STREAMS)
    for (unsigned int count = 0; count < NUMSOUNDS; count++)
    {
        result = gSystem->createSound(soundname[count], FMOD_IGNORETAGS, 0, &sound[count]);
        ERRCHECK(result);
    }
#endif

    /*
        Kick off the first 2 sounds.  First one is immediate, second one will be triggered to start after the first one.
    */
    channel[slot] = queue_next_sound(outputrate, channel[1-slot], rand()%NUMSOUNDS, slot);
    slot = 1-slot;  /* flip */
    channel[slot] = queue_next_sound(outputrate, channel[1-slot], rand()%NUMSOUNDS, slot);
    slot = 1-slot;  /* flip */

    do
    {
        bool isplaying = false;

        Common_Update();

        if (Common_BtnPress(BTN_ACTION1))
        {
            FMOD::ChannelGroup *mastergroup;

            paused = !paused;

            result = gSystem->getMasterChannelGroup(&mastergroup);
            ERRCHECK(result);
            result = mastergroup->setPaused(paused);
            ERRCHECK(result);
        }

        result = gSystem->update();
        ERRCHECK(result);

        /*
            Replace the sound that just finished with a new sound, to create endless seamless stitching!
        */
        result = channel[slot]->isPlaying(&isplaying);
        if (result != FMOD_ERR_INVALID_HANDLE)
        {
            ERRCHECK(result);
        }

        if (!isplaying && !paused)
        {
#ifdef USE_STREAMS
            /* 
                Release the sound that isn't playing any more. 
            */
            result = sound[slot]->release();       
            ERRCHECK(result);
            sound[slot] = 0;
#endif

            /*
                Replace sound that just ended with a new sound, queued up to trigger exactly after the other sound ends.
            */
            channel[slot] = queue_next_sound(outputrate, channel[1-slot], rand()%NUMSOUNDS, slot);
            slot = 1-slot;  /* flip */
        }

        Common_Draw("==================================================");
        Common_Draw("Granular Synthesis SetDelay Example.");
        Common_Draw("Copyright (c) Firelight Technologies 2004-2024.");
        Common_Draw("==================================================");
        Common_Draw("");
        Common_Draw("Toggle #define USE_STREAM on/off in code to switch between streams and static samples.");
        Common_Draw("");
        Common_Draw("Press %s to pause", Common_BtnStr(BTN_ACTION1));
        Common_Draw("Press %s to quit", Common_BtnStr(BTN_QUIT));
        Common_Draw("");
        Common_Draw("Channels are %s", paused ? "paused" : "playing");

        Common_Sleep(10);   /* If you wait too long, ie longer than the length of the shortest sound, you will get gaps. */
    } while (!Common_BtnPress(BTN_QUIT));

    /*
        Shut down
    */
    for (unsigned int count = 0; count < sizeof(sound) / sizeof(sound[0]); count++)
    {
        if (sound[count])
        {
            result = sound[count]->release();
            ERRCHECK(result);
        }
    }
    
    result = gSystem->release();
    ERRCHECK(result);

    Common_Close();

    return 0;
}
