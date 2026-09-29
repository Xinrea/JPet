#include "AudioManager.hpp"

#import <AVFoundation/AVFoundation.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <random>

#include "DataManager.hpp"
#include "LAppDefine.hpp"
#include "LAppPal.hpp"

// Compile with -fobjc-arc. All player access is serialized; each entry point
// that creates Cocoa objects has a pool, including calls from worker threads.
namespace {
std::mutex instance_mutex;
AudioManager* instance = nullptr;
}

struct AudioManager::Impl {
  std::mutex mutex;
  AVAudioPlayer* __strong player = nil;
  vector<wstring> start;
  vector<wstring> idle;
  vector<wstring> click;
  std::mt19937 random{std::random_device{}()};
  bool initialized = false;
  float pan = 0.0f;
  float attenuation = 1.0f / std::max(1.0f, LAppDefine::AudioDepth);
  float volume = 0.0f;

  const vector<wstring>* Files(AudioType type) const {
    switch (type) {
      case AudioType::START: return &start;
      case AudioType::IDLE: return &idle;
      case AudioType::CLICK: return &click;
    }
    return nullptr;
  }

  void ApplySpatial() {
    player.pan = pan;
    // FMOD's gain can exceed one, unlike AVAudioPlayer's. Apply distance
    // attenuation before clamping so the default volume (20 / 10) at z=2
    // retains approximately the original loudness.
    player.volume = std::max(0.0f, std::min(1.0f, volume * attenuation));
  }

  // Called with mutex held and an autorelease pool in place. A single player
  // implements the existing non-overlapping channel behavior without a cache
  // of decoded sounds that would grow for arbitrary caller-supplied paths.
  void Play(const wstring& file) {
    if (!initialized || file.empty() || player.isPlaying) return;
    if (DataManager::GetInstance()->GetConfig<bool>("audio", "mute", false)) return;

    const std::string path = LAppPal::WStringToString(file);
    NSString* filename = [[NSString alloc] initWithBytes:path.data()
                                                length:path.size()
                                              encoding:NSUTF8StringEncoding];
    if (!filename) return;
    NSError* error = nil;
    AVAudioPlayer* next = [[AVAudioPlayer alloc]
        initWithContentsOfURL:[NSURL fileURLWithPath:filename] error:&error];
    if (!next || ![next prepareToPlay]) {
      LAppPal::PrintLog("[AudioManager]Unable to load audio: %s", path.c_str());
      return;
    }
    volume = static_cast<float>(
        DataManager::GetInstance()->GetConfig<int>("audio", "volume", 20)) / 10.0f;
    player = next;
    ApplySpatial();
    if (![player play]) {
      LAppPal::PrintLog("[AudioManager]Unable to play audio: %s", path.c_str());
      player = nil;
    }
  }
};

AudioManager::AudioManager() : impl_(new Impl) {}

AudioManager::~AudioManager() {
  Release();
}

AudioManager* AudioManager::GetInstance() {
  std::lock_guard<std::mutex> lock(instance_mutex);
  if (!instance) instance = new AudioManager;
  return instance;
}

void AudioManager::ReleaseInstance() {
  // As with any raw-pointer singleton, shutdown must join/stop callers before
  // deleting the instance. The mutex serializes creation and destruction, not
  // the lifetime of pointers already handed to callers.
  std::lock_guard<std::mutex> lock(instance_mutex);
  delete instance;
  instance = nullptr;
}

bool AudioManager::Initialize() {
  @autoreleasepool {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->initialized) return true;
    impl_->start.clear();
    impl_->idle.clear();
    impl_->click.clear();
    for (const auto& file : LAppPal::ListFolder(L"resources/audios/")) {
      if (LAppPal::StartWith(file, L"s")) impl_->start.push_back(file);
      else if (LAppPal::StartWith(file, L"i")) impl_->idle.push_back(file);
      else if (LAppPal::StartWith(file, L"r")) impl_->click.push_back(file);
    }
    // No audio device or sound files are required until playback is requested.
    impl_->initialized = true;
    return true;
  }
}

bool AudioManager::IsPlay() {
  @autoreleasepool {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->player.isPlaying;
  }
}

void AudioManager::Play3dSound(AudioType type) {
  @autoreleasepool {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto* files = impl_->Files(type);
    if (!files || files->empty()) return;
    std::uniform_int_distribution<size_t> choose(0, files->size() - 1);
    impl_->Play(L"resources/audios/" + (*files)[choose(impl_->random)]);
  }
}

void AudioManager::Play3dSound(AudioType type, int no) {
  @autoreleasepool {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto* files = impl_->Files(type);
    if (!files || files->empty()) return;
    // Normalize negative indices too, without signed overflow for INT_MIN.
    const auto count = static_cast<std::int64_t>(files->size());
    const auto index = (static_cast<std::int64_t>(no) % count + count) % count;
    impl_->Play(L"resources/audios/" + (*files)[static_cast<size_t>(index)]);
  }
}

void AudioManager::Play3dSound(const wstring& file) {
  @autoreleasepool {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->Play(file);
  }
}

void AudioManager::Update(int x, int y, int w, int h, int mw, int mh) {
  @autoreleasepool {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (mw <= 0 || mh <= 0) return;
    const float px = ((static_cast<float>(x) + w / 2.0f) / (mw / 2.0f) - 1.0f)
        * LAppDefine::AudioSpace;
    const float py = ((static_cast<float>(y) + h / 2.0f) / (mh / 2.0f) - 1.0f)
        * LAppDefine::AudioSpace;
    const float depth = LAppDefine::AudioDepth;
    const float distance = std::sqrt(px * px + py * py + depth * depth);
    impl_->attenuation = 1.0f / std::max(1.0f, distance);
    impl_->pan = distance > 0.0f
        ? std::max(-1.0f, std::min(1.0f, px / distance)) : 0.0f;
    impl_->ApplySpatial();
  }
}

void AudioManager::Release() {
  @autoreleasepool {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    [impl_->player stop];
    impl_->player = nil;
    impl_->start.clear();
    impl_->idle.clear();
    impl_->click.clear();
    impl_->initialized = false;
  }
}
