/*==============================================================================
FMOD Example Framework
Copyright (c), Firelight Technologies Pty, Ltd 2012-2024.
==============================================================================*/
#include "common.h"
#include "fmod_errors.h"

/* Cross platform OS Functions internal to the FMOD library, exposed for the example framework. */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct FMOD_OS_FILE            FMOD_OS_FILE;
typedef struct FMOD_OS_CRITICALSECTION FMOD_OS_CRITICALSECTION;

FMOD_RESULT F_API FMOD_OS_Time_GetUs(unsigned int *us);
FMOD_RESULT F_API FMOD_OS_Debug_Output(const char *format, ...);
FMOD_RESULT F_API FMOD_OS_File_Open(const char *name, int mode, unsigned int *filesize, FMOD_OS_FILE **handle);
FMOD_RESULT F_API FMOD_OS_File_Close(FMOD_OS_FILE *handle);
FMOD_RESULT F_API FMOD_OS_File_Read(FMOD_OS_FILE *handle, void *buf, unsigned int count, unsigned int *read);
FMOD_RESULT F_API FMOD_OS_File_Write(FMOD_OS_FILE *handle, const void *buffer, unsigned int bytesToWrite, bool flush);
FMOD_RESULT F_API FMOD_OS_File_Seek(FMOD_OS_FILE *handle, unsigned int offset);
FMOD_RESULT F_API FMOD_OS_Time_Sleep(unsigned int ms);
FMOD_RESULT F_API FMOD_OS_CriticalSection_Create(FMOD_OS_CRITICALSECTION **crit, bool memorycrit);
FMOD_RESULT F_API FMOD_OS_CriticalSection_Free(FMOD_OS_CRITICALSECTION *crit, bool memorycrit);
FMOD_RESULT F_API FMOD_OS_CriticalSection_Enter(FMOD_OS_CRITICALSECTION *crit);
FMOD_RESULT F_API FMOD_OS_CriticalSection_Leave(FMOD_OS_CRITICALSECTION *crit);
FMOD_RESULT F_API FMOD_OS_CriticalSection_TryEnter(FMOD_OS_CRITICALSECTION *crit, bool *entered);
FMOD_RESULT F_API FMOD_OS_CriticalSection_IsLocked(FMOD_OS_CRITICALSECTION *crit, bool *locked);
FMOD_RESULT F_API FMOD_OS_Thread_Create(const char *name, void (*callback)(void *param), void *param, FMOD_THREAD_AFFINITY affinity, FMOD_THREAD_PRIORITY priority, FMOD_THREAD_STACK_SIZE stacksize, void **handle);
FMOD_RESULT F_API FMOD_OS_Thread_Destroy(void *handle);

#ifdef __cplusplus
}
#endif

void (*Common_Private_Error)(FMOD_RESULT, const char *, int);

void ERRCHECK_fn(FMOD_RESULT result, const char *file, int line)
{
    if (result != FMOD_OK)
    {
        if (Common_Private_Error)
        {
            Common_Private_Error(result, file, line);
        }
        Common_Fatal("%s(%d): FMOD error %d - %s", file, line, result, FMOD_ErrorString(result));
    }
}

void Common_Format(char *buffer, int bufferSize, const char *formatString...)
{
    va_list args;
    va_start(args, formatString);
    Common_vsnprintf(buffer, bufferSize, formatString, args);
    va_end(args);
    buffer[bufferSize-1] = '\0';
}

void Common_Fatal(const char *format, ...)
{
    char error[1024];

    va_list args;
    va_start(args, format);
    Common_vsnprintf(error, 1024, format, args);
    va_end(args);
    error[1023] = '\0';

    do
    {
        Common_Draw("A fatal error has occurred...");
        Common_Draw("");
        Common_Draw("%s", error);
        Common_Draw("");
        Common_Draw("Press %s to quit", Common_BtnStr(BTN_QUIT));

        Common_Update();
        Common_Sleep(50);
    } while (!Common_BtnPress(BTN_QUIT));

    Common_Exit(0);
}

