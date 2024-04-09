/*==============================================================================
Record enumeration example
Copyright (c), Firelight Technologies Pty, Ltd 2004-2024.

This example shows how to enumerate the available recording drivers on this
device. It demonstrates how the enumerated list changes as microphones are
attached and detached. It also shows that you can record from multi mics at
the same time.

Please note to minimize latency care should be taken to control the number
of samples between the record position and the play position. Check the record
example for details on this process.

For information on using FMOD example code in your own programs, visit
https://www.fmod.com/legal
==============================================================================*/
#include "fmod.hpp"
#include "common.h"

static const int MAX_DRIVERS = 16;
static const int MAX_DRIVERS_IN_VIEW = 3;

struct RECORD_STATE
{
    FMOD::Sound *sound;
    FMOD::Channel *channel;
};

FMOD_RESULT F_CALLBACK SystemCallback(FMOD_SYSTEM* /*system*/, FMOD_SYSTEM_CALLBACK_TYPE /*type*/, void *, void *, void *userData)
{
    int *recordListChangedCount = (int *)userData;
    *recordListChangedCount = *recordListChangedCount + 1;

    return FMOD_OK;
}

int FMOD_Main()
{
    int scroll = 0;
    int cursor = 0;
    RECORD_STATE record[MAX_DRIVERS] = { };

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

    /*
        Setup a callback so we can be notified if the record list has changed.
    */
    int recordListChangedCount = 0;
    result = system->setUserData(&recordListChangedCount);
    ERRCHECK(result);

    result = system->setCallback(&SystemCallback, FMOD_SYSTEM_CALLBACK_RECORDLISTCHANGED);
    ERRCHECK(result);
    
    /*
        Main loop
    */
    do
    {
        Common_Update();

        int numDrivers = 0;
        int numConnected = 0;
        result = system->getRecordNumDrivers(&numDrivers, &numConnected);
        ERRCHECK(result);

        numDrivers = Common_Min(numDrivers, MAX_DRIVERS); /* Clamp the reported number of drivers to simplify this example */

        if (Common_BtnPress(BTN_ACTION1))
        {
            bool isRecording = false;
            system->isRecording(cursor, &isRecording);

            if (isRecording)
            {
                system->recordStop(cursor);
            }
            else
            {
                /* Clean up previous record sound */
                if (record[cursor].sound)
                {
                    result = record[cursor].sound->release();
                    ERRCHECK(result);
                }

                /* Query device native settings and start a recording */
                int nativeRate = 0;
                int nativeChannels = 0;
                result = system->getRecordDriverInfo(cursor, NULL, 0, NULL, &nativeRate, NULL, &nativeChannels, NULL);
                ERRCHECK(result);

                FMOD_CREATESOUNDEXINFO exinfo = {0};
                exinfo.cbsize           = sizeof(FMOD_CREATESOUNDEXINFO);
                exinfo.numchannels      = nativeChannels;
                exinfo.format           = FMOD_SOUND_FORMAT_PCM16;
                exinfo.defaultfrequency = nativeRate;
                exinfo.length           = nativeRate * sizeof(short) * nativeChannels; /* 1 second buffer, size here doesn't change latency */
    
                result = system->createSound(0, FMOD_LOOP_NORMAL | FMOD_OPENUSER, &exinfo, &record[cursor].sound);
                ERRCHECK(result);

                result = system->recordStart(cursor, record[cursor].sound, true);
                if (result != FMOD_ERR_RECORD_DISCONNECTED)
                {
                    ERRCHECK(result);
                }
            }
        }
        else if (Common_BtnPress(BTN_ACTION2))
        {
            bool isPlaying = false;
            record[cursor].channel->isPlaying(&isPlaying);

            if (isPlaying)
            {
                record[cursor].channel->stop();
            }
            else if (record[cursor].sound)
            {
                result = system->playSound(record[cursor].sound, NULL, false, &record[cursor].channel);
                ERRCHECK(result);
            }
        }
        else if (Common_BtnPress(BTN_UP))
        {
            cursor = Common_Max(cursor - 1, 0);
            scroll = Common_Max(scroll - 1, 0);
        }
        else if (Common_BtnPress(BTN_DOWN))
        {
            if (numDrivers)
            {
                cursor = Common_Min(cursor + 1, numDrivers - 1);
            }

            if (numDrivers > MAX_DRIVERS_IN_VIEW)
            {
                scroll = Common_Min(scroll + 1, numDrivers - MAX_DRIVERS_IN_VIEW);
            }
        }

        result = system->update();
        ERRCHECK(result);

        Common_Draw("==================================================");
        Common_Draw("Record Enumeration Example.");
        Common_Draw("Copyright (c) Firelight Technologies 2004-2024.");
        Common_Draw("==================================================");
        Common_Draw("");
        Common_Draw("Record list has updated %d time(s).", recordListChangedCount);
        Common_Draw("Currently %d device(s) plugged in.", numConnected);
        Common_Draw("");
        Common_Draw("Press %s to quit", Common_BtnStr(BTN_QUIT));
        Common_Draw("Press %s and %s to scroll list", Common_BtnStr(BTN_UP), Common_BtnStr(BTN_DOWN));
        Common_Draw("Press %s start / stop recording", Common_BtnStr(BTN_ACTION1));
        Common_Draw("Press %s start / stop playback", Common_BtnStr(BTN_ACTION2));
        Common_Draw("");
        int numDisplay = Common_Min(numDrivers, MAX_DRIVERS_IN_VIEW);
        for (int i = scroll; i < scroll + numDisplay; i++)
        {
            char name[256];
            int sampleRate;
            int channels;
            FMOD_GUID guid;
            FMOD_DRIVER_STATE state;

            result = system->getRecordDriverInfo(i, name, sizeof(name), &guid, &sampleRate, NULL, &channels, &state);
            ERRCHECK(result);

            bool isRecording = false;
            system->isRecording(i, &isRecording);

            bool isPlaying = false;
            record[i].channel->isPlaying(&isPlaying);

            Common_Draw("%c%2d. %s%.41s", (cursor == i) ? '>' : ' ', i, (state & FMOD_DRIVER_STATE_DEFAULT) ? "(*) " : "", name);
            Common_Draw("%dKHz %dch {%08X-%04X-%04X-%04X-%02X%02X%02X%02X%02X%02X}", sampleRate / 1000, channels, guid.Data1, guid.Data2, guid.Data3, guid.Data4[0] << 8 | guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
            Common_Draw("(%s) (%s) (%s)", (state & FMOD_DRIVER_STATE_CONNECTED) ? "Connected" : "Unplugged", isRecording ? "Recording" : "Not recording", isPlaying ? "Playing" : "Not playing");
            Common_Draw("");
        }

        Common_Sleep(50);
    } while (!Common_BtnPress(BTN_QUIT));

    for (int i = 0; i < MAX_DRIVERS; i++)
    {
        if (record[i].sound)
        {
            result = record[i].sound->release();
            ERRCHECK(result);
        }
    }

    result = system->release();
    ERRCHECK(result);

    Common_Close();

    return 0;
}
