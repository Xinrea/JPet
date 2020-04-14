#include "AudioManager.hpp"
#include "LAppPal.hpp"
#include "LAppDefine.hpp"
using namespace LAppDefine;

namespace {
    AudioManager* s_instance = NULL;
}

AudioManager* AudioManager::GetInstance() {
    if (s_instance == NULL) {
        s_instance = new AudioManager();
    }
    return s_instance;
}

void AudioManager::ReleaseInstance() {
    if (s_instance != NULL) {
        delete s_instance;
    }
    s_instance = NULL;
}

bool AudioManager::Initialize() {
    if (DebugLogEnable) {
        LAppPal::PrintLog("[AudioManager]Initialize");
    }
    FMOD::System_Create(&_system);
    _system->init(256,FMOD_INIT_NORMAL,0);
    return true;
}

bool AudioManager::IsPlay() {
    if (_channel) {
        bool isPlay;
        return _channel->isPlaying(&isPlay);
    }
    return false;
}

void AudioManager::Play3dSound(string soundfile) {
    bool isPlay;
    if (DebugLogEnable) {
        LAppPal::PrintLog("[AudioManager]Play Sound: %s",soundfile.c_str());
    }
    if (_mute) return;
    FMOD::Sound* sound;
    auto iter = sounds.find(soundfile);
    if (iter == sounds.end())
    {
        _system->createSound(soundfile.c_str(), FMOD_3D_HEADRELATIVE, 0, &sound);
    }
    else
    {
        sound = iter->second;
    }
    
    if (_channel) {
        _channel->isPlaying(&isPlay);
        if (!isPlay) {
            _system->playSound(sound, 0, true, &_channel);
            _channel->setVolume(_volume);
            _channel->setPaused(false);
        }
    }
    else {
        isPlay = false;
        _system->playSound(sound, 0, true, &_channel);
        _channel->setVolume(_volume);
        _channel->setPaused(false);
    }
}

void AudioManager::Update(int x, int y, int w, int h, int mw, int mh) {
    FMOD_VECTOR pos = { 0.0f, 0.0f, AudioDepth };
    FMOD_VECTOR vel = { 0.0f, 0.0f, 0.0f };
    pos.x = (static_cast<float>(x)+w/2) / (mw / 2) * AudioSpace - AudioSpace;
    pos.y = (static_cast<float>(y)+h/2) / (mh / 2) * AudioSpace - AudioSpace;
    if (_channel)_channel->set3DAttributes(&pos,&vel);
    _system->update();
}

void AudioManager::Release() {
    if (_system) _system->release();
}

AudioManager::AudioManager():
    _system(NULL),
    _channel(NULL)
{
    
}

AudioManager::~AudioManager(){

}