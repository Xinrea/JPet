// Opt-in integration probe. Reads selected credentials without printing them,
// opens RocksDB read-only, never changes game state and never reads the desktop.
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#import <LocalAuthentication/LocalAuthentication.h>
#import <CoreGraphics/CoreGraphics.h>
#include "VoiceToolsNetwork.hpp"
#include "VoiceTools.hpp"
#include <rocksdb/db.h>
#include <toml++/toml.hpp>
#include <iostream>
#include <fstream>
#include <chrono>

namespace Voice { bool EncodeDesktopImage(CGImageRef, DesktopImage&, std::string&); }
namespace {
using nlohmann::json;
int failures = 0;
void Report(const char* name, const json& result) {
  const bool ok = result.value("ok", false);
  std::cout << name << ": " << (ok ? "PASS" : "FAIL") << '\n';
  if (ok && result.contains("results")) std::cout << "result_count=" << result["results"].size() << '\n';
  if (ok && result.contains("sources")) std::cout << "source_count=" << result["sources"].size() << '\n';
  if (!ok) { ++failures; std::cout << result.value("error", std::string("unexpected service response")) << '\n'; }
}
}
int main(int argc, char** argv) {
  @autoreleasepool {
    try {
      if (argc != 2 && !(argc == 3 && std::string(argv[2]) == "--bilibili-only")) {
        std::cerr << "Usage: voice_tools_network_probe <JPet profile directory> [--bilibili-only]\n"; return 2;
      }
      const std::string profile = argv[1];
      rocksdb::DB* raw = nullptr;
      if (!rocksdb::DB::OpenForReadOnly(rocksdb::Options{}, profile + "/GameData", &raw).ok()) { std::cerr << "cannot open profile read-only\n"; return 1; }
      std::unique_ptr<rocksdb::DB> db(raw);
      std::string cookies, uid;
      db->Get(rocksdb::ReadOptions{}, "cookies", &cookies); db->Get(rocksdb::ReadOptions{}, "uid", &uid);
      if (argc == 3) {
        for (const auto& pair : {std::pair{"video", "千问"}, {"user", "哔哩哔哩"}, {"live", "游戏"},
          {"article", "千问"}, {"bangumi", "凡人修仙传"}, {"film", "纪录片"}}) {
          Report(pair.first, Voice::SearchBilibili({{"query", pair.second}, {"type", pair.first},
            {"order", "relevance"}, {"page", 1}, {"limit", 3}}, cookies, uid));
        }
        return failures ? 1 : 0;
      }
      {
        Report("Bilibili authenticated video search", Voice::SearchBilibili({{"query", "千问"}, {"type", "video"}, {"order", "relevance"}, {"page", 1}, {"limit", 3}}, cookies, uid));
      }
      const auto settings = toml::parse_file(profile + "/jpet.toml");
      const auto workspace = settings["voice"]["workspace_id"].value_or(std::string{});
      LAContext* authentication = [[LAContext alloc] init];
      authentication.interactionNotAllowed = YES;
      NSDictionary* query = @{(__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService: @"cn.vjoi.jpet.qwen", (__bridge id)kSecAttrAccount: @(profile.c_str()),
        (__bridge id)kSecReturnData: @YES, (__bridge id)kSecMatchLimit: (__bridge id)kSecMatchLimitOne,
        (__bridge id)kSecUseAuthenticationContext: authentication};
      CFTypeRef stored = nullptr;
      const auto status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &stored);
      if (status != errSecSuccess) { std::cout << "Qwen probes SKIP: credential unavailable without interactive keychain access\n"; return failures ? 1 : 0; }
      NSData* data = CFBridgingRelease(stored);
      const std::string key(static_cast<const char*>(data.bytes), data.length);
      Report("Qwen web search", Voice::SearchWeb({{"query", "阿里云百炼官方文档网址"}, {"limit", 5}}, workspace, key));
      auto color = CGColorSpaceCreateDeviceRGB();
      auto context = CGBitmapContextCreate(nullptr, 300, 200, 8, 1200, color, kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big);
      CGColorSpaceRelease(color);
      CGContextSetRGBFillColor(context, 1, 1, 1, 1); CGContextFillRect(context, CGRectMake(0, 0, 300, 200));
      CGContextSetRGBFillColor(context, 1, 0, 0, 1); CGContextFillEllipseInRect(context, CGRectMake(100, 50, 100, 100));
      auto source = CGBitmapContextCreateImage(context); CGContextRelease(context);
      Voice::DesktopImage image; std::string error;
      const bool encoded = Voice::EncodeDesktopImage(source, image, error); CGImageRelease(source);
      if (!encoded) throw std::runtime_error("synthetic image failed");
      Report("Qwen vision with synthetic image", Voice::DescribeDesktop({{"question", "图片中是什么颜色的什么形状？"}, {"display", 0}}, image, workspace, key));
      return failures ? 1 : 0;
    } catch (const std::exception&) { std::cerr << "integration probe failed; raw service/credential data suppressed\n"; return 1; }
  }
}
