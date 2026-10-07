#include "VoicePlatform.hpp"
#include "VoiceEventQueue.hpp"
#include "VoiceAudioMac.hpp"
#include "VoiceIndicatorStyle.hpp"
#include "LAppPal.hpp"

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <Security/Security.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <algorithm>
#include <cmath>
#include <cstring>

@interface JPetVoiceIndicatorView : NSView
@property(nonatomic) BOOL error;
@end

@implementation JPetVoiceIndicatorView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  using namespace Voice::Indicator;
  const auto color = [](Color value) {
    return [NSColor colorWithRed:value.red / 255.0 green:value.green / 255.0
                           blue:value.blue / 255.0 alpha:1];
  };
  const Color accent = self.error ? ErrorAccent : Accent;
  const Color soft = self.error ? ErrorSoft : Soft;
  [NSGraphicsContext saveGraphicsState];
  NSBezierPath* card = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, .5, .5)
      xRadius:Radius yRadius:Radius];
  [card addClip];
  [color(Background) setFill];
  [card fill];
  NSGradient* gradient = [[NSGradient alloc] initWithStartingColor:color(self.error ? ErrorTop : AccentTop)
                                                    endingColor:color(accent)];
  [gradient drawInRect:NSMakeRect(0, 0, Width, 28) angle:90];
  // Small diagonal bars echo the section ribbons in the settings panel.
  [[NSColor colorWithWhite:1 alpha:.22] setStroke];
  for (int x = 220; x < Width; x += 10) {
    NSBezierPath* stripe = [NSBezierPath bezierPath];
    [stripe moveToPoint:NSMakePoint(x, 0)];
    [stripe lineToPoint:NSMakePoint(x - 14, 28)];
    stripe.lineWidth = 3;
    [stripe stroke];
  }
  [@"JPet · 语音对话" drawAtPoint:NSMakePoint(14, 6) withAttributes:@{
    NSFontAttributeName:[NSFont systemFontOfSize:11 weight:NSFontWeightBold],
    NSForegroundColorAttributeName:NSColor.whiteColor
  }];
  [NSColor.whiteColor setFill];
  [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(277, 5, 49, 18) xRadius:7 yRadius:7] fill];
  [@"Option" drawAtPoint:NSMakePoint(285, 7) withAttributes:@{
    NSFontAttributeName:[NSFont systemFontOfSize:10 weight:NSFontWeightBold],
    NSForegroundColorAttributeName:color(self.error ? Error : Ink)
  }];
  [color(soft) setFill];
  [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(14, 39, 34, 34)] fill];
  [color(self.error ? Error : Accent) setStroke];
  NSBezierPath* mic = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(28, 46, 6, 12) xRadius:3 yRadius:3];
  mic.lineWidth = 1.8;
  [mic stroke];
  NSBezierPath* stand = [NSBezierPath bezierPath];
  [stand moveToPoint:NSMakePoint(24, 53)];
  [stand lineToPoint:NSMakePoint(24, 56)];
  [stand curveToPoint:NSMakePoint(38, 56) controlPoint1:NSMakePoint(24, 66) controlPoint2:NSMakePoint(38, 66)];
  [stand lineToPoint:NSMakePoint(38, 53)];
  [stand moveToPoint:NSMakePoint(31, 63)];
  [stand lineToPoint:NSMakePoint(31, 67)];
  [stand moveToPoint:NSMakePoint(27, 67)];
  [stand lineToPoint:NSMakePoint(35, 67)];
  stand.lineWidth = 1.8;
  stand.lineCapStyle = NSLineCapStyleRound;
  [stand stroke];
  [color(soft) setFill];
  NSRectFill(NSMakeRect(0, Height - 5, Width, 5));
  [color(self.error ? ErrorTop : Border) setStroke];
  card.lineWidth = 1;
  [card stroke];
  [NSGraphicsContext restoreGraphicsState];
}
@end

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
          state->queue->Network(generation, Event::Type::Diagnostic,
              "[Voice] WebSocket receive failed code=" + std::to_string(error.code) +
              " close_code=" + std::to_string(task.closeCode));
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
      AVAudioConverter* converter = MakeInputConverter(hardware);
      if (!converter) {
        queue->Push(Event::Type::Error, "无法转换麦克风音频，请更换输入设备后重试");
        return;
      }
      AVAudioFormat* target = converter.outputFormat;
      LAppPal::PrintLog("[Voice] Capture format input_rate=%.0f input_channels=%u input_layout=0x%x "
          "selected_channel=1 output_rate=16000 output_channels=1 sample_format=s16le",
          hardware.sampleRate, hardware.channelCount, hardware.channelLayout.layoutTag);
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
      if (error) {
        events->Network(generation, Event::Type::Diagnostic,
            "[Voice] WebSocket send failed code=" + std::to_string(error.code));
        events->Network(generation, Event::Type::Error, "千问语音发送失败，请检查网络后重试");
      }
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
      state_->indicator = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, Indicator::Width, Indicator::Height)
          styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
          backing:NSBackingStoreBuffered defer:NO];
      state_->indicator.releasedWhenClosed = NO;
      state_->indicator.level = NSFloatingWindowLevel;
      state_->indicator.opaque = NO;
      state_->indicator.backgroundColor = NSColor.clearColor;
      state_->indicator.hasShadow = YES;
      state_->indicator.ignoresMouseEvents = YES;
      state_->indicator.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
          NSWindowCollectionBehaviorFullScreenAuxiliary;
      state_->indicator.contentView = [[JPetVoiceIndicatorView alloc] initWithFrame:
          NSMakeRect(0, 0, Indicator::Width, Indicator::Height)];
      state_->label = [NSTextField wrappingLabelWithString:@""];
      state_->label.frame = NSMakeRect(60, 34, 264, 46);
      state_->label.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
      state_->label.alignment = NSTextAlignmentLeft;
      state_->label.maximumNumberOfLines = 3;
      [state_->indicator.contentView addSubview:state_->label];
    }
    NSString* value = Text(text);
    if (![state_->label.stringValue isEqualToString:value]) state_->label.stringValue = value;
    const auto ink = error ? Indicator::Error : Indicator::Ink;
    state_->label.textColor = [NSColor colorWithRed:ink.red / 255.0 green:ink.green / 255.0
                                            blue:ink.blue / 255.0 alpha:1];
    JPetVoiceIndicatorView* view = (JPetVoiceIndicatorView*)state_->indicator.contentView;
    if (view.error != error) { view.error = error; view.needsDisplay = YES; }
    NSWindow* pet = glfwGetCocoaWindow(window);
    NSRect frame = pet.frame;
    NSRect screen = (pet.screen ?: NSScreen.mainScreen).visibleFrame;
    const CGFloat x = std::clamp(NSMidX(frame) - Indicator::Width / 2.0, NSMinX(screen), NSMaxX(screen) - Indicator::Width);
    const CGFloat y = std::clamp(NSMaxY(frame) + 8, NSMinY(screen), NSMaxY(screen) - Indicator::Height);
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
