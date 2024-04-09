/*==============================================================================
3D Example
Copyright (c), Firelight Technologies Pty, Ltd 2004-2024.

This example shows how to basic 3D positioning of sounds.

For information on using FMOD example code in your own programs, visit
https://www.fmod.com/legal
==============================================================================*/
#include "fmod.hpp"
#include "common.h"

const int   INTERFACE_UPDATETIME = 50;      // 50ms update for interface
const float DISTANCEFACTOR = 1.0f;          // Units per meter.  I.e feet would = 3.28.  centimeters would = 100.

int FMOD_Main()
{
    FMOD::System    *system;
    FMOD::Sound     *sound1, *sound2, *sound3;
    FMOD::Channel   *channel1 = 0, *channel2 = 0, *channel3 = 0;
    FMOD_RESULT      result;
    bool             listenerflag = true;
    FMOD_VECTOR      listenerpos  = { 0.0f, 0.0f, -1.0f * DISTANCEFACTOR };
    void            *extradriverdata = 0;

    Common_Init(&extradriverdata);

    /*
        Create a System object and initialize.
    */
    result = FMOD::System_Create(&system);
    ERRCHECK(result);
    
    result = system->init(100, FMOD_INIT_NORMAL, extradriverdata);
    ERRCHECK(result);
    
    /*
        Set the distance units. (meters/feet etc).
    */
    result = system->set3DSettings(1.0, DISTANCEFACTOR, 1.0f);
    ERRCHECK(result);

    /*
        Load some sounds
    */
    result = system->createSound(Common_MediaPath("drumloop.wav"), FMOD_3D, 0, &sound1);
    ERRCHECK(result);
    result = sound1->set3DMinMaxDistance(0.5f * DISTANCEFACTOR, 5000.0f * DISTANCEFACTOR);
    ERRCHECK(result);
    result = sound1->setMode(FMOD_LOOP_NORMAL);
    ERRCHECK(result);

    result = system->createSound(Common_MediaPath("jaguar.wav"), FMOD_3D, 0, &sound2);
    ERRCHECK(result);
    result = sound2->set3DMinMaxDistance(0.5f * DISTANCEFACTOR, 5000.0f * DISTANCEFACTOR);
    ERRCHECK(result);
    result = sound2->setMode(FMOD_LOOP_NORMAL);
    ERRCHECK(result);

    result = system->createSound(Common_MediaPath("swish.wav"), FMOD_2D, 0, &sound3);
    ERRCHECK(result);

    /*
        Play sounds at certain positions
    */
    {
        FMOD_VECTOR pos = { -10.0f * DISTANCEFACTOR, 0.0f, 0.0f };
        FMOD_VECTOR vel = {  0.0f, 0.0f, 0.0f };

        result = system->playSound(sound1, 0, true, &channel1);
        ERRCHECK(result);
        result = channel1->set3DAttributes(&pos, &vel);
        ERRCHECK(result);
        result = channel1->setPaused(false);
        ERRCHECK(result);
    }

    {
        FMOD_VECTOR pos = { 15.0f * DISTANCEFACTOR, 0.0f, 0.0f };
        FMOD_VECTOR vel = { 0.0f, 0.0f, 0.0f };

        result = system->playSound(sound2, 0, true, &channel2);
        ERRCHECK(result);
        result = channel2->set3DAttributes(&pos, &vel);
        ERRCHECK(result);
        result = channel2->setPaused(false);
        ERRCHECK(result);
    }

    /*
        Main loop
    */
    do
    {
        Common_Update();

        if (Common_BtnPress(BTN_ACTION1))
        {
            bool paused;
            channel1->getPaused(&paused);
            channel1->setPaused(!paused);
        }

        if (Common_BtnPress(BTN_ACTION2))
        {
            bool paused;
            channel2->getPaused(&paused);
            channel2->setPaused(!paused);
        }

        if (Common_BtnPress(BTN_ACTION3))
        {
            result = system->playSound(sound3, 0, false, &channel3);
            ERRCHECK(result);
        }

        if (Common_BtnPress(BTN_MORE))
        {
            listenerflag = !listenerflag;
        }

        if (!listenerflag)
        {
            if (Common_BtnDown(BTN_LEFT))
            {
                listenerpos.x -= 1.0f * DISTANCEFACTOR;
                if (listenerpos.x < -24 * DISTANCEFACTOR)
                {
                    listenerpos.x = -24 * DISTANCEFACTOR;
                }
            }

            if (Common_BtnDown(BTN_RIGHT))
            {
                listenerpos.x += 1.0f * DISTANCEFACTOR;
                if (listenerpos.x > 23 * DISTANCEFACTOR)
                {
                    listenerpos.x = 23 * DISTANCEFACTOR;
                }
            }
        }

        // ==========================================================================================
        // UPDATE THE LISTENER
        // ==========================================================================================
        {
            static float t = 0;
            static FMOD_VECTOR lastpos = { 0.0f, 0.0f, 0.0f };
            FMOD_VECTOR forward        = { 0.0f, 0.0f, 1.0f };
            FMOD_VECTOR up             = { 0.0f, 1.0f, 0.0f };
            FMOD_VECTOR vel;

            if (listenerflag)
            {
                listenerpos.x = (float)sin(t * 0.05f) * 24.0f * DISTANCEFACTOR; // left right pingpong
            }

            // ********* NOTE ******* READ NEXT COMMENT!!!!!
            // vel = how far we moved last FRAME (m/f), then time compensate it to SECONDS (m/s).
            vel.x = (listenerpos.x - lastpos.x) * (1000 / INTERFACE_UPDATETIME);
            vel.y = (listenerpos.y - lastpos.y) * (1000 / INTERFACE_UPDATETIME);
            vel.z = (listenerpos.z - lastpos.z) * (1000 / INTERFACE_UPDATETIME);

            // store pos for next time
            lastpos = listenerpos;

            result = system->set3DListenerAttributes(0, &listenerpos, &vel, &forward, &up);
            ERRCHECK(result);

            t += (30 * (1.0f / (float)INTERFACE_UPDATETIME));    // t is just a time value .. it increments in 30m/s steps in this example
        }

        result = system->update();
        ERRCHECK(result);

        // Create small visual display.
        char s[80] = "|.............<1>......................<2>.......|";
        s[(int)(listenerpos.x / DISTANCEFACTOR) + 25] = 'L';

        Common_Draw("==================================================");
        Common_Draw("3D Example.");
        Common_Draw("Copyright (c) Firelight Technologies 2004-2024.");
        Common_Draw("==================================================");
        Common_Draw("");
        Common_Draw("Press %s to toggle sound 1 (16bit Mono 3D)", Common_BtnStr(BTN_ACTION1));
        Common_Draw("Press %s to toggle sound 2 (8bit Mono 3D)", Common_BtnStr(BTN_ACTION2));
        Common_Draw("Press %s to play a sound (16bit Stereo 2D)", Common_BtnStr(BTN_ACTION3));
        Common_Draw("Press %s or %s to move listener in still mode", Common_BtnStr(BTN_LEFT), Common_BtnStr(BTN_RIGHT));
        Common_Draw("Press %s to toggle listener auto movement", Common_BtnStr(BTN_MORE));
        Common_Draw("Press %s to quit", Common_BtnStr(BTN_QUIT));
        Common_Draw("");
        Common_Draw(s);

        Common_Sleep(INTERFACE_UPDATETIME - 1);
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
