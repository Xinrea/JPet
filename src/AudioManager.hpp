#include <map>
#include <vector>
#include <string>

#include "fmod.hpp"
#pragma once
using std::map;
using std::vector;
using std::wstring;

enum class AudioType {
 START, IDLE, CLICK
};

class AudioManager {
 public:
  AudioManager();
  ~AudioManager();
  static AudioManager *GetInstance();
  static void ReleaseInstance();
  bool Initialize();
  bool IsPlay();
  void Play3dSound(AudioType t, int no);
  void Play3dSound(AudioType t);
  void Play3dSound(const wstring& file);
  void Update(int x, int y, int w, int h, int mw, int mh);
  void Release();

 private:
  FMOD::System *_system;
  FMOD::Channel *_channel;
  map<wstring, FMOD::Sound *> sounds;
  vector<wstring> start_audios_;
  vector<wstring> idle_audios_;
  vector<wstring> click_audios_;
};
