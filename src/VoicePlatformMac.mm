#include "VoicePlatform.hpp"
#include "VoiceEventQueue.hpp"
#include "VoiceAudioMac.hpp"
#include "VoiceIndicatorStyle.hpp"
#include "LAppPal.hpp"

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>
#import <IOKit/hidsystem/IOLLEvent.h>
#import <Carbon/Carbon.h>  // kVK_* virtual key codes
#import <Security/Security.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <algorithm>
#include <cmath>
#include <cstring>

@interface JPetVoiceIndicatorView : NSView
@property(nonatomic) BOOL error;
@property(nonatomic, copy) NSString* shortcutHint;
@property(nonatomic) int pulseFrame;
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
  [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(150, 5, Width - 164, 18) xRadius:7 yRadius:7] fill];
  NSMutableParagraphStyle* centered = [[NSMutableParagraphStyle alloc] init];
  centered.alignment = NSTextAlignmentCenter;
  [self.shortcutHint drawInRect:NSMakeRect(154, 7, Width - 172, 14) withAttributes:@{
    NSFontAttributeName:[NSFont systemFontOfSize:10 weight:NSFontWeightBold],
    NSForegroundColorAttributeName:color(self.error ? Error : Ink),
    NSParagraphStyleAttributeName:centered
  }];
  if (self.pulseFrame >= 0) {
    const double phase = self.pulseFrame / 30.0;
    const double radius = 18 + 5 * phase;
    [[color(Accent) colorWithAlphaComponent:.65 * (1 - phase)] setStroke];
    NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:
        NSMakeRect(31 - radius, 56 - radius, radius * 2, radius * 2)];
    ring.lineWidth = 1.5;
    [ring stroke];
  }
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
  NSRectFill(NSMakeRect(0, NSHeight(self.bounds) - 5, Width, 5));
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

constexpr AudioObjectPropertyElement MainElement = 0;  // kAudioObjectPropertyElementMain needs macOS 12.

// Core Audio devices with streams in `scope` (input or output), by UID and name.
std::vector<std::pair<AudioDeviceID, AudioDevice>> CoreAudioDevices(AudioObjectPropertyScope scope) {
  std::vector<std::pair<AudioDeviceID, AudioDevice>> out;
  AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, MainElement};
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr) return out;
  std::vector<AudioDeviceID> ids(size / sizeof(AudioDeviceID));
  if (ids.empty() || AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, ids.data()) != noErr)
    return out;
  for (const auto id : ids) {
    AudioObjectPropertyAddress streams{kAudioDevicePropertyStreams, scope, MainElement};
    UInt32 streamSize = 0;
    if (AudioObjectGetPropertyDataSize(id, &streams, 0, nullptr, &streamSize) != noErr || streamSize == 0) continue;
    const auto text = [id](AudioObjectPropertySelector selector) {
      AudioObjectPropertyAddress property{selector, kAudioObjectPropertyScopeGlobal, MainElement};
      CFStringRef value = nullptr;
      UInt32 valueSize = sizeof(value);
      std::string result;
      if (AudioObjectGetPropertyData(id, &property, 0, nullptr, &valueSize, &value) == noErr && value) {
        const char* utf8 = [(__bridge NSString*)value UTF8String];
        if (utf8) result = utf8;
        CFRelease(value);
      }
      return result;
    };
    auto uid = text(kAudioDevicePropertyDeviceUID);
    if (!uid.empty()) out.push_back({id, {std::move(uid), text(kAudioObjectPropertyName)}});
  }
  return out;
}

// Points an engine's input or output unit at the device `uid` and returns its
// name; with no UID, or one no longer connected, the unit keeps the default.
std::string SelectDevice(AVAudioIONode* node, const std::string& uid, AudioObjectPropertyScope scope) {
  if (uid.empty()) return "default";
  for (const auto& [id, device] : CoreAudioDevices(scope)) {
    if (device.id != uid) continue;
    AudioDeviceID value = id;
    if (AudioUnitSetProperty(node.audioUnit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                             &value, sizeof(value)) == noErr)
      return "\"" + device.name + "\"";
    break;
  }
  return "default (saved device missing)";
}

