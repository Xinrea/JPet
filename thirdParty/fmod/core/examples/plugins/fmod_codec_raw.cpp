/*===============================================================================================
Raw Codec Plugin Example
Copyright (c), Firelight Technologies Pty, Ltd 2004-2024.

This example shows how to create a codec that reads raw PCM data.

1. The codec can be compiled as a DLL, using the reserved function name 'FMODGetCodecDescription' 
   as the only export symbol, and at runtime, the dll can be loaded in with System::loadPlugin.

2. Alternatively a codec of this type can be compiled directly into the program that uses it, and 
   you just register the codec into FMOD with System::registerCodec.   This puts the codec into 
   the FMOD system, just the same way System::loadPlugin would if it was an external file.

3. The 'open' callback is the first thing called, and FMOD already has a file handle open for it.
   In the open callback you can use FMOD_CODEC_STATE::fileread / FMOD_CODEC_STATE::fileseek to parse 
   your own file format, and return FMOD_ERR_FORMAT if it is not the format you support.  Return 
   FMOD_OK if it succeeds your format test.

4. When an FMOD user calls System::createSound or System::createStream, the 'open' callback is called
   once after FMOD tries to open it as many other types of file.   If you want to override FMOD's 
   internal codecs then use the 'priority' parameter of System::loadPlugin or System::registerCodec.

5. In the open callback, tell FMOD what sort of PCM format the sound will produce with the 
   FMOD_CODEC_STATE::waveformat member.

6. The 'close' callback is called when Sound::release is called by the FMOD user.

7. The 'read' callback is called when System::createSound or System::createStream wants to receive 
   PCM data, in the format that you specified with FMOD_CODEC_STATE::waveformat.  Data is 
   interleaved as decribed in the terminology section of the FMOD API documentation.
   When a stream is being used, the read callback will be called repeatedly, using a size value 
   determined by the decode buffer size of the stream.  See FMOD_CREATESOUNDEXINFO or 
   FMOD_ADVANCEDSETTINGS.

8. The 'seek' callback is called when Channel::setPosition is called, or when looping a sound 
   when it is a stream.

===============================================================================================*/

#include <stdio.h>

#include "fmod.h"

FMOD_RESULT F_CALLBACK rawopen(FMOD_CODEC_STATE *codec, FMOD_MODE usermode, FMOD_CREATESOUNDEXINFO *userexinfo);
FMOD_RESULT F_CALLBACK rawclose(FMOD_CODEC_STATE *codec);
FMOD_RESULT F_CALLBACK rawread(FMOD_CODEC_STATE *codec, void *buffer, unsigned int size, unsigned int *read);
FMOD_RESULT F_CALLBACK rawsetposition(FMOD_CODEC_STATE *codec, int subsound, unsigned int position, FMOD_TIMEUNIT postype);

FMOD_CODEC_DESCRIPTION rawcodec =
{
    FMOD_CODEC_PLUGIN_VERSION,          // Plugin version.
    "FMOD Raw player plugin example",   // Name.
    0x00010000,                         // Version 0xAAAABBBB   A = major, B = minor.
    0,                                  // Don't force everything using this codec to be a stream
    FMOD_TIMEUNIT_PCMBYTES,             // The time format we would like to accept into setposition/getposition.
    &rawopen,                           // Open callback.
    &rawclose,                          // Close callback.
    &rawread,                           // Read callback.
    0,                                  // Getlength callback.  (If not specified FMOD return the length in FMOD_TIMEUNIT_PCM, FMOD_TIMEUNIT_MS or FMOD_TIMEUNIT_PCMBYTES units based on the lengthpcm member of the FMOD_CODEC structure).
    &rawsetposition,                    // Setposition callback.
    0,                                  // Getposition callback. (only used for timeunit types that are not FMOD_TIMEUNIT_PCM, FMOD_TIMEUNIT_MS and FMOD_TIMEUNIT_PCMBYTES).
    0                                   // Sound create callback (don't need it)
};


/*
    FMODGetCodecDescription is mandatory for every fmod plugin.  This is the symbol the registerplugin function searches for.
    Must be declared with F_CALL to make it export as stdcall.
    MUST BE EXTERN'ED AS C!  C++ functions will be mangled incorrectly and not load in fmod.
*/
#ifdef __cplusplus
extern "C" {
#endif

F_EXPORT FMOD_CODEC_DESCRIPTION * F_CALL FMODGetCodecDescription()
{
    return &rawcodec;
}

#ifdef __cplusplus
}
#endif


static FMOD_CODEC_WAVEFORMAT    rawwaveformat;

/*
    The actual codec code.

    Note that the callbacks uses FMOD's supplied file system callbacks.

    This is important as even though you might want to open the file yourself, you would lose the following benefits.
    1. Automatic support of memory files, CDDA based files, and HTTP/TCPIP based files.
    2. "fileoffset" / "length" support when user calls System::createSound with FMOD_CREATESOUNDEXINFO structure.
    3. Buffered file access.
    FMOD files are high level abstracts that support all sorts of 'file', they are not just disk file handles.
    If you want FMOD to use your own filesystem (and potentially lose the above benefits) use System::setFileSystem.
*/

FMOD_RESULT F_CALLBACK rawopen(FMOD_CODEC_STATE *codec, FMOD_MODE /*usermode*/, FMOD_CREATESOUNDEXINFO * /*userexinfo*/)
{          
    rawwaveformat.channels     = 2;
    rawwaveformat.format       = FMOD_SOUND_FORMAT_PCM16;
    rawwaveformat.frequency    = 44100;
    rawwaveformat.pcmblocksize = 0;
    unsigned int size;
    FMOD_CODEC_FILE_SIZE(codec, &size);
    rawwaveformat.lengthpcm    = size / (rawwaveformat.channels * sizeof(short));   /* bytes converted to PCM samples */;

    codec->numsubsounds      = 0;                    /* number of 'subsounds' in this sound.  For most codecs this is 0, only multi sound codecs such as FSB or CDDA have subsounds. */
    codec->waveformat        = &rawwaveformat;
    codec->plugindata        = 0;                    /* user data value */
        
    /* If your file format needs to read data to determine the format and load metadata, do so here with codec->fileread/fileseek function pointers.  This will handle reading from disk/memory or internet. */
    
    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK rawclose(FMOD_CODEC_STATE * /*codec*/)
{
    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK rawread(FMOD_CODEC_STATE *codec, void *buffer, unsigned int size, unsigned int *read)
{
    return FMOD_CODEC_FILE_READ(codec, buffer, size, read);
}

FMOD_RESULT F_CALLBACK rawsetposition(FMOD_CODEC_STATE *codec, int /*subsound*/, unsigned int position, FMOD_TIMEUNIT /*postype*/)
{
    return FMOD_CODEC_FILE_SEEK(codec, position, 0);
}

