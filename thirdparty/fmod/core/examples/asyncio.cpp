/*===============================================================================================
 Async IO Example
 Copyright (c), Firelight Technologies Pty, Ltd 2004-2024.

 This example shows how to play a stream and use a custom file handler that defers reads for the
 streaming part.  FMOD will allow the user to return straight away from a file read request and
 supply the data at a later time.
===============================================================================================*/
#include "fmod.hpp"
#include "common.h"
#include <list>

struct AsyncData
{
    FMOD_ASYNCREADINFO *info;
};

struct ScopedMutex
{
    Common_Mutex *mMutex;
    ScopedMutex(Common_Mutex *mutex) : mMutex(mutex) { Common_Mutex_Enter(mMutex); }
    ~ScopedMutex() { Common_Mutex_Leave(mMutex); }
};

Common_Mutex gListCrit;
std::list<AsyncData*> gList;
bool gThreadQuit = false;
bool gThreadFinished = false;
bool gSleepBreak = false;


/*
    A little text buffer to allow a scrolling window
*/
const int DRAW_ROWS = NUM_ROWS - 8;
const int DRAW_COLS = NUM_COLUMNS;

char gLineData[DRAW_ROWS][DRAW_COLS];
Common_Mutex gLineCrit;

void AddLine(const char *formatString...)
{
    ScopedMutex mutex(&gLineCrit);
    
    char s[DRAW_COLS];
    va_list args;
    va_start(args, formatString);
    Common_vsnprintf(s, DRAW_COLS, formatString, args);
    va_end(args);

    for (int i = 1; i < DRAW_ROWS; i++)
    {
        memcpy(gLineData[i-1], gLineData[i], DRAW_COLS);
    }
    strncpy(gLineData[DRAW_ROWS-1], s, DRAW_COLS);
}

void DrawLines()
{
    ScopedMutex mutex(&gLineCrit);

    for (int i = 0; i < DRAW_ROWS; i++)
    {
        Common_Draw(gLineData[i]);
    }
}


