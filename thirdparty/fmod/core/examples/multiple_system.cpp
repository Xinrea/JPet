/*==============================================================================
Multiple System Example
Copyright (c), Firelight Technologies Pty, Ltd 2004-2024.

This example shows how to play sounds on two different output devices from the
same application. It creates two FMOD::System objects, selects a different sound
device for each, then allows the user to play one sound on each device.

Note that sounds created on device A cannot be played on device B and vice
versa.

For information on using FMOD example code in your own programs, visit
https://www.fmod.com/legal
==============================================================================*/
#include "fmod.hpp"
#include "common.h"

FMOD_RESULT fetchDriver(FMOD::System *system, int *driver)
{
    FMOD_RESULT result;
    int numdrivers;
    int selectedindex = 0;

    result = system->getNumDrivers(&numdrivers);
    ERRCHECK(result);

    if (numdrivers == 0)
    {
        result = system->setOutput(FMOD_OUTPUTTYPE_NOSOUND);
        ERRCHECK(result);
    }

    do
    {
        Common_Update();

        if (Common_BtnPress(BTN_UP) && (selectedindex != 0))
        {
            selectedindex--;
        }
        if (Common_BtnPress(BTN_DOWN) && (selectedindex != (numdrivers - 1)))
        {
            selectedindex++;
        }

        Common_Draw("==================================================");
        Common_Draw("Multiple System Example.");
        Common_Draw("Copyright (c) Firelight Technologies 2004-2024.");
        Common_Draw("==================================================");
        Common_Draw("");
        Common_Draw("Choose a device for system: 0x%p", system);
        Common_Draw("");
        Common_Draw("Use %s and %s to select.", Common_BtnStr(BTN_UP), Common_BtnStr(BTN_DOWN));
        Common_Draw("Press %s to confirm.", Common_BtnStr(BTN_ACTION1));
        Common_Draw("");
        for (int i = 0; i < numdrivers; i++)
        {
            char name[256];

            result = system->getDriverInfo(i, name, sizeof(name), 0, 0, 0, 0);
            ERRCHECK(result);

            Common_Draw("[%c] - %d. %s", selectedindex == i ? 'X' : ' ', i, name);
        }

        Common_Sleep(50);
    } while (!Common_BtnPress(BTN_ACTION1) && !Common_BtnPress(BTN_QUIT));

    *driver = selectedindex;

    return FMOD_OK;
}

int FMOD_Main()
{
    FMOD::System     *systemA, *systemB;
    FMOD::Sound      *soundA, *soundB;
    FMOD::Channel    *channelA = 0, *channelB = 0;
    FMOD_RESULT       result;
    int               driver;
    void             *extradriverdata = 0;
    
    Common_Init(&extradriverdata);

    /*
        Create Sound Card A
    */
    result = FMOD::System_Create(&systemA);
    ERRCHECK(result);

    result = fetchDriver(systemA, &driver);
    ERRCHECK(result);

    result = systemA->setDriver(driver);
    ERRCHECK(result);

    result = systemA->init(32, FMOD_INIT_NORMAL, extradriverdata);
    ERRCHECK(result);

    /*
        Create Sound Card B
    */
    result = FMOD::System_Create(&systemB);
    ERRCHECK(result);

    result = fetchDriver(systemB, &driver);
    ERRCHECK(result);

    result = systemB->setDriver(driver);
    ERRCHECK(result);

    result = systemB->init(32, FMOD_INIT_NORMAL, extradriverdata);
    ERRCHECK(result);

    /*
        Load 1 sample into each soundcard.
    */
    result = systemA->createSound(Common_MediaPath("drumloop.wav"), FMOD_LOOP_OFF, 0, &soundA);
    ERRCHECK(result);

    result = systemB->createSound(Common_MediaPath("jaguar.wav"), FMOD_DEFAULT, 0, &soundB);
    ERRCHECK(result);

    /*
        Main loop
    */
    do
    {
        Common_Update();
        
        if (Common_BtnPress(BTN_ACTION1))
        {
            result = systemA->playSound(soundA, 0, 0, &channelA);
            ERRCHECK(result);
        }

        if (Common_BtnPress(BTN_ACTION2))
        {
            result = systemB->playSound(soundB, 0, 0, &channelB);
            ERRCHECK(result);
        }

        result = systemA->update();
        ERRCHECK(result);
        result = systemB->update();
        ERRCHECK(result);

        {
            int channelsplayingA = 0;
            int channelsplayingB = 0;

            result = systemA->getChannelsPlaying(&channelsplayingA, NULL);
            ERRCHECK(result);
            result = systemB->getChannelsPlaying(&channelsplayingB, NULL);
            ERRCHECK(result);

            Common_Draw("==================================================");
            Common_Draw("Multiple System Example.");
            Common_Draw("Copyright (c) Firelight Technologies 2004-2024.");
            Common_Draw("==================================================");
            Common_Draw("");
            Common_Draw("Press %s to play a sound on device A", Common_BtnStr(BTN_ACTION1));
            Common_Draw("Press %s to play a sound on device B", Common_BtnStr(BTN_ACTION2));
            Common_Draw("Press %s to quit", Common_BtnStr(BTN_QUIT));
            Common_Draw("");
            Common_Draw("Channels playing on A: %d", channelsplayingA);
            Common_Draw("Channels playing on B: %d", channelsplayingB);
        }
        
        Common_Sleep(50);
    } while (!Common_BtnPress(BTN_QUIT));

    /*
        Shut down
    */
    result = soundA->release();
    ERRCHECK(result);
    result = systemA->close();
    ERRCHECK(result);
    result = systemA->release();
    ERRCHECK(result);

    result = soundB->release();
    ERRCHECK(result);
    result = systemB->close();
    ERRCHECK(result);
    result = systemB->release();
    ERRCHECK(result);

    Common_Close();

    return 0;
}
