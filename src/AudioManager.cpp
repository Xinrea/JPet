#include "AudioManager.hpp"

#include "LAppDefine.hpp"
#include "LAppPal.hpp"
using namespace LAppDefine;

namespace {
AudioManager *s_instance = nullptr;
}

AudioManager *AudioManager::GetInstance() {
  if (s_instance == nullptr) {
    s_instance = new AudioManager();
  }
  return s_instance;
}

void AudioManager::ReleaseInstance() {
  delete s_instance;
  s_instance = nullptr;
}

bool AudioManager::Initialize() {
  if (DebugLogEnable) {
    LAppPal::PrintLog("[AudioManager]Initialize");
  }
  FMOD::System_Create(&_system);
  _system->init(256, FMOD_INIT_NORMAL, 0);
  // load filenames of all audio file
  auto audio_file_list = LAppPal::ListFolder(L"resources/audios/");
  for (const std::wstring& f : audio_file_list) {
    if (LAppPal::StartWith(f, L"s")) {
      start_audios_.push_back(f);
      continue;
    }
    if (LAppPal::StartWith(f, L"i")) {
      idle_audios_.push_back(f);
      continue;
    }
    if (LAppPal::StartWith(f, L"r")) {
      click_audios_.push_back(f);
      continue;
    }
  }
  return true;
}

bool AudioManager::IsPlay() {
  if (_channel) {
    bool isPlay;
    return _channel->isPlaying(&isPlay);
  }
  return false;
}


void AudioManager::Play3dSound(AudioType t) {
  Play3dSound(t, rand() % 100);
}

void AudioManager::Play3dSound(AudioType t, int no) {
  LAppPal::PrintLog(LogLevel::Debug, "[AudioManager]Play Sound: %d %d", t, no);
  if (_mute) return;
  wstring target_audio_file;
  switch (t) {
    case AudioType::START: {
      if (start_audios_.empty()) {
        return;
      }
      no = no % start_audios_.size();
      target_audio_file = start_audios_[no];
      break;
    }
    case AudioType::IDLE: {
      if (idle_audios_.empty()) {
        return;
      }      
      no = no % idle_audios_.size();
      target_audio_file = idle_audios_[no];
      break;
    }
    case AudioType::CLICK: {
      if (click_audios_.empty()) {
        return;
      }      
      no = no % click_audios_.size();
      target_audio_file = click_audios_[no];
      break;
    }
  }
  Play3dSound(L"resources/audios/" + target_audio_file);
}

void AudioManager::Play3dSound(const wstring& target_audio_file) {
  LAppPal::PrintLog(LogLevel::Debug, L"[AudioManager]Play Sound: %ls", target_audio_file.c_str());
  bool isPlay;
  FMOD::Sound *sound;
  // find in cache
  auto iter = sounds.find(target_audio_file);
  if (iter == sounds.end()) {
    _system->createSound(LAppPal::WStringToString(target_audio_file).c_str(), FMOD_3D_HEADRELATIVE, 0, &sound);
  } else {
    sound = iter->second;
  }

  if (_channel) {
    _channel->isPlaying(&isPlay);
    if (!isPlay) {
      _system->playSound(sound, 0, true, &_channel);
      _channel->setVolume(_volume);
      _channel->setPaused(false);
    }
  } else {
    isPlay = false;
    _system->playSound(sound, 0, true, &_channel);
    _channel->setVolume(_volume);
    _channel->setPaused(false);
  }
}

void AudioManager::Update(int x, int y, int w, int h, int mw, int mh) {
  FMOD_VECTOR pos = {0.0f, 0.0f, AudioDepth};
  FMOD_VECTOR vel = {0.0f, 0.0f, 0.0f};
  pos.x = (static_cast<float>(x) + float(w) / 2) / (float(mw) / 2) * AudioSpace - AudioSpace;
  pos.y = (static_cast<float>(y) + float(h) / 2) / (float(mh) / 2) * AudioSpace - AudioSpace;
  if (_channel) _channel->set3DAttributes(&pos, &vel);
  _system->update();
}

void AudioManager::Release() {
  if (_system) _system->release();
}

AudioManager::AudioManager() : _system(nullptr), _channel(nullptr) {}

AudioManager::~AudioManager() = default;
