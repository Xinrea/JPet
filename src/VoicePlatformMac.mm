#include "VoicePlatform.hpp"
#include "VoiceEventQueue.hpp"

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <Security/Security.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Voice {
namespace {
NSString* Text(const std::string& value) {
  return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
}

struct MacState : std::enable_shared_from_this<MacState> {
  std::shared_ptr<EventQueue> queue = std::make_shared<EventQueue>();
  NSURLSession* __strong network = nil;
  NSURLSessionWebSocketTask* __strong socket = nil;
  AVAudioEngine* __strong inputEngine = nil;
  AVAudioEngine* __strong outputEngine = nil;
  AVAudioPlayerNode* __strong player = nil;
  NSPanel* __strong indicator = nil;
  NSTextField* __strong label = nil;
  uint64_t captureRequest = 0;
  bool captureWanted = false;
  bool tapInstalled = false;
  std::mutex playbackMutex;
  uint64_t playbackGeneration = 0;
  uint64_t scheduledFrames = 0;
  uint64_t completedFrames = 0;

  void Receive(NSURLSessionWebSocketTask* task, uint64_t generation) {
    std::weak_ptr<MacState> weak = shared_from_this();
    [task receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage* message, NSError* error) {
      @autoreleasepool {
        const auto state = weak.lock();
        if (!state || state->queue->connection != generation) return;
        if (error) {
          NSString* description = error.localizedDescription.lowercaseString;
          const bool auth = [description containsString:@"401"] || [description containsString:@"403"];
          state->queue->Network(generation, Event::Type::Error, auth
              ? "千问认证失败，请检查北京地域的 API Key、业务空间 ID 和模型权限"
              : "千问语音连接已断开，请检查网络和业务空间设置后重试");
          return;
        }
        if (message.type == NSURLSessionWebSocketMessageTypeString && message.string) {
          const char* utf8 = message.string.UTF8String;
          if (utf8) state->queue->Network(generation, Event::Type::Message, utf8);
        }
        state->Receive(task, generation);
      }
    }];
  }

  void StartInputEngine() {
    if (!captureWanted || tapInstalled) return;
    @try {
      inputEngine = [[AVAudioEngine alloc] init];
      AVAudioInputNode* input = inputEngine.inputNode;
      AVAudioFormat* hardware = [input outputFormatForBus:0];
      if (hardware.sampleRate <= 0 || hardware.channelCount == 0) {
        queue->Push(Event::Type::Error, "没有可用的麦克风，请检查系统声音设置");
        return;
      }
      AVAudioFormat* target = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatInt16
          sampleRate:16000 channels:1 interleaved:YES];
      AVAudioConverter* converter = [[AVAudioConverter alloc] initFromFormat:hardware toFormat:target];
      if (!converter) {
        queue->Push(Event::Type::Error, "无法转换麦克风音频，请更换输入设备后重试");
        return;
      }
      const auto events = queue;
      const auto generation = ++events->capture;
      queue->capturing = true;
      [input installTapOnBus:0 bufferSize:1024 format:hardware block:^(AVAudioPCMBuffer* buffer, AVAudioTime*) {
        @autoreleasepool {
          if (!events->capturing || events->capture != generation) return;
          const AVAudioFrameCount capacity = static_cast<AVAudioFrameCount>(
              std::ceil(buffer.frameLength * 16000.0 / hardware.sampleRate)) + 32;
          AVAudioPCMBuffer* output = [[AVAudioPCMBuffer alloc] initWithPCMFormat:target frameCapacity:capacity];
          __block BOOL supplied = NO;
          NSError* error = nil;
          const auto result = [converter convertToBuffer:output error:&error
              withInputFromBlock:^AVAudioBuffer*(AVAudioPacketCount, AVAudioConverterInputStatus* status) {
                if (supplied) { *status = AVAudioConverterInputStatus_NoDataNow; return nil; }
                supplied = YES;
                *status = AVAudioConverterInputStatus_HaveData;
                return buffer;
              }];
          if (result == AVAudioConverterOutputStatus_Error) {
            events->Captured(generation, Event::Type::Error, "麦克风音频转换失败，请重新说话");
          } else if (output.frameLength) {
            events->Captured(generation, Event::Type::Microphone,
                std::string(reinterpret_cast<const char*>(output.int16ChannelData[0]), output.frameLength * 2));
          }
        }
      }];
      tapInstalled = true;
      NSError* error = nil;
      if (![inputEngine startAndReturnError:&error]) {
        StopInputEngine();
        queue->Push(Event::Type::Error, "无法启动麦克风，请检查输入设备和麦克风权限");
      }
    } @catch (NSException*) {
      StopInputEngine();
      queue->Push(Event::Type::Error, "无法启动麦克风，请检查输入设备和麦克风权限");
    }
  }

  void StopInputEngine() {
    ++queue->capture;
    queue->capturing = false;
    [inputEngine stop];
    if (tapInstalled) [inputEngine.inputNode removeTapOnBus:0];
    tapInstalled = false;
    inputEngine = nil;
  }
};