struct MacState {
  std::shared_ptr<EventQueue> queue = std::make_shared<EventQueue>();
  AVAudioEngine* __strong inputEngine = nil;
  AVAudioEngine* __strong outputEngine = nil;
  AVAudioPlayerNode* __strong player = nil;
  NSPanel* __strong indicator = nil;
  NSTextField* __strong label = nil;
  // Device UIDs chosen in the voice settings (empty: system default).
  std::string inputDevice, outputDevice;
  uint64_t captureRequest = 0;
  bool captureWanted = false;
  bool tapInstalled = false;
  std::mutex playbackMutex;
  uint64_t playbackGeneration = 0;
  uint64_t scheduledFrames = 0;
  uint64_t completedFrames = 0;

  void StartInputEngine() {
    if (!captureWanted || tapInstalled) return;
    @try {
      inputEngine = [[AVAudioEngine alloc] init];
      AVAudioInputNode* input = inputEngine.inputNode;
      // Before reading the format, which follows the selected device.
      const auto device = SelectDevice(input, inputDevice, kAudioObjectPropertyScopeInput);
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
          "selected_channel=1 output_rate=16000 output_channels=1 sample_format=s16le device=%s",
          hardware.sampleRate, hardware.channelCount, hardware.channelLayout.layoutTag, device.c_str());
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
    StopPlayback();
    [state_->outputEngine stop];
    [state_->indicator orderOut:nil];
    [state_->indicator close];
  }