/*
    File callbacks
*/
FMOD_RESULT F_CALLBACK myopen(const char *name, unsigned int *filesize, void **handle, void * /*userdata*/)
{
    assert(name);
    assert(filesize);
    assert(handle);

    Common_File_Open(name, 0, filesize, handle);    // mode 0 = 'read'.
    
    if (!handle)
    {
        return FMOD_ERR_FILE_NOTFOUND;
    }

    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK myclose(void *handle, void * /*userdata*/)
{
    assert(handle);

    Common_File_Close(handle);

    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK myread(void *handle, void *buffer, unsigned int sizebytes, unsigned int *bytesread, void * /*userdata*/)
{
    assert(handle);
    assert(buffer);
    assert(bytesread);

    Common_File_Read(handle, buffer, sizebytes, bytesread);

    if (*bytesread < sizebytes)
    {
        return FMOD_ERR_FILE_EOF;
    }

    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK myseek(void *handle, unsigned int pos, void * /*userdata*/)
{
    assert(handle);

    Common_File_Seek(handle, pos);

    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK myasyncread(FMOD_ASYNCREADINFO *info, void * /*userdata*/)
{
    assert(info);

    ScopedMutex mutex(&gListCrit);

    AsyncData *data = (AsyncData *)malloc(sizeof(AsyncData));
    if (!data)
    {
        /* Signal FMOD to wake up, this operation has has failed */
        info->done(info, FMOD_ERR_MEMORY);
        return FMOD_ERR_MEMORY;
    }

    AddLine("REQUEST %5d bytes, offset %5d PRIORITY = %d.", info->sizebytes, info->offset, info->priority);
    data->info = info;
    gList.push_back(data);
    
    /* Example only: Use your native filesystem scheduler / priority here */
    if (info->priority > 50)
    {
        gSleepBreak = true;
    }

    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK myasynccancel(FMOD_ASYNCREADINFO *info, void * /*userdata*/)
{
    assert(info);

    ScopedMutex mutex(&gListCrit);

    /* Find the pending IO request and remove it */
    for (std::list<AsyncData*>::iterator itr = gList.begin(); itr != gList.end(); itr++)
    {
        AsyncData *data = *itr;
        if (data->info == info)
        {
            gList.remove(data);
            free(data);

            /* Signal FMOD to wake up, this operation has been cancelled */
            info->done(info, FMOD_ERR_FILE_DISKEJECTED);
            return FMOD_ERR_FILE_DISKEJECTED;
        }
    }

    /* IO request not found, it must have completed already */
    return FMOD_OK;
}


/*
    Async file IO processing thread
*/
void ProcessQueue(void * /*param*/)
{
    while (!gThreadQuit)
    {
        /* Grab the next IO task off the list */
        FMOD_ASYNCREADINFO *info = NULL;
        Common_Mutex_Enter(&gListCrit);
        if (!gList.empty())
        {
            info = gList.front()->info;
            gList.pop_front();
        }
        Common_Mutex_Leave(&gListCrit);

        if (info)
        {
            /* Example only: Let's deprive the read of the whole block, only give 16kb at a time to make it re-ask for more later */
            unsigned int toread = info->sizebytes;
            if (toread > 16384)
            {
                toread = 16384;
            }

            /* Example only: Demonstration of priority influencing turnaround time */
            for (int i = 0; i < 50; i++)
            {
                Common_Sleep(10);
                if (gSleepBreak)
                {
                    AddLine("URGENT REQUEST - reading now!");
                    gSleepBreak = false;
                    break;
                }
            }

            /* Process the seek and read request with EOF handling */
            Common_File_Seek(info->handle, info->offset);

            Common_File_Read(info->handle, info->buffer, toread, &info->bytesread);
            
            if (info->bytesread < toread)
            {
                AddLine("FED     %5d bytes, offset %5d (* EOF)", info->bytesread, info->offset);
                info->done(info, FMOD_ERR_FILE_EOF);
            }
            else
            {
                AddLine("FED     %5d bytes, offset %5d", info->bytesread, info->offset);
                info->done(info, FMOD_OK);
            }
        }
        else
        {
            Common_Sleep(10); /* Example only: Use your native filesystem synchronisation to wait for more requests */
        }
    }

    gThreadFinished = true;
}


int FMOD_Main()
{
    void *extradriverdata = NULL;
    void *threadhandle = NULL;

    Common_Init(&extradriverdata);

    Common_Mutex_Create(&gLineCrit);
    Common_Mutex_Create(&gListCrit);

    Common_Thread_Create(ProcessQueue, NULL, &threadhandle);

    /*
        Create a System object and initialize.
    */
    FMOD::System *system = NULL;
    FMOD_RESULT result = FMOD::System_Create(&system);
    ERRCHECK(result);

    result = system->init(1, FMOD_INIT_NORMAL, extradriverdata);
    ERRCHECK(result);

    result = system->setStreamBufferSize(32768, FMOD_TIMEUNIT_RAWBYTES);
    ERRCHECK(result);
    
    result = system->setFileSystem(myopen, myclose, myread, myseek, myasyncread, myasynccancel, 2048);
    ERRCHECK(result);

    FMOD::Sound *sound = NULL;
    result = system->createStream(Common_MediaPath("wave.mp3"), FMOD_LOOP_NORMAL | FMOD_2D | FMOD_IGNORETAGS, NULL, &sound);
    ERRCHECK(result);

    FMOD::Channel *channel = NULL;
    result = system->playSound(sound, 0, false, &channel);
    ERRCHECK(result);

    /*
        Main loop.
    */
    do
    {
        Common_Update();

        if (sound)
        {
            bool starving = false;
            FMOD_OPENSTATE openstate = FMOD_OPENSTATE_READY;
            result = sound->getOpenState(&openstate, NULL, &starving, NULL);
            ERRCHECK(result);
            
            if (starving)
            {
                AddLine("Starving");
            }

            result = channel->setMute(starving);
            ERRCHECK(result);
        }

        if (Common_BtnPress(BTN_ACTION1))
        {
            result = sound->release();
            if (result == FMOD_OK)
            {
                sound = NULL;
                AddLine("Released sound");
            }
        }

        result = system->update();
        ERRCHECK(result);

        Common_Draw("==================================================");
        Common_Draw("Async IO Example.");
        Common_Draw("Copyright (c) Firelight Technologies 2004-2024.");
        Common_Draw("==================================================");
        Common_Draw("");
        Common_Draw("Press %s to release playing stream", Common_BtnStr(BTN_ACTION1));
        Common_Draw("Press %s to quit", Common_BtnStr(BTN_QUIT));
        Common_Draw("");
        DrawLines();
        Common_Sleep(50);
    } while (!Common_BtnPress(BTN_QUIT));

    /*
        Shut down
    */
    if (sound)
    {
        result = sound->release();
        ERRCHECK(result);
    }
    result = system->close();
    ERRCHECK(result);
    result = system->release();
    ERRCHECK(result);

    gThreadQuit = true;
    while (!gThreadFinished)
    {
        Common_Sleep(10);
    }

    Common_Mutex_Destroy(&gListCrit);
    Common_Mutex_Destroy(&gLineCrit);
    Common_Thread_Destroy(threadhandle);
    Common_Close();

    return 0;
}
