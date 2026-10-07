#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#include "VoiceDesktop.hpp"
#include <iostream>
#include <stdexcept>

namespace Voice { bool EncodeDesktopImage(CGImageRef, DesktopImage&, std::string&); }
int main() {
  @autoreleasepool {
    try {
      auto color = CGColorSpaceCreateDeviceRGB();
      auto context = CGBitmapContextCreate(nullptr, 3840, 2160, 8, 3840 * 4, color, kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big);
      CGColorSpaceRelease(color);
      if (!context) throw std::runtime_error("synthetic image allocation");
      auto pixels = static_cast<uint32_t*>(CGBitmapContextGetData(context));
      uint32_t random = 123;
      for (int i = 0; i < 3840 * 2160; ++i) { random = 1664525 * random + 1013904223; pixels[i] = random | 0xff000000; }
      auto source = CGBitmapContextCreateImage(context);
      CGContextRelease(context);
      Voice::DesktopImage result;
      std::string error;
      const auto success = Voice::EncodeDesktopImage(source, result, error);
      CGImageRelease(source);
      if (!success || result.jpeg.empty() || result.jpeg.size() > 512 * 1024 || result.width > 1920 || result.height * 16 != result.width * 9)
        throw std::runtime_error("screenshot preserves aspect ratio and fits payload bound");
      NSData* encoded = [NSData dataWithBytes:result.jpeg.data() length:result.jpeg.size()];
      auto decoder = CGImageSourceCreateWithData((__bridge CFDataRef)encoded, nullptr);
      auto decoded = decoder ? CGImageSourceCreateImageAtIndex(decoder, 0, nullptr) : nullptr;
      if (!decoded || CGImageGetWidth(decoded) != result.width || CGImageGetHeight(decoded) != result.height)
        throw std::runtime_error("JPEG decodes to declared dimensions");
      CGImageRelease(decoded); CFRelease(decoder);
      if (Voice::EncodeDesktopImage(nullptr, result, error) || error.empty()) throw std::runtime_error("missing capture reports failure");
      std::cout << "Desktop JPEG encoding tested with synthetic pixels; no screen read\n";
      return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
  }
}