class MacPlatform final : public Platform {
 public:
  ~MacPlatform() override {
    StopCapture();
    Disconnect();
    StopPlayback();
    [state_->outputEngine stop];
    [state_->indicator orderOut:nil];
    [state_->indicator close];
  }

  bool ShortcutHeld() const override {
    return ([NSEvent modifierFlags] & NSEventModifierFlagOption) != 0;
  }

  void Connect(const std::string& url, const std::string& key) override {
    Disconnect();
    const auto generation = ++state_->queue->connection;
    NSURLSessionConfiguration* config = NSURLSessionConfiguration.ephemeralSessionConfiguration;
    config.timeoutIntervalForRequest = 15;
    state_->network = [NSURLSession sessionWithConfiguration:config];
    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:Text(url)]];
    [request setValue:Text("Bearer " + key) forHTTPHeaderField:@"Authorization"];
    state_->socket = [state_->network webSocketTaskWithRequest:request];
    state_->socket.maximumMessageSize = 4 * 1024 * 1024;
    [state_->socket resume];
    state_->Receive(state_->socket, generation);
  }

  void Disconnect() override {
    ++state_->queue->connection;
    [state_->socket cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil];
    [state_->network invalidateAndCancel];
    state_->socket = nil;
    state_->network = nil;
    state_->queue->Clear();
  }

  void Send(const std::string& message) override {
    const auto generation = state_->queue->connection.load();
    const auto events = state_->queue;
    NSURLSessionWebSocketMessage* payload = [[NSURLSessionWebSocketMessage alloc] initWithString:Text(message)];
    [state_->socket sendMessage:payload completionHandler:^(NSError* error) {
      if (error) events->Network(generation, Event::Type::Error, "千问语音发送失败，请检查网络后重试");
    }];
  }

  void StartCapture() override {
    state_->captureWanted = true;
    const auto request = ++state_->captureRequest;
    const auto permission = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    if (permission == AVAuthorizationStatusAuthorized) state_->StartInputEngine();
    else if (permission == AVAuthorizationStatusNotDetermined) {
      const std::weak_ptr<MacState> weak = state_;
      [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
        dispatch_async(dispatch_get_main_queue(), ^{
          const auto state = weak.lock();
          if (!state || !state->captureWanted || state->captureRequest != request) return;
          if (granted) state->StartInputEngine();
          else state->queue->Push(Event::Type::Error, "请在系统设置 → 隐私与安全性 → 麦克风中允许 JPet");
        });
      }];
    } else state_->queue->Push(Event::Type::Error, "请在系统设置 → 隐私与安全性 → 麦克风中允许 JPet");
  }

  void StopCapture() override {
    state_->captureWanted = false;
    ++state_->captureRequest;
    state_->StopInputEngine();
  }

  std::vector<Event> Poll() override { return state_->queue->Poll(); }

  void Play(const std::string& pcm, float volume) override {
    @try {
      AVAudioFormat* format = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32
          sampleRate:24000 channels:1 interleaved:NO];
      if (!state_->outputEngine) {
        state_->outputEngine = [[AVAudioEngine alloc] init];
        state_->player = [[AVAudioPlayerNode alloc] init];
        [state_->outputEngine attachNode:state_->player];
        [state_->outputEngine connect:state_->player to:state_->outputEngine.mainMixerNode format:format];
      }
      if (!state_->outputEngine.isRunning) {
        NSError* error = nil;
        if (![state_->outputEngine startAndReturnError:&error]) {
          state_->queue->Push(Event::Type::Error, "无法播放千问回复，请检查系统声音输出");
          return;
        }
      }
      const AVAudioFrameCount frames = static_cast<AVAudioFrameCount>(pcm.size() / 2);
      AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:frames];
      buffer.frameLength = frames;
      for (size_t i = 0; i < frames; ++i) {
        const uint16_t sample = static_cast<unsigned char>(pcm[2 * i]) |
            (static_cast<uint16_t>(static_cast<unsigned char>(pcm[2 * i + 1])) << 8);
        buffer.floatChannelData[0][i] = static_cast<int16_t>(sample) / 32768.0f;
      }
      state_->player.volume = std::clamp(volume, 0.0f, 1.0f);
      uint64_t generation;
      {
        std::lock_guard<std::mutex> lock(state_->playbackMutex);
        generation = state_->playbackGeneration;
        state_->scheduledFrames += frames;
      }
      const std::weak_ptr<MacState> weak = state_;
      [state_->player scheduleBuffer:buffer completionCallbackType:AVAudioPlayerNodeCompletionDataPlayedBack
          completionHandler:^(AVAudioPlayerNodeCompletionCallbackType) {
            const auto state = weak.lock();
            if (state) {
              std::lock_guard<std::mutex> lock(state->playbackMutex);
              if (state->playbackGeneration == generation) state->completedFrames += frames;
            }
          }];
      if (!state_->player.isPlaying) [state_->player play];
    } @catch (NSException*) {
      state_->queue->Push(Event::Type::Error, "无法播放千问回复，请检查系统声音输出");
    }
  }

  bool IsPlaying() const override {
    std::lock_guard<std::mutex> lock(state_->playbackMutex);
    return state_->scheduledFrames > state_->completedFrames;
  }

  void StopPlayback() override {
    {
      std::lock_guard<std::mutex> lock(state_->playbackMutex);
      ++state_->playbackGeneration;
      state_->scheduledFrames = 0;
      state_->completedFrames = 0;
    }
    [state_->player stop];
  }

  void ShowIndicator(GLFWwindow* window, const std::string& text, bool error) override {
    if (text.empty()) { [state_->indicator orderOut:nil]; return; }
    if (!state_->indicator) {
      state_->indicator = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 320, 56)
          styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
          backing:NSBackingStoreBuffered defer:NO];
      state_->indicator.releasedWhenClosed = NO;
      state_->indicator.level = NSFloatingWindowLevel;
      state_->indicator.opaque = NO;
      state_->indicator.backgroundColor = [NSColor colorWithWhite:0.12 alpha:0.94];
      state_->indicator.hasShadow = YES;
      state_->indicator.ignoresMouseEvents = YES;
      state_->indicator.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
          NSWindowCollectionBehaviorFullScreenAuxiliary;
      state_->indicator.contentView.wantsLayer = YES;
      state_->indicator.contentView.layer.cornerRadius = 12;
      state_->label = [NSTextField wrappingLabelWithString:@""];
      state_->label.frame = NSMakeRect(14, 8, 292, 40);
      state_->label.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
      state_->label.alignment = NSTextAlignmentCenter;
      [state_->indicator.contentView addSubview:state_->label];
    }
    state_->label.stringValue = Text(text);
    state_->label.textColor = error ? [NSColor colorWithRed:1 green:0.65 blue:0.65 alpha:1] : NSColor.whiteColor;
    NSWindow* pet = glfwGetCocoaWindow(window);
    NSRect frame = pet.frame;
    NSRect screen = (pet.screen ?: NSScreen.mainScreen).visibleFrame;
    const CGFloat x = std::clamp(NSMidX(frame) - 160, NSMinX(screen), NSMaxX(screen) - 320);
    const CGFloat y = std::clamp(NSMaxY(frame) + 8, NSMinY(screen), NSMaxY(screen) - 56);
    [state_->indicator setFrameOrigin:NSMakePoint(x, y)];
    [state_->indicator orderFrontRegardless];
  }

 private:
  std::shared_ptr<MacState> state_ = std::make_shared<MacState>();
};

