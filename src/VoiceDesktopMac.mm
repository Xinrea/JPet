#import <AppKit/AppKit.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <ImageIO/ImageIO.h>
#include "VoiceDesktop.hpp"
#include <algorithm>
#include <cmath>
#include <dlfcn.h>
#include <memory>
#include <mutex>
#include <vector>

namespace Voice {
namespace {
struct CaptureState {
  std::mutex mutex;
  CGImageRef image = nullptr;
  std::string error;
  dispatch_semaphore_t signal = dispatch_semaphore_create(0);
  ~CaptureState() { if (image) CGImageRelease(image); }
};
CGImageRef Resize(CGImageRef image, size_t width, size_t height) {
  CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
  auto context = CGBitmapContextCreate(nullptr, width, height, 8, width * 4, color,
    kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big);
  CGColorSpaceRelease(color);
  if (!context) return nullptr;
  CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
  CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
  auto result = CGBitmapContextCreateImage(context);
  CGContextRelease(context);
  return result;
}
} // namespace

// Also exercised with generated images, without reading the user's screen.
bool EncodeDesktopImage(CGImageRef source, DesktopImage& image, std::string& error) {
  if (!source || !CGImageGetWidth(source) || !CGImageGetHeight(source)) {
    error = "未获得有效桌面截图";
    return false;
  }
  @autoreleasepool {
    for (const auto limit : {1920, 1280}) {
      const auto scale = std::min(1.0, double(limit) / std::max(CGImageGetWidth(source), CGImageGetHeight(source)));
      const auto width = std::max<size_t>(1, std::lround(CGImageGetWidth(source) * scale));
      const auto height = std::max<size_t>(1, std::lround(CGImageGetHeight(source) * scale));
      auto resized = Resize(source, width, height);
      if (!resized) continue;
      NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithCGImage:resized];
      CGImageRelease(resized);
      for (const double quality : {0.8, 0.55, 0.3}) {
        NSData* data = [bitmap representationUsingType:NSBitmapImageFileTypeJPEG
          properties:@{NSImageCompressionFactor: @(quality)}];
        if (data.length && data.length <= 512 * 1024) {
          image.jpeg.assign(static_cast<const char*>(data.bytes), data.length);
          image.width = static_cast<int>(width);
          image.height = static_cast<int>(height);
          return true;
        }
      }
    }
  }
  error = "桌面截图压缩失败，请重试";
  return false;
}

bool CaptureDesktop(int display, DesktopImage& image, std::string& error) {
  @autoreleasepool {
    if (!CGPreflightScreenCaptureAccess()) {
      dispatch_async(dispatch_get_main_queue(), ^{ CGRequestScreenCaptureAccess(); });
      error = "请在系统设置 → 隐私与安全性 → 屏幕与系统音频录制中允许JPet录屏，再重启JPet并重新请求查看桌面";
      return false;
    }
    uint32_t count = 0;
    if (CGGetActiveDisplayList(0, nullptr, &count) != kCGErrorSuccess || !count || count > 32) {
      error = "无法枚举当前显示器";
      return false;
    }
    std::vector<CGDirectDisplayID> displays(count);
    if (CGGetActiveDisplayList(count, displays.data(), &count) != kCGErrorSuccess) {
      error = "无法获取当前显示器";
      return false;
    }
    displays.resize(count);
    const auto main = CGMainDisplayID();
    std::sort(displays.begin(), displays.end(), [main](auto a, auto b) {
      if (a == b) return false;
      if (a == main) return true;
      if (b == main) return false;
      return a < b;
    });
    image.displayCount = static_cast<int>(count);
    if (display < 0 || display >= static_cast<int>(count)) {
      error = "指定显示器不存在；display=0为主屏";
      return false;
    }
    const auto id = displays[display];
    auto state = std::make_shared<CaptureState>();
    if (@available(macOS 14.0, *)) {
      [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent* content, NSError* failure) {
        SCDisplay* selected = nil;
        for (SCDisplay* candidate in content.displays) if (candidate.displayID == id) { selected = candidate; break; }
        if (failure || !selected) {
          { std::lock_guard<std::mutex> lock(state->mutex); state->error = "无法读取屏幕内容，请检查录屏权限后重试"; }
          dispatch_semaphore_signal(state->signal);
          return;
        }
        SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:selected excludingWindows:@[]];
        SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
        const double scale = std::min(1.0, 1920.0 / std::max(CGDisplayPixelsWide(id), CGDisplayPixelsHigh(id)));
        config.width = std::max<size_t>(1, std::lround(CGDisplayPixelsWide(id) * scale));
        config.height = std::max<size_t>(1, std::lround(CGDisplayPixelsHigh(id) * scale));
        config.showsCursor = YES;
        [SCScreenshotManager captureImageWithFilter:filter configuration:config completionHandler:^(CGImageRef captured, NSError* failure) {
          {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (failure || !captured) state->error = "桌面截图失败，请检查录屏权限后重试";
            else state->image = CGImageRetain(captured);
          }
          dispatch_semaphore_signal(state->signal);
        }];
      }];
      if (dispatch_semaphore_wait(state->signal, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC))) {
        error = "获取桌面截图超时，请重试";
        return false;
      }
    } else {
      // The old API is unavailable to SDK 15+ compilation but still exists on
      // macOS 11–13. Resolve it only on these systems; ScreenCaptureKit on 14+.
      using Capture = CGImageRef (*)(CGDirectDisplayID);
      auto capture = reinterpret_cast<Capture>(dlsym(RTLD_DEFAULT, "CGDisplayCreateImage"));
      if (capture) state->image = capture(id);
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->error.empty()) { error = state->error; return false; }
    return EncodeDesktopImage(state->image, image, error);
  }
}
} // namespace Voice
