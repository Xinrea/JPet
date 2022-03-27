/*==============================================================================
Channel Groups Example
Copyright (c), Firelight Technologies Pty, Ltd 2004-2024.

This example shows how to put channels into channel groups, so that you can
affect a group of channels at a time instead of just one.

For information on using FMOD example code in your own programs, visit
https://www.fmod.com/legal
==============================================================================*/
#include "fmod.hpp"
#include "common.h"

int FMOD_Main()
{
    FMOD::System       *system;
    FMOD::Sound        *sound[6];
    FMOD::Channel      *channel[6];
    FMOD::ChannelGroup *groupA, *groupB, *masterGroup;
    FMOD_RESULT         result;
    int                 count;
    void               *extradriverdata = 0;

    Common_Init(&extradriverdata);

    /*
        Create a System object and initialize.
    */
    result = FMOD::System_Create(&system);
    ERRCHECK(result);

    result = system->init(32, FMOD_INIT_NORMAL, extradriverdata);
    ERRCHECK(result);

    result = system->createSound(Common_MediaPath("drumloop.wav"), FMOD_LOOP_NORMAL, 0, &sound[0]);
    ERRCHECK(result);
    result = system->createSound(Common_MediaPath("jaguar.wav"), FMOD_LOOP_NORMAL, 0, &sound[1]);
    ERRCHECK(result);
    result = system->createSound(Common_MediaPath("swish.wav"), FMOD_LOOP_NORMAL, 0, &sound[2]);
    ERRCHECK(result);
    result = system->createSound(Common_MediaPath("c.ogg"), FMOD_LOOP_NORMAL, 0, &sound[3]);
    ERRCHECK(result);
    result = system->createSound(Common_MediaPath("d.ogg"), FMOD_LOOP_NORMAL, 0, &sound[4]);
    ERRCHECK(result);
    result = system->createSound(Common_MediaPath("e.ogg"), FMOD_LOOP_NORMAL, 0, &sound[5]);
    ERRCHECK(result);

    result = system->createChannelGroup("Group A", &groupA);
    ERRCHECK(result);
    result = system->createChannelGroup("Group B", &groupB);
    ERRCHECK(result);

    result = system->getMasterChannelGroup(&masterGroup);
    ERRCHECK(result);

    /*
        Instead of being independent, set the group A and B to be children of the master group.
    */
    result = masterGroup->addGroup(groupA);
    ERRCHECK(result);

    result = masterGroup->addGroup(groupB);
    ERRCHECK(result);

    /*
        Start all the sounds.
    */
    for (count = 0; count < 6; count++)
    {
        result = system->playSound(sound[count], 0, true, &channel[count]);
        ERRCHECK(result);
        
        result = channel[count]->setChannelGroup((count < 3) ? groupA : groupB);
        ERRCHECK(result);
        
        result = channel[count]->setPaused(false);
        ERRCHECK(result);
    }   

    /*
        Change the volume of each group, just because we can! (reduce overall noise).
    */
    result = groupA->setVolume(0.5f);
    ERRCHECK(result);
    result = groupB->setVolume(0.5f);
    ERRCHECK(result);

    /*
        Main loop.
    */
    do
    {
        Common_Update();

        if (Common_BtnPress(BTN_ACTION1))
        {
            bool mute = true;
            groupA->getMute(&mute);
            groupA->setMute(!mute);
        }

        if (Common_BtnPress(BTN_ACTION2))
        {
            bool mute = true;
            groupB->getMute(&mute);
            groupB->setMute(!mute);
        }

        if (Common_BtnPress(BTN_ACTION3))
        {
            bool mute = true;
            masterGroup->getMute(&mute);
            masterGroup->setMute(!mute);
        }

        result = system->update();
        ERRCHECK(result);

        {
            int channelsplaying = 0;

            result = system->getChannelsPlaying(&channelsplaying, NULL);
            ERRCHECK(result);

            Common_Draw("==================================================");
            Common_Draw("Channel Groups Example.");
            Common_Draw("Copyright (c) Firelight Technologies 2004-2024.");
            Common_Draw("==================================================");
            Common_Draw("");
            Common_Draw("Group A : drumloop.wav, jaguar.wav, swish.wav");
            Common_Draw("Group B : c.ogg, d.ogg, e.ogg");
            Common_Draw("");
            Common_Draw("Press %s to mute/unmute group A", Common_BtnStr(BTN_ACTION1));
            Common_Draw("Press %s to mute/unmute group B", Common_BtnStr(BTN_ACTION2));
            Common_Draw("Press %s to mute/unmute master group", Common_BtnStr(BTN_ACTION3));
            Common_Draw("Press %s to quit", Common_BtnStr(BTN_QUIT));
            Common_Draw("");
            Common_Draw("Channels playing %d", channelsplaying);
        }

        Common_Sleep(50);
    } while (!Common_BtnPress(BTN_QUIT));


    /*
        A little fade out over 2 seconds.
    */
    {
        float pitch = 1.0f;
        float vol = 1.0f;

        for (count = 0; count < 200; count++)
        {
            masterGroup->setPitch(pitch);
            masterGroup->setVolume(vol);

            vol   -= (1.0f / 200.0f);
            pitch -= (0.5f / 200.0f);

            result = system->update();
            ERRCHECK(result);

            Common_Sleep(10);
        }
    }

    /*
        Shut down.
    */
    for (count = 0; count < 6; count++)
    {
        result = sound[count]->release();
        ERRCHECK(result);
    }

    result = groupA->release();
    ERRCHECK(result);
    result = groupB->release();
    ERRCHECK(result);

    result = system->close();
    ERRCHECK(result);
    result = system->release();
    ERRCHECK(result);

    Common_Close();

    return 0;
}