NSMutableDictionary* KeyQuery(const std::string& profile) {
  return [@{(__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
      (__bridge id)kSecAttrService: @"cn.vjoi.jpet.qwen",
      (__bridge id)kSecAttrAccount: Text(profile)} mutableCopy];
}
}  // namespace

std::unique_ptr<Platform> MakePlatform() { return std::make_unique<MacPlatform>(); }

bool SaveApiKey(const std::string& profile, const std::string& key, std::string& error) {
  @autoreleasepool {
    NSMutableDictionary* query = KeyQuery(profile);
    OSStatus result;
    if (key.empty()) {
      result = SecItemDelete((__bridge CFDictionaryRef)query);
      if (result == errSecItemNotFound) result = errSecSuccess;
    } else {
      NSData* data = [NSData dataWithBytes:key.data() length:key.size()];
      NSDictionary* update = @{(__bridge id)kSecValueData: data};
      result = SecItemUpdate((__bridge CFDictionaryRef)query, (__bridge CFDictionaryRef)update);
      if (result == errSecItemNotFound) {
        query[(__bridge id)kSecValueData] = data;
        query[(__bridge id)kSecAttrAccessible] = (__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;
        result = SecItemAdd((__bridge CFDictionaryRef)query, nullptr);
      }
    }
    if (result != errSecSuccess) error = "无法保存 API Key，请允许 JPet 访问系统钥匙串";
    return result == errSecSuccess;
  }
}

std::string LoadApiKey(const std::string& profile, std::string& error) {
  @autoreleasepool {
    NSMutableDictionary* query = KeyQuery(profile);
    query[(__bridge id)kSecReturnData] = @YES;
    query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
    CFTypeRef result = nullptr;
    const auto status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
    if (status == errSecItemNotFound) return {};
    if (status != errSecSuccess) { error = "无法读取 API Key，请允许 JPet 访问系统钥匙串"; return {}; }
    NSData* data = CFBridgingRelease(result);
    return std::string(static_cast<const char*>(data.bytes), data.length);
  }
}
}  // namespace Voice