void Common_Draw(const char *format, ...)
{
    char string[1024];
    char *stringPtr = string;

    va_list args;
    va_start(args, format);
    Common_vsnprintf(string, 1024, format, args);
    va_end(args);
    string[1023] = '\0';

    unsigned int length = (unsigned int)strlen(string);

    do
    {
        bool consumeNewLine = false;
        unsigned int copyLength = length;

        // Search for new line characters
        char *newLinePtr = strchr(stringPtr, '\n');
        if (newLinePtr)
        {
            consumeNewLine = true;
            copyLength = (unsigned int)(newLinePtr - stringPtr);
        }

        if (copyLength > NUM_COLUMNS)
        {
            // Hard wrap by default
            copyLength = NUM_COLUMNS;

            // Loop for a soft wrap
            for (int i = NUM_COLUMNS - 1; i >= 0; i--)
            {
                if (stringPtr[i] == ' ')
                {
                    copyLength = i + 1;
                    break;
                }
            }
        }

        // Null terminate the sub string temporarily by swapping out a char
        char tempChar = stringPtr[copyLength];
        stringPtr[copyLength] = 0;
        Common_DrawText(stringPtr);
        stringPtr[copyLength] = tempChar;

        copyLength += (consumeNewLine ? 1 : 0);
        length -= copyLength;
        stringPtr += copyLength;
    } while (length > 0);
}

void Common_Time_GetUs(unsigned int *us)
{
    FMOD_OS_Time_GetUs(us);
}

void Common_Log(const char *format, ...)
{
    char string[1024];

    va_list args;
    va_start(args, format);
    Common_vsnprintf(string, 1024, format, args);
    va_end(args);
    string[1023] = '\0';

    FMOD_OS_Debug_Output(string);
}

void Common_LoadFileMemory(const char *name, void **buff, int *length)
{
    FMOD_OS_FILE *file = NULL;
    unsigned int len, bytesread;

    FMOD_OS_File_Open(name, 0, &len, &file);
    void *mem = malloc(len);
    FMOD_OS_File_Read(file, mem, len, &bytesread);
    FMOD_OS_File_Close(file);

    *buff = mem;
    *length = bytesread;
}

void Common_UnloadFileMemory(void *buff)
{
    free(buff);
}

void Common_Sleep(unsigned int ms)
{
    FMOD_OS_Time_Sleep(ms);
}

void Common_File_Open(const char *name, int mode, unsigned int *filesize, void **handle)
{
    FMOD_OS_File_Open(name, mode, filesize, (FMOD_OS_FILE **)handle);
}

void Common_File_Close(void *handle)
{
    FMOD_OS_File_Close((FMOD_OS_FILE *)handle);
}

void Common_File_Read(void *handle, void *buf, unsigned int length, unsigned int *read)
{
    FMOD_OS_File_Read((FMOD_OS_FILE *)handle, buf, length, read);
}

void Common_File_Write(void *handle, void *buf, unsigned int length)
{
    FMOD_OS_File_Write((FMOD_OS_FILE *)handle, buf, length, true);
}

void Common_File_Seek(void *handle, unsigned int offset)
{
    FMOD_OS_File_Seek((FMOD_OS_FILE *)handle, offset);
}

void Common_Mutex_Create(Common_Mutex *mutex)
{
    FMOD_OS_CriticalSection_Create((FMOD_OS_CRITICALSECTION **)&mutex->crit, false);
}

void Common_Mutex_Destroy(Common_Mutex *mutex)
{
    FMOD_OS_CriticalSection_Free((FMOD_OS_CRITICALSECTION *)mutex->crit, false);
}

void Common_Mutex_Enter(Common_Mutex *mutex)
{
    FMOD_OS_CriticalSection_Enter((FMOD_OS_CRITICALSECTION *)mutex->crit);
}

void Common_Mutex_Leave(Common_Mutex *mutex)
{
    FMOD_OS_CriticalSection_Leave((FMOD_OS_CRITICALSECTION *)mutex->crit);
}

void Common_Thread_Create(void (*callback)(void *param), void *param, void **handle)
{
    FMOD_OS_Thread_Create("FMOD Example Thread", callback, param, FMOD_THREAD_AFFINITY_GROUP_A, FMOD_THREAD_PRIORITY_MEDIUM, (16 * 1024), handle);
}

void Common_Thread_Destroy(void *handle)
{
    FMOD_OS_Thread_Destroy(handle);
}
