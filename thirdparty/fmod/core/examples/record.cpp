/*==============================================================================
Record example
Copyright (c), Firelight Technologies Pty, Ltd 2004-2024.

This example shows how to record continuously and play back the same data while
keeping a specified latency between the two. This is achieved by delaying the
start of playback until the specified number of milliseconds has been recorded.
At runtime the playback speed will be slightly altered to compensate for any
drift in either play or record drivers.

For information on using FMOD example code in your own programs, visit
https://www.fmod.com/legal
==============================================================================*/
#include "fmod.hpp"
#include "common.h"

#define LATENCY_MS      (50) /* Some devices will require higher latency to avoid glitches */
#define DRIFT_MS        (1)
#define DEVICE_INDEX    (0)

int FMOD_Main()
{
    FMOD::Channel *channel = NULL;
    unsigned int samplesRecorded = 0;
    unsigned int samplesPlayed = 0;
    bool dspEnabled = false;

    void *extraDriverData = NULL;
    Common_Init(&extraDriverData);

    /*
        Create a System object and initialize.
    */
    FMOD::System *system = NULL;
    FMOD_RESULT result = FMOD::System_Create(&system);
    ERRCHECK(result);

    result = system->init(100, FMOD_INIT_NORMAL, extraDriverData);
    ERRCHECK(result);

    int numDrivers = 0;
    result = system->getRecordNumDrivers(NULL, &numDrivers);
    ERRCHECK(result);

    if (numDrivers == 0)
    {
        Common_Fatal("No recording devices found/plugged in!  Aborting.");
    }

    /*
        Determine latency in samples.
    */
    int nativeRate = 0;
    int nativeChannels = 0;
    result = system->getRecordDriverInfo(DEVICE_INDEX, NULL, 0, NULL, &nativeRate, NULL, &nativeChannels, NULL);
    ERRCHECK(result);

    unsigned int driftThreshold = (nativeRate * DRIFT_MS) / 1000;       /* The point where we start compensating for drift */
    unsigned int desiredLatency = (nativeRate * LATENCY_MS) / 1000;     /* User specified latency */
    unsigned int adjustedLatency = desiredLatency;                      /* User specified latency adjusted for driver update granularity */
    int actualLatency = desiredLatency;                                 /* Latency measured once playback begins (smoothened for jitter) */

    /*
        Create user sound to record into, then start recording.
    */
    FMOD_CREATESOUNDEXINFO exinfo = {0};
    exinfo.cbsize           = sizeof(FMOD_CREATESOUNDEXINFO);
    exinfo.numchannels      = nativeChannels;
    exinfo.format           = FMOD_SOUND_FORMAT_PCM16;
    exinfo.defaultfrequency = nativeRate;
    exinfo.length           = nativeRate * sizeof(short) * nativeChannels; /* 1 second buffer, size here doesn't change latency */
    
    FMOD::Sound *sound = NULL;
    result = system->createSound(0, FMOD_LOOP_NORMAL | FMOD_OPENUSER, &exinfo, &sound);
    ERRCHECK(result);

    result = system->recordStart(DEVICE_INDEX, sound, true);
    ERRCHECK(result);

    unsigned int soundLength = 0;
    result = sound->getLength(&soundLength, FMOD_TIMEUNIT_PCM);
    ERRCHECK(result);

    /*
        Main loop
    */
    do
    {
        Common_Update();

        /*
            Add a DSP effect -- just for fun
        */
        if (Common_BtnPress(BTN_ACTION1))
        {
            FMOD_REVERB_PROPERTIES propOn = FMOD_PRESET_CONCERTHALL;
            FMOD_REVERB_PROPERTIES propOff = FMOD_PRESET_OFF;

            dspEnabled = !dspEnabled;

            result = system->setReverbProperties(0, dspEnabled ? &propOn : &propOff);
            ERRCHECK(result);
        }

        result = system->update();
        ERRCHECK(result);
       
        /*
            Determine how much has been recorded since we last checked
        */
        unsigned int recordPos = 0;
        result = system->getRecordPosition(DEVICE_INDEX, &recordPos);
        if (result != FMOD_ERR_RECORD_DISCONNECTED)
        {
            ERRCHECK(result);
        }

        static unsigned int lastRecordPos = 0;
        unsigned int recordDelta = (recordPos >= lastRecordPos) ? (recordPos - lastRecordPos) : (recordPos + soundLength - lastRecordPos);
        lastRecordPos = recordPos;
        samplesRecorded += recordDelta;

        static unsigned int minRecordDelta = (unsigned int)-1;
        if (recordDelta && (recordDelta < minRecordDelta))
        {
            minRecordDelta = recordDelta; /* Smallest driver granularity seen so far */
            adjustedLatency = (recordDelta <= desiredLatency) ? desiredLatency : recordDelta; /* Adjust our latency if driver granularity is high */
        }
        
        /*
            Delay playback until our desired latency is reached.
        */
        if (!channel && samplesRecorded >= adjustedLatency)
        {
            result = system->playSound(sound, 0, false, &channel);
            ERRCHECK(result);
        }

        if (channel)
        {
            /*
                Stop playback if recording stops.
            */
            bool isRecording = false;
            result = system->isRecording(DEVICE_INDEX, &isRecording);
            if (result != FMOD_ERR_RECORD_DISCONNECTED)
            {
                ERRCHECK(result);
            }

            if (!isRecording)
            {
                result = channel->setPaused(true);
                ERRCHECK(result);
            }

            /*
                Determine how much has been played since we last checked.
            */
            unsigned int playPos = 0;
            result = channel->getPosition(&playPos, FMOD_TIMEUNIT_PCM);
            ERRCHECK(result);

            static unsigned int lastPlayPos = 0;
            unsigned int playDelta = (playPos >= lastPlayPos) ? (playPos - lastPlayPos) : (playPos + soundLength - lastPlayPos);
            lastPlayPos = playPos;
            samplesPlayed += playDelta;
            
            /*
                Compensate for any drift.
            */
            int latency = samplesRecorded - samplesPlayed;
            actualLatency = (int)((0.97f * actualLatency) + (0.03f * latency));

            int playbackRate = nativeRate;
            if (actualLatency < (int)(adjustedLatency - driftThreshold)) 
            {
                /* Play position is catching up to the record position, slow playback down by 2% */
                playbackRate = nativeRate - (nativeRate / 50); 
            }
            else if (actualLatency > (int)(adjustedLatency + driftThreshold))
            {
                /* Play position is falling behind the record position, speed playback up by 2% */
                playbackRate = nativeRate + (nativeRate / 50);
            }

            channel->setFrequency((float)playbackRate);
            ERRCHECK(result);
        }

        Common_Draw("==================================================");
        Common_Draw("Record Example.");
        Common_Draw("Copyright (c) Firelight Technologies 2004-2024.");
        Common_Draw("==================================================");
        Common_Draw("");
        Common_Draw("Adjust LATENCY define to compensate for stuttering");
        Common_Draw("Current value is %dms", LATENCY_MS);
        Common_Draw("");
        Common_Draw("Press %s to %s DSP effect", Common_BtnStr(BTN_ACTION1), dspEnabled ? "disable" : "enable");
        Common_Draw("Press %s to quit", Common_BtnStr(BTN_QUIT));
        Common_Draw("");
        Common_Draw("Adjusted latency: %4d (%dms)", adjustedLatency, adjustedLatency * 1000 / nativeRate);
        Common_Draw("Actual latency:   %4d (%dms)", actualLatency, actualLatency * 1000 / nativeRate);
        Common_Draw("");
        Common_Draw("Recorded: %5d (%ds)", samplesRecorded, samplesRecorded / nativeRate);
        Common_Draw("Played:   %5d (%ds)", samplesPlayed, samplesPlayed / nativeRate);

        Common_Sleep(10);
    } while (!Common_BtnPress(BTN_QUIT));

    /*
        Shut down
    */
    result = sound->release();
    ERRCHECK(result);
    result = system->release();
    ERRCHECK(result);

    Common_Close();

    return 0;
}