  bool KeyHeld(const Key& key) const override {
    constexpr auto mac = [](const char* code) { return KeyCode(code, true); };
    static_assert(mac("Control") == NSEventModifierFlagControl && mac("ControlLeft") == NX_DEVICELCTLKEYMASK &&
        mac("ControlRight") == NX_DEVICERCTLKEYMASK && mac("Alt") == NSEventModifierFlagOption &&
        mac("AltLeft") == NX_DEVICELALTKEYMASK && mac("AltRight") == NX_DEVICERALTKEYMASK &&
        mac("Shift") == NSEventModifierFlagShift && mac("ShiftLeft") == NX_DEVICELSHIFTKEYMASK &&
        mac("ShiftRight") == NX_DEVICERSHIFTKEYMASK && mac("MetaLeft") == NX_DEVICELCMDKEYMASK &&
        mac("MetaRight") == NX_DEVICERCMDKEYMASK, "modifiers match the macOS modifier flags");
    static_assert(mac("F1") == kVK_F1 && mac("F2") == kVK_F2 && mac("F3") == kVK_F3 && mac("F4") == kVK_F4 &&
        mac("F5") == kVK_F5 && mac("F6") == kVK_F6 && mac("F7") == kVK_F7 && mac("F8") == kVK_F8 &&
        mac("F9") == kVK_F9 && mac("F10") == kVK_F10 && mac("F11") == kVK_F11 && mac("F12") == kVK_F12 &&
        mac("F13") == kVK_F13 && mac("F14") == kVK_F14 && mac("F15") == kVK_F15 && mac("F16") == kVK_F16 &&
        mac("F17") == kVK_F17 && mac("F18") == kVK_F18 && mac("F19") == kVK_F19 && mac("F20") == kVK_F20,
        "function keys match the macOS virtual keys");
    static_assert(mac("KeyA") == kVK_ANSI_A && mac("KeyB") == kVK_ANSI_B && mac("KeyC") == kVK_ANSI_C &&
        mac("KeyD") == kVK_ANSI_D && mac("KeyE") == kVK_ANSI_E && mac("KeyF") == kVK_ANSI_F &&
        mac("KeyG") == kVK_ANSI_G && mac("KeyH") == kVK_ANSI_H && mac("KeyI") == kVK_ANSI_I &&
        mac("KeyJ") == kVK_ANSI_J && mac("KeyK") == kVK_ANSI_K && mac("KeyL") == kVK_ANSI_L &&
        mac("KeyM") == kVK_ANSI_M && mac("KeyN") == kVK_ANSI_N && mac("KeyO") == kVK_ANSI_O &&
        mac("KeyP") == kVK_ANSI_P && mac("KeyQ") == kVK_ANSI_Q && mac("KeyR") == kVK_ANSI_R &&
        mac("KeyS") == kVK_ANSI_S && mac("KeyT") == kVK_ANSI_T && mac("KeyU") == kVK_ANSI_U &&
        mac("KeyV") == kVK_ANSI_V && mac("KeyW") == kVK_ANSI_W && mac("KeyX") == kVK_ANSI_X &&
        mac("KeyY") == kVK_ANSI_Y && mac("KeyZ") == kVK_ANSI_Z, "letters match the macOS virtual keys");
    static_assert(mac("Digit0") == kVK_ANSI_0 && mac("Digit1") == kVK_ANSI_1 && mac("Digit2") == kVK_ANSI_2 &&
        mac("Digit3") == kVK_ANSI_3 && mac("Digit4") == kVK_ANSI_4 && mac("Digit5") == kVK_ANSI_5 &&
        mac("Digit6") == kVK_ANSI_6 && mac("Digit7") == kVK_ANSI_7 && mac("Digit8") == kVK_ANSI_8 &&
        mac("Digit9") == kVK_ANSI_9 && mac("Backquote") == kVK_ANSI_Grave && mac("Minus") == kVK_ANSI_Minus &&
        mac("Equal") == kVK_ANSI_Equal && mac("BracketLeft") == kVK_ANSI_LeftBracket &&
        mac("BracketRight") == kVK_ANSI_RightBracket && mac("Backslash") == kVK_ANSI_Backslash &&
        mac("Semicolon") == kVK_ANSI_Semicolon && mac("Quote") == kVK_ANSI_Quote && mac("Comma") == kVK_ANSI_Comma &&
        mac("Period") == kVK_ANSI_Period && mac("Slash") == kVK_ANSI_Slash, "digits and symbols match the macOS virtual keys");
    static_assert(mac("Space") == kVK_Space && mac("Tab") == kVK_Tab && mac("Enter") == kVK_Return &&
        mac("Backspace") == kVK_Delete && mac("Escape") == kVK_Escape && mac("Delete") == kVK_ForwardDelete &&
        mac("Home") == kVK_Home && mac("End") == kVK_End && mac("PageUp") == kVK_PageUp && mac("PageDown") == kVK_PageDown &&
        mac("ArrowLeft") == kVK_LeftArrow && mac("ArrowUp") == kVK_UpArrow && mac("ArrowRight") == kVK_RightArrow &&
        mac("ArrowDown") == kVK_DownArrow && mac("Numpad0") == kVK_ANSI_Keypad0 && mac("Numpad1") == kVK_ANSI_Keypad1 &&
        mac("Numpad2") == kVK_ANSI_Keypad2 && mac("Numpad3") == kVK_ANSI_Keypad3 && mac("Numpad4") == kVK_ANSI_Keypad4 &&
        mac("Numpad5") == kVK_ANSI_Keypad5 && mac("Numpad6") == kVK_ANSI_Keypad6 && mac("Numpad7") == kVK_ANSI_Keypad7 &&
        mac("Numpad8") == kVK_ANSI_Keypad8 && mac("Numpad9") == kVK_ANSI_Keypad9 &&
        mac("NumpadMultiply") == kVK_ANSI_KeypadMultiply && mac("NumpadAdd") == kVK_ANSI_KeypadPlus &&
        mac("NumpadSubtract") == kVK_ANSI_KeypadMinus && mac("NumpadDecimal") == kVK_ANSI_KeypadDecimal &&
        mac("NumpadDivide") == kVK_ANSI_KeypadDivide, "editing and keypad keys match the macOS virtual keys");
    // Modifier flags are readable without permission; other keys need Input Monitoring.
    if (key.kind == KeyKind::Modifier) return ([NSEvent modifierFlags] & key.mac) != 0;
    return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, static_cast<CGKeyCode>(key.mac));
  }
  bool ShortcutReadable(const Shortcut& shortcut, bool prompt) override {
    if (shortcut.ModifiersOnly() || CGPreflightListenEventAccess()) return true;
    if (prompt) CGRequestListenEventAccess();
    return false;
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
        // Before mainMixerNode connects the output unit.
        const auto device = SelectDevice(state_->outputEngine.outputNode, state_->outputDevice, kAudioObjectPropertyScopeOutput);
        LAppPal::PrintLog("[Voice] Playback device=%s", device.c_str());
        state_->player = [[AVAudioPlayerNode alloc] init];
        [state_->outputEngine attachNode:state_->player];
        [state_->outputEngine connect:state_->player to:state_->outputEngine.mainMixerNode format:format];
      }
      if (!state_->outputEngine.isRunning) {
        NSError* error = nil;
        if (![state_->outputEngine startAndReturnError:&error]) {
          state_->queue->Push(Event::Type::Error, "无法播放轴伊的回复，请检查系统声音输出");
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
      state_->queue->Push(Event::Type::Error, "无法播放轴伊的回复，请检查系统声音输出");
    }
  }

  bool IsPlaying() const override {
    std::lock_guard<std::mutex> lock(state_->playbackMutex);
    return state_->scheduledFrames > state_->completedFrames;
  }

  uint64_t PlayedSamples() const override {
    std::lock_guard<std::mutex> lock(state_->playbackMutex);
    return state_->completedFrames;
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

  void SetDevices(const std::string& input, const std::string& output) override {
    state_->inputDevice = input;  // The input engine is rebuilt on every StartCapture.
    if (state_->outputDevice == output) return;
    state_->outputDevice = output;
    // The output engine lives between replies; rebuild it on the new device.
    StopPlayback();
    [state_->outputEngine stop];
    state_->outputEngine = nil;
    state_->player = nil;
  }

  void ShowIndicator(GLFWwindow* window, const Indicator::Content& content) override {
    if (content.text.empty()) { [state_->indicator orderOut:nil]; return; }
    if (!state_->indicator) {
      state_->indicator = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, Indicator::Width, Indicator::MinHeight)
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
          NSMakeRect(0, 0, Indicator::Width, Indicator::MinHeight)];
      state_->label = [NSTextField wrappingLabelWithString:@""];
      state_->label.frame = NSMakeRect(Indicator::BodyLeft, Indicator::BodyTop, Indicator::BodyWidth,
          Indicator::MinHeight - Indicator::BodyTop - Indicator::BodyBottom);
      state_->label.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
      state_->label.alignment = NSTextAlignmentLeft;
      state_->label.maximumNumberOfLines = 5;
      state_->label.lineBreakMode = NSLineBreakByWordWrapping;
      state_->label.cell.truncatesLastVisibleLine = YES;
      [state_->indicator.contentView addSubview:state_->label];
    }
    NSString* value = Text(content.text);
    CGFloat height = NSHeight(state_->indicator.frame);
    if (![state_->label.stringValue isEqualToString:value]) {
      state_->label.stringValue = value;
      const auto textSize = [state_->label.cell cellSizeForBounds:NSMakeRect(0, 0, Indicator::BodyWidth, CGFLOAT_MAX)];
      height = Indicator::HeightForText(static_cast<int>(std::ceil(textSize.height)));
    }
    const auto ink = content.error ? Indicator::Error : Indicator::Ink;
    state_->label.textColor = [NSColor colorWithRed:ink.red / 255.0 green:ink.green / 255.0
                                            blue:ink.blue / 255.0 alpha:1];
    JPetVoiceIndicatorView* view = (JPetVoiceIndicatorView*)state_->indicator.contentView;
    NSString* hint = Text(content.shortcutHint);
    const int pulse = content.microphoneOn && !content.error ? Indicator::PulseFrame() : -1;
    if (view.error != content.error || ![view.shortcutHint isEqualToString:hint] || view.pulseFrame != pulse) {
      view.error = content.error;
      view.shortcutHint = hint;
      view.pulseFrame = pulse;
      view.needsDisplay = YES;
    }
    NSWindow* pet = glfwGetCocoaWindow(window);
    NSRect frame = pet.frame;
    NSRect screen = (pet.screen ?: NSScreen.mainScreen).visibleFrame;
    const CGFloat x = std::clamp(NSMidX(frame) - Indicator::Width / 2.0, NSMinX(screen), NSMaxX(screen) - Indicator::Width);
    const CGFloat y = std::clamp(NSMaxY(frame) + 8, NSMinY(screen), NSMaxY(screen) - height);
    state_->label.frame = NSMakeRect(Indicator::BodyLeft, Indicator::BodyTop, Indicator::BodyWidth,
        height - Indicator::BodyTop - Indicator::BodyBottom);
    const NSRect target = NSMakeRect(x, y, Indicator::Width, height);
    if (!NSEqualRects(state_->indicator.frame, target)) {
      view.needsDisplay = YES;
      [state_->indicator setFrame:target display:YES];
    }
    [state_->indicator orderFrontRegardless];
  }

 private:
  std::shared_ptr<MacState> state_ = std::make_shared<MacState>();
};

