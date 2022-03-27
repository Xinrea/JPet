/*==============================================================================
FMOD Example Framework
Copyright (c), Firelight Technologies Pty, Ltd 2012-2024.
==============================================================================*/
#ifndef FMOD_EXAMPLES_COMMON_H
#define FMOD_EXAMPLES_COMMON_H

#include "common_platform.h"
#include "fmod.h"

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>

#define NUM_COLUMNS 50
#define NUM_ROWS 25

#ifndef Common_Sin
    #define Common_Sin sin
#endif

#ifndef Common_snprintf
    #define Common_snprintf snprintf
#endif

#ifndef Common_vsnprintf
    #define Common_vsnprintf vsnprintf
#endif

enum Common_Button
{
    BTN_ACTION1,
    BTN_ACTION2,
    BTN_ACTION3,
    BTN_ACTION4,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_UP,
    BTN_DOWN,
    BTN_MORE,
    BTN_QUIT
};

typedef struct
{
    void *crit;
} Common_Mutex;

/* Cross platform functions (common) */
void Common_Format(char *buffer, int bufferSize, const char *formatString...);
void Common_Fatal(const char *format, ...);
void Common_Draw(const char *format, ...);
void Common_Time_GetUs(unsigned int *us);
void Common_Log(const char *format, ...);
void Common_LoadFileMemory(const char *name, void **buff, int *length);
void Common_UnloadFileMemory(void *buff);
void Common_Sleep(unsigned int ms);
void Common_File_Open(const char *name, int mode, unsigned int *filesize, void **handle);   // mode : 0 = read, 1 = write.
void Common_File_Close(void *handle);
void Common_File_Read(void *handle, void *buf, unsigned int length, unsigned int *read);
void Common_File_Write(void *handle, void *buf, unsigned int length);
void Common_File_Seek(void *handle, unsigned int offset);
void Common_Mutex_Create(Common_Mutex *mutex);
void Common_Mutex_Destroy(Common_Mutex *mutex);
void Common_Mutex_Enter(Common_Mutex *mutex);
void Common_Mutex_Leave(Common_Mutex *mutex);
void Common_Thread_Create(void (*callback)(void *param), void *param, void **handle);
void Common_Thread_Destroy(void *handle);

void ERRCHECK_fn(FMOD_RESULT result, const char *file, int line);
#define ERRCHECK(_result) ERRCHECK_fn(_result, __FILE__, __LINE__)
#define Common_Max(_a, _b) ((_a) > (_b) ? (_a) : (_b))
#define Common_Min(_a, _b) ((_a) < (_b) ? (_a) : (_b))
#define Common_Clamp(_min, _val, _max) ((_val) < (_min) ? (_min) : ((_val) > (_max) ? (_max) : (_val)))

/* Functions with platform specific implementation (common_platform) */
void Common_Init(void **extraDriverData);
void Common_Close();
void Common_Update();
void Common_Exit(int returnCode);
void Common_DrawText(const char *text);
bool Common_BtnPress(Common_Button btn);
bool Common_BtnDown(Common_Button btn);
const char *Common_BtnStr(Common_Button btn);
const char *Common_MediaPath(const char *fileName);
const char *Common_WritePath(const char *fileName);


#endif
