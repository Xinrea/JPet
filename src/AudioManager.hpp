#include "fmod.hpp"
#include <string>
#include <map>
#pragma once
using std::map;
using std::string;

class AudioManager
{
public:
    AudioManager();
    ~AudioManager();
    static AudioManager *GetInstance();
    static void ReleaseInstance();
    bool Initialize();
    bool IsPlay();
    void Play3dSound(string soundname);
    void SetVolume(float v) { _volume = v; }
    float GetVolume() { return _volume; }
    void SetMute(bool m) { _mute = m; }
    bool GetMute() { return _mute; }
    void Update(int x, int y, int w, int h, int mw, int mh);
    void Release();

private:
    FMOD::System *_system;
    FMOD::Channel *_channel;
    map<string, FMOD::Sound*> sounds;
    float _volume = 5.0f;
    bool _mute = false;
};