NSMutableDictionary* KeyQuery(const std::string& profile, NSString* service) {
  return [@{(__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
      (__bridge id)kSecAttrService: service,
      (__bridge id)kSecAttrAccount: Text(profile)} mutableCopy];
}

NSString* const BailianService = @"cn.vjoi.jpet.qwen";  // Legacy; only removed.
// Chat service keys: the third-party key keeps the former custom endpoint's item.
NSString* ChatService(const std::string& provider) {
  return provider == "thirdparty" ? @"cn.vjoi.jpet.chat" : [@"cn.vjoi.jpet.chat." stringByAppendingString:Text(provider)];
}

// An empty key removes the saved one.
bool SaveSecret(NSMutableDictionary* query, const std::string& key) {
  OSStatus result;
  if (key.empty()) {
    result = SecItemDelete((__bridge CFDictionaryRef)query);
    return result == errSecSuccess || result == errSecItemNotFound;
  }
  NSData* data = [NSData dataWithBytes:key.data() length:key.size()];
  NSDictionary* update = @{(__bridge id)kSecValueData: data};
  result = SecItemUpdate((__bridge CFDictionaryRef)query, (__bridge CFDictionaryRef)update);
  if (result == errSecItemNotFound) {
    query[(__bridge id)kSecValueData] = data;
    query[(__bridge id)kSecAttrAccessible] = (__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;
    result = SecItemAdd((__bridge CFDictionaryRef)query, nullptr);
  }
  return result == errSecSuccess;
}

// Empty when no key is saved; false only when the Keychain refuses access.
bool LoadSecret(NSMutableDictionary* query, std::string& key) {
  query[(__bridge id)kSecReturnData] = @YES;
  query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
  CFTypeRef result = nullptr;
  const auto status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
  if (status == errSecItemNotFound) return true;
  if (status != errSecSuccess) return false;
  NSData* data = CFBridgingRelease(result);
  key.assign(static_cast<const char*>(data.bytes), data.length);
  return true;
}
}  // namespace

std::unique_ptr<Platform> MakePlatform() { return std::make_unique<MacPlatform>(); }

AudioDevices ListAudioDevices() {
  AudioDevices devices;
  for (auto& [id, device] : CoreAudioDevices(kAudioObjectPropertyScopeInput)) devices.inputs.push_back(std::move(device));
  for (auto& [id, device] : CoreAudioDevices(kAudioObjectPropertyScopeOutput)) devices.outputs.push_back(std::move(device));
  return devices;
}

void RemoveLegacyApiKey(const std::string& profile) {
  @autoreleasepool { SaveSecret(KeyQuery(profile, BailianService), ""); }
}

bool SaveChatApiKey(const std::string& profile, const std::string& provider, const std::string& key, std::string& error) {
  @autoreleasepool {
    if (SaveSecret(KeyQuery(profile, ChatService(provider)), key)) return true;
    error = "无法保存大模型 API Key，请允许 JPet 访问系统钥匙串";
    return false;
  }
}

std::string LoadChatApiKey(const std::string& profile, const std::string& provider, std::string& error) {
  @autoreleasepool {
    std::string key;
    if (!LoadSecret(KeyQuery(profile, ChatService(provider)), key)) error = "无法读取大模型 API Key，请允许 JPet 访问系统钥匙串";
    return key;
  }
}

}  // namespace Voice